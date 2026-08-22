#include "VRBodyPoseManager.h"

#include "AvatarManager.h"
#include "RemoteSolvedPosePresentation.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::uint32_t kKnownPoseFlags = kPoseChunkPresent | kPoseChunkBasis |
                                          kPoseChunkAxisMask | kPoseChunkNodeMask;
constexpr std::uint32_t kKnownJointRotationFlags = kVrikJointChunkPresent | kVrikJointChunkIndexMask;
constexpr float kMaximumWorldPosition = 1000000.0f;
constexpr float kMaximumLocalPosition = 500.0f;
constexpr float kMinimumLocalScale = 0.5f;
constexpr float kMaximumLocalScale = 2.0f;
constexpr float kMinimumParentScale = 0.05f;
constexpr float kMaximumParentScale = 20.0f;
constexpr float kMinimumBasisLength = 0.95f;
constexpr float kMaximumBasisLength = 1.05f;
constexpr float kMaximumBasisDot = 0.05f;
constexpr float kMinimumBasisDeterminant = 0.95f;
constexpr std::size_t kMaximumPendingActors = 64;
constexpr std::uint16_t kMaximumPendingAttempts = 600;

using PoseNode = GameplayPoseNode;
static_assert(RemoteSolvedPosePresentation::kPoseNodeCount == static_cast<std::size_t>(PoseNode::Count));
static_assert(RemoteSolvedPosePresentation::kJointCount == kVrikJointRotationCount);

struct DecodedPoseChunk
{
    PoseNode Node{};
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    std::uint8_t Axis{};
    bool Basis{};
    RE::NiPoint3 Vector{};
    float Scale{1.0F};
};

struct DecodedJointRotationChunk
{
    std::uint8_t Joint{};
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    float X{};
    float Y{};
    float Z{};
    float W{1.0f};
};

struct PendingPose
{
    PoseNode Node{};
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    std::uint8_t BasisMask{};
    bool PositionValid{};
    RE::NiPoint3 Position{};
    RE::NiMatrix3 Rotation{};
    float Scale{1.0f};

    [[nodiscard]] bool Complete() const noexcept { return PositionValid && BasisMask == 0x7u; }
};

using PendingNodes = std::array<PendingPose, static_cast<std::size_t>(PoseNode::Count)>;
struct PendingActorPose
{
    RE::ActorHandle Actor{};
    std::uint64_t TargetHandle{};
    PendingNodes Nodes{};
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    std::uint32_t ExpectedNodeMask{};
    std::uint16_t Attempts{};
    bool CommitReceived{};
    bool CacheAdmitted{};
    BridgeIdentity Identity{};
};
std::unordered_map<std::uint64_t, PendingActorPose> s_pendingPoses;

struct PendingJointRotation
{
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    bool RotationValid{};
    RE::NiMatrix3 Rotation{};

    [[nodiscard]] bool Complete() const noexcept { return RotationValid; }
};

struct PendingActorJointPose
{
    RE::ActorHandle Actor{};
    std::uint64_t TargetHandle{};
    std::array<PendingJointRotation, kVrikJointRotationCount> Joints{};
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    std::uint32_t ExpectedJointMask{};
    std::uint16_t Attempts{};
    bool CommitReceived{};
    bool CacheAdmitted{};
    BridgeIdentity Identity{};
};
std::unordered_map<std::uint64_t, PendingActorJointPose> s_pendingJointPoses;

struct AppliedJointRoot
{
    std::uintptr_t Address{};
    std::uint32_t RootGeneration{};
};
std::unordered_map<std::uint64_t, AppliedJointRoot> s_appliedJointRoots;
std::unordered_map<std::uint64_t, AppliedJointRoot> s_appliedPoseRoots;

[[nodiscard]] bool IsFinite(const float a_value) noexcept
{
    return std::isfinite(a_value);
}

[[nodiscard]] bool IsFinite(const RE::NiPoint3& a_value) noexcept
{
    return IsFinite(a_value.x) && IsFinite(a_value.y) && IsFinite(a_value.z);
}

[[nodiscard]] bool IsBounded(const RE::NiPoint3& a_value, const float a_limit) noexcept
{
    return IsFinite(a_value) && std::abs(a_value.x) <= a_limit && std::abs(a_value.y) <= a_limit &&
           std::abs(a_value.z) <= a_limit;
}

[[nodiscard]] float Dot(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right) noexcept
{
    return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
}

[[nodiscard]] RE::NiPoint3 Cross(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right) noexcept
{
    return {
        a_left.y * a_right.z - a_left.z * a_right.y,
        a_left.z * a_right.x - a_left.x * a_right.z,
        a_left.x * a_right.y - a_left.y * a_right.x,
    };
}

[[nodiscard]] bool IsOrthonormal(const RE::NiMatrix3& a_matrix) noexcept
{
    const RE::NiPoint3 x{a_matrix.entry[0][0], a_matrix.entry[0][1], a_matrix.entry[0][2]};
    const RE::NiPoint3 y{a_matrix.entry[1][0], a_matrix.entry[1][1], a_matrix.entry[1][2]};
    const RE::NiPoint3 z{a_matrix.entry[2][0], a_matrix.entry[2][1], a_matrix.entry[2][2]};
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
        return false;

    const auto minimumLengthSquared = kMinimumBasisLength * kMinimumBasisLength;
    const auto maximumLengthSquared = kMaximumBasisLength * kMaximumBasisLength;
    const auto xLengthSquared = Dot(x, x);
    const auto yLengthSquared = Dot(y, y);
    const auto zLengthSquared = Dot(z, z);
    return xLengthSquared >= minimumLengthSquared && xLengthSquared <= maximumLengthSquared &&
           yLengthSquared >= minimumLengthSquared && yLengthSquared <= maximumLengthSquared &&
           zLengthSquared >= minimumLengthSquared && zLengthSquared <= maximumLengthSquared &&
           std::abs(Dot(x, y)) <= kMaximumBasisDot && std::abs(Dot(x, z)) <= kMaximumBasisDot &&
           std::abs(Dot(y, z)) <= kMaximumBasisDot && Dot(Cross(x, y), z) >= kMinimumBasisDeterminant;
}

[[nodiscard]] bool IsUnitQuaternion(
    const float a_x,
    const float a_y,
    const float a_z,
    const float a_w) noexcept
{
    if (!IsFinite(a_x) || !IsFinite(a_y) || !IsFinite(a_z) || !IsFinite(a_w))
        return false;
    const auto lengthSquared = a_x * a_x + a_y * a_y + a_z * a_z + a_w * a_w;
    return lengthSquared >= 0.95f && lengthSquared <= 1.05f;
}

[[nodiscard]] RE::NiMatrix3 QuaternionToMatrix(
    const float a_x,
    const float a_y,
    const float a_z,
    const float a_w) noexcept
{
    const auto xx = a_x * a_x;
    const auto yy = a_y * a_y;
    const auto zz = a_z * a_z;
    const auto xy = a_x * a_y;
    const auto xz = a_x * a_z;
    const auto yz = a_y * a_z;
    const auto xw = a_x * a_w;
    const auto yw = a_y * a_w;
    const auto zw = a_z * a_w;
    RE::NiMatrix3 result{};
    result.entry[0][0] = 1.0f - 2.0f * (yy + zz);
    result.entry[0][1] = 2.0f * (xy - zw);
    result.entry[0][2] = 2.0f * (xz + yw);
    result.entry[1][0] = 2.0f * (xy + zw);
    result.entry[1][1] = 1.0f - 2.0f * (xx + zz);
    result.entry[1][2] = 2.0f * (yz - xw);
    result.entry[2][0] = 2.0f * (xz - yw);
    result.entry[2][1] = 2.0f * (yz + xw);
    result.entry[2][2] = 1.0f - 2.0f * (xx + yy);
    return result;
}

[[nodiscard]] bool IsSafeTransform(const RE::NiTransform& a_transform, const float a_minimumScale,
                                   const float a_maximumScale) noexcept
{
    return IsBounded(a_transform.translate, kMaximumWorldPosition) && IsOrthonormal(a_transform.rotate) &&
           IsFinite(a_transform.scale) && a_transform.scale >= a_minimumScale && a_transform.scale <= a_maximumScale;
}

[[nodiscard]] bool HasNoPoseForms(const GameplayActionPayload& a_payload) noexcept
{
    return a_payload.SecondaryHandle.Value == 0 && a_payload.TargetLocalFormId == 0 && a_payload.LocalFormIdA == 0 &&
           a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0;
}

[[nodiscard]] bool IsLegacyGraphPayload(const GameplayActionPayload& a_payload, const float a_limit) noexcept
{
    return HasNoPoseForms(a_payload) && a_payload.ValueA == 0 && a_payload.ValueB == 0 && a_payload.ActionFlags == 0 &&
           std::abs(a_payload.ScalarA) <= a_limit && std::abs(a_payload.ScalarB) <= a_limit &&
           std::abs(a_payload.ScalarC) <= a_limit && std::abs(a_payload.ScalarD) <= a_limit;
}

[[nodiscard]] bool DecodePoseChunk(const GameplayActionPayload& a_payload, DecodedPoseChunk& ar_chunk) noexcept
{
    if (!HasNoPoseForms(a_payload) || (a_payload.ActionFlags & ~kKnownPoseFlags) != 0 ||
        (a_payload.ActionFlags & kPoseChunkPresent) == 0 || a_payload.ValueA == 0)
        return false;

    const auto rawNode = (a_payload.ActionFlags & kPoseChunkNodeMask) >> kPoseChunkNodeShift;
    if (rawNode >= static_cast<std::uint32_t>(PoseNode::Count) || !IsFinite(a_payload.ScalarA) ||
        !IsFinite(a_payload.ScalarB) || !IsFinite(a_payload.ScalarC) || !IsFinite(a_payload.ScalarD))
        return false;

    ar_chunk.Node = static_cast<PoseNode>(rawNode);
    ar_chunk.Sequence = static_cast<std::uint32_t>(a_payload.ValueA);
    ar_chunk.RootGeneration = static_cast<std::uint32_t>(a_payload.ValueB);
    ar_chunk.Vector = {a_payload.ScalarA, a_payload.ScalarB, a_payload.ScalarC};
    ar_chunk.Basis = (a_payload.ActionFlags & kPoseChunkBasis) != 0;
    ar_chunk.Axis = static_cast<std::uint8_t>(
        (a_payload.ActionFlags & kPoseChunkAxisMask) >> kPoseChunkAxisShift);
    if (ar_chunk.Basis)
        return ar_chunk.Axis < 3 && a_payload.ScalarD == 0.0F && IsBounded(ar_chunk.Vector, 1.1F);

    ar_chunk.Scale = a_payload.ScalarD;
    if (ar_chunk.Axis != 0 || ar_chunk.Scale < 0.01F || ar_chunk.Scale > 100.0F)
        return false;
    // HMD and hands are source-world transforms. FBT body nodes are local to
    // their skeleton parents and retain that local-space contract.
    return ar_chunk.Node >= PoseNode::Pelvis ? IsBounded(ar_chunk.Vector, kMaximumLocalPosition) :
                                               IsBounded(ar_chunk.Vector, kMaximumWorldPosition);
}

[[nodiscard]] bool DecodePoseCommit(
    const GameplayActionPayload& a_payload,
    std::uint32_t& ar_sequence,
    std::uint32_t& ar_rootGeneration,
    std::uint32_t& ar_expectedNodeMask) noexcept
{
    if (!HasNoPoseForms(a_payload) || a_payload.ValueA == 0 ||
        (a_payload.ActionFlags & ~kPoseCommitNodeMask) != 0 || a_payload.ActionFlags == 0 ||
        a_payload.ScalarA != 0.0F || a_payload.ScalarB != 0.0F ||
        a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
        return false;

    ar_sequence = static_cast<std::uint32_t>(a_payload.ValueA);
    ar_rootGeneration = static_cast<std::uint32_t>(a_payload.ValueB);
    ar_expectedNodeMask = a_payload.ActionFlags;
    return true;
}

[[nodiscard]] bool DecodeJointRotationChunk(
    const GameplayActionPayload& a_payload,
    DecodedJointRotationChunk& ar_chunk) noexcept
{
    if (!HasNoPoseForms(a_payload) || (a_payload.ActionFlags & ~kKnownJointRotationFlags) != 0 ||
        (a_payload.ActionFlags & kVrikJointChunkPresent) == 0 || a_payload.ValueA == 0 ||
        !IsFinite(a_payload.ScalarA) || !IsFinite(a_payload.ScalarB) || !IsFinite(a_payload.ScalarC) ||
        !IsFinite(a_payload.ScalarD))
        return false;

    const auto rawJoint = (a_payload.ActionFlags & kVrikJointChunkIndexMask) >> kVrikJointChunkIndexShift;
    if (rawJoint >= kVrikJointRotationCount)
        return false;

    ar_chunk.Joint = static_cast<std::uint8_t>(rawJoint);
    ar_chunk.Sequence = static_cast<std::uint32_t>(a_payload.ValueA);
    ar_chunk.RootGeneration = static_cast<std::uint32_t>(a_payload.ValueB);
    ar_chunk.X = a_payload.ScalarA;
    ar_chunk.Y = a_payload.ScalarB;
    ar_chunk.Z = a_payload.ScalarC;
    ar_chunk.W = a_payload.ScalarD;
    return ar_chunk.RootGeneration != 0 && IsUnitQuaternion(ar_chunk.X, ar_chunk.Y, ar_chunk.Z, ar_chunk.W) &&
           IsOrthonormal(QuaternionToMatrix(ar_chunk.X, ar_chunk.Y, ar_chunk.Z, ar_chunk.W));
}

[[nodiscard]] bool DecodeJointRotationCommit(
    const GameplayActionPayload& a_payload,
    std::uint32_t& ar_sequence,
    std::uint32_t& ar_rootGeneration,
    std::uint32_t& ar_expectedJointMask) noexcept
{
    if (!HasNoPoseForms(a_payload) || a_payload.ValueA == 0 || a_payload.ValueB == 0 ||
        (a_payload.ActionFlags & ~kVrikJointCommitMask) != 0 || a_payload.ActionFlags == 0 ||
        a_payload.ScalarA != 0.0F || a_payload.ScalarB != 0.0F ||
        a_payload.ScalarC != 0.0F || a_payload.ScalarD != 0.0F)
        return false;

    ar_sequence = static_cast<std::uint32_t>(a_payload.ValueA);
    ar_rootGeneration = static_cast<std::uint32_t>(a_payload.ValueB);
    ar_expectedJointMask = a_payload.ActionFlags;
    return true;
}

[[nodiscard]] bool IsVrikFingerPayload(const GameplayActionPayload& a_payload) noexcept
{
    constexpr std::uint32_t kBothFingerSetsPresent = (1u << 0) | (1u << 1);
    return HasNoPoseForms(a_payload) && a_payload.ValueA == 0 && a_payload.ValueB == 0 &&
           a_payload.ActionFlags == kBothFingerSetsPresent && a_payload.ScalarA >= 0.0f &&
           a_payload.ScalarA <= 1.0f && a_payload.ScalarB >= 0.0f && a_payload.ScalarB <= 1.0f &&
           a_payload.ScalarC >= 0.0f && a_payload.ScalarC <= 1.0f && a_payload.ScalarD >= 0.0f &&
           a_payload.ScalarD <= 1.0f;
}

[[nodiscard]] const char* SkeletonNodeName(const PoseNode a_node) noexcept
{
    switch (a_node) {
    case PoseNode::Hmd:
        // Remote NPCs have no PlayerCharacter UprightHmdNode.  The established
        // humanoid skeleton head node is the rendering endpoint, not a VRIK
        // camera offset or a fabricated tracker node.
        return "NPC Head [Head]";
    case PoseNode::LeftHand:
        return "NPC L Hand [LHnd]";
    case PoseNode::RightHand:
        return "NPC R Hand [RHnd]";
    case PoseNode::Pelvis:
        return "NPC Pelvis [Pelv]";
    case PoseNode::Spine0:
        return "NPC Spine [Spn0]";
    case PoseNode::Spine1:
        return "NPC Spine1 [Spn1]";
    case PoseNode::Spine2:
        return "NPC Spine2 [Spn2]";
    case PoseNode::Neck:
        return "NPC Neck [Neck]";
    case PoseNode::LeftClavicle:
        return "NPC L Clavicle [LClv]";
    case PoseNode::LeftUpperArm:
        return "NPC L UpperArm [LUar]";
    case PoseNode::LeftForearm:
        return "NPC L Forearm [LLar]";
    case PoseNode::RightClavicle:
        return "NPC R Clavicle [RClv]";
    case PoseNode::RightUpperArm:
        return "NPC R UpperArm [RUar]";
    case PoseNode::RightForearm:
        return "NPC R Forearm [RLar]";
    case PoseNode::LeftThigh:
        return "NPC L Thigh [LThg]";
    case PoseNode::LeftCalf:
        return "NPC L Calf [LClf]";
    case PoseNode::LeftFoot:
        return "NPC L Foot [Lft ]";
    case PoseNode::RightThigh:
        return "NPC R Thigh [RThg]";
    case PoseNode::RightCalf:
        return "NPC R Calf [RClf]";
    case PoseNode::RightFoot:
        return "NPC R Foot [Rft ]";
    default:
        return nullptr;
    }
}

[[nodiscard]] const char* JointNodeName(const std::uint8_t a_joint) noexcept
{
    constexpr std::array<const char*, kVrikJointRotationCount> kJointNames{
        "NPC L Finger11 [LF11]", "NPC L Finger12 [LF12]", "NPC L Finger13 [LF13]",
        "NPC L Finger21 [LF21]", "NPC L Finger22 [LF22]", "NPC L Finger23 [LF23]",
        "NPC L Finger31 [LF31]", "NPC L Finger32 [LF32]", "NPC L Finger33 [LF33]",
        "NPC L Finger41 [LF41]", "NPC L Finger42 [LF42]", "NPC L Finger43 [LF43]",
        "NPC L Finger51 [LF51]", "NPC L Finger52 [LF52]", "NPC L Finger53 [LF53]",
        "NPC R Finger11 [RF11]", "NPC R Finger12 [RF12]", "NPC R Finger13 [RF13]",
        "NPC R Finger21 [RF21]", "NPC R Finger22 [RF22]", "NPC R Finger23 [RF23]",
        "NPC R Finger31 [RF31]", "NPC R Finger32 [RF32]", "NPC R Finger33 [RF33]",
        "NPC R Finger41 [RF41]", "NPC R Finger42 [RF42]", "NPC R Finger43 [RF43]",
        "NPC R Finger51 [RF51]", "NPC R Finger52 [RF52]", "NPC R Finger53 [RF53]",
    };
    return a_joint < kJointNames.size() ? kJointNames[a_joint] : nullptr;
}

[[nodiscard]] bool IsPhysicsControlled(const RE::Actor& a_actor, RE::NiAVObject& a_node,
                                       const RE::NiAVObject& a_root) noexcept
{
    static_cast<void>(a_node);
    static_cast<void>(a_root);
    // Ordinary humanoid nodes and their ancestors commonly have collision
    // objects, so collision presence is not evidence that PLANCK owns this
    // pose. PLANCK-controlled full ragdolls are reflected by Actor state; only
    // that state suppresses network pose writes.
    return a_actor.IsInRagdollState();
}

[[nodiscard]] bool BuildLocalPose(
    const RE::NiAVObject& a_node,
    const PendingPose& a_pose,
    RE::NiTransform& ar_local) noexcept
{
    if (!IsSafeTransform(a_node.local, kMinimumLocalScale, kMaximumLocalScale))
        return false;

    ar_local = a_node.local;
    if (a_pose.Node < PoseNode::Pelvis) {
        auto* const parent = a_node.parent;
        if (!parent || !IsSafeTransform(parent->world, kMinimumParentScale, kMaximumParentScale))
            return false;
        const auto parentInverse = parent->world.Invert();
        ar_local.translate = parentInverse * a_pose.Position;
        ar_local.scale = a_pose.Scale / parent->world.scale;
        ar_local.rotate = parent->world.rotate.Transpose() * a_pose.Rotation;
    } else if (a_pose.Node == PoseNode::Pelvis) {
        // FBT publishes the pelvis translation and rotation in parent-local
        // space. Preserve the target skeleton's scale.
        ar_local.translate = a_pose.Position;
        ar_local.rotate = a_pose.Rotation;
    } else {
        // Limb translations and scales describe the source skeleton's bind
        // pose, not portable tracker data. Applying their zero/default values
        // collapses remote leg chains, so only the local rotation is owned by
        // replication.
        ar_local.rotate = a_pose.Rotation;
    }

    return IsBounded(ar_local.translate, kMaximumLocalPosition) && IsFinite(ar_local.scale) &&
           ar_local.scale >= kMinimumLocalScale && ar_local.scale <= kMaximumLocalScale &&
           IsOrthonormal(ar_local.rotate);
}

struct PoseApplication
{
    RE::NiPointer<RE::NiAVObject> Node{};
    RE::NiTransform Local{};
    RE::NiTransform OriginalLocal{};
};

class ScopedPoseRollback
{
public:
    ScopedPoseRollback(
        RE::NiAVObject& a_root,
        PoseApplication* a_bodyApplications,
        const std::size_t a_bodyApplicationCount,
        PoseApplication* a_endpointApplications,
        const std::size_t& a_endpointApplicationCount) noexcept :
        Root(a_root),
        BodyApplications(a_bodyApplications),
        BodyApplicationCount(a_bodyApplicationCount),
        EndpointApplications(a_endpointApplications),
        EndpointApplicationCount(a_endpointApplicationCount)
    {}

    ~ScopedPoseRollback() noexcept
    {
        if (!Active)
            return;

        try {
            RestoreLocals(BodyApplications, BodyApplicationCount);
            RestoreLocals(EndpointApplications, EndpointApplicationCount);

            RE::NiUpdateData update{};
            Root.UpdateDownwardPass(update, 0);
            Root.UpdateWorldBound();
        } catch (...) {
        }
    }

    void Release() noexcept { Active = false; }

private:
    static void RestoreLocals(PoseApplication* a_applications, const std::size_t a_applicationCount)
    {
        for (std::size_t index = 0; index < a_applicationCount; ++index)
            a_applications[index].Node->local = a_applications[index].OriginalLocal;
    }

    RE::NiAVObject& Root;
    PoseApplication* BodyApplications;
    std::size_t BodyApplicationCount;
    PoseApplication* EndpointApplications;
    const std::size_t& EndpointApplicationCount;
    bool Active{true};
};

[[nodiscard]] bool IsNewerSequence(const std::uint32_t a_candidate, const std::uint32_t a_current) noexcept
{
    return static_cast<std::int32_t>(a_candidate - a_current) > 0;
}

[[nodiscard]] bool ApplyCommittedPose(
    RE::Actor& a_actor,
    RE::NiAVObject& a_root,
    const PendingActorPose& a_pending) noexcept
{
    if (!a_pending.CommitReceived || a_pending.ExpectedNodeMask == 0 || a_actor.IsInRagdollState())
        return false;

    const auto rootAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_root));
    if (a_pending.RootGeneration != 0) {
        if (const auto known = s_appliedPoseRoots.find(a_pending.TargetHandle);
            known != s_appliedPoseRoots.end() && known->second.Address != rootAddress &&
            known->second.RootGeneration != a_pending.RootGeneration &&
            !IsNewerSequence(a_pending.RootGeneration, known->second.RootGeneration))
            return false;
    }

    std::array<PoseApplication, static_cast<std::size_t>(PoseNode::Count)> bodyApplications{};
    std::size_t bodyApplicationCount{};
    for (std::size_t index = static_cast<std::size_t>(PoseNode::Pelvis); index < a_pending.Nodes.size(); ++index) {
        const auto nodeBit = 1u << static_cast<std::uint32_t>(index);
        if ((a_pending.ExpectedNodeMask & nodeBit) == 0)
            continue;

        const auto& pose = a_pending.Nodes[index];
        if (!pose.Complete() || pose.Sequence != a_pending.Sequence ||
            pose.RootGeneration != a_pending.RootGeneration)
            return false;

        const auto* const name = SkeletonNodeName(pose.Node);
        if (!name)
            return false;
        auto& application = bodyApplications[bodyApplicationCount];
        application.Node.reset(a_root.GetObjectByName(RE::BSFixedString(name)));
        if (!application.Node || IsPhysicsControlled(a_actor, *application.Node, a_root) ||
            !BuildLocalPose(*application.Node, pose, application.Local))
            return false;
        application.OriginalLocal = application.Node->local;
        ++bodyApplicationCount;
    }

    // Body pose nodes are parent-first in GameplayPoseNode. Apply and
    // propagate this complete local chain before resolving source-world HMD
    // and hand endpoints through their now-updated remote parents.
    std::array<PoseApplication, static_cast<std::size_t>(PoseNode::Pelvis)> endpointApplications{};
    std::size_t endpointApplicationCount{};

    ScopedPoseRollback rollback{a_root, bodyApplications.data(), bodyApplicationCount,
                                endpointApplications.data(), endpointApplicationCount};
    try {
        for (std::size_t index = 0; index < bodyApplicationCount; ++index)
            bodyApplications[index].Node->local = bodyApplications[index].Local;

        RE::NiUpdateData update{};
        a_root.UpdateDownwardPass(update, 0);

        for (std::size_t index = 0; index < static_cast<std::size_t>(PoseNode::Pelvis); ++index) {
            const auto nodeBit = 1u << static_cast<std::uint32_t>(index);
            if ((a_pending.ExpectedNodeMask & nodeBit) == 0)
                continue;

            const auto& pose = a_pending.Nodes[index];
            if (!pose.Complete() || pose.Sequence != a_pending.Sequence ||
                pose.RootGeneration != a_pending.RootGeneration)
                return false;

            const auto* const name = SkeletonNodeName(pose.Node);
            if (!name)
                return false;
            auto& application = endpointApplications[endpointApplicationCount];
            application.Node.reset(a_root.GetObjectByName(RE::BSFixedString(name)));
            if (!application.Node || IsPhysicsControlled(a_actor, *application.Node, a_root) ||
                !BuildLocalPose(*application.Node, pose, application.Local))
                return false;
            application.OriginalLocal = application.Node->local;
            ++endpointApplicationCount;
        }

        if (bodyApplicationCount == 0 && endpointApplicationCount == 0)
            return false;
        for (std::size_t index = 0; index < endpointApplicationCount; ++index)
            endpointApplications[index].Node->local = endpointApplications[index].Local;

        a_root.UpdateDownwardPass(update, 0);
        a_root.UpdateWorldBound();
        if (a_pending.RootGeneration != 0) {
            if (!s_appliedPoseRoots.contains(a_pending.TargetHandle) &&
                s_appliedPoseRoots.size() >= kMaximumPendingActors)
                s_appliedPoseRoots.erase(s_appliedPoseRoots.begin());
            s_appliedPoseRoots[a_pending.TargetHandle] = {rootAddress, a_pending.RootGeneration};
        }
        rollback.Release();
    } catch (...) {
        return false;
    }
    return true;
}

[[nodiscard]] PendingActorPose* GetPendingActor(
    const std::uint64_t a_handle, RE::Actor& a_actor)
{
    if (const auto existing = s_pendingPoses.find(a_handle); existing != s_pendingPoses.end()) {
        existing->second.Actor = RE::ActorHandle{std::addressof(a_actor)};
        existing->second.TargetHandle = a_handle;
        return std::addressof(existing->second);
    }
    if (s_pendingPoses.size() >= kMaximumPendingActors)
        return nullptr;

    auto [it, inserted] = s_pendingPoses.try_emplace(a_handle);
    if (!inserted)
        return nullptr;
    it->second.Actor = RE::ActorHandle{std::addressof(a_actor)};
    it->second.TargetHandle = a_handle;
    return std::addressof(it->second);
}

[[nodiscard]] bool PreparePendingFrame(
    PendingActorPose& ar_pending,
    const std::uint32_t a_sequence,
    const std::uint32_t a_rootGeneration) noexcept
{
    if (ar_pending.Sequence != 0 && ar_pending.Sequence != a_sequence &&
        !IsNewerSequence(a_sequence, ar_pending.Sequence))
        return false;

    if (ar_pending.Sequence != a_sequence) {
        ar_pending.Nodes = {};
        ar_pending.Sequence = a_sequence;
        ar_pending.RootGeneration = a_rootGeneration;
        ar_pending.ExpectedNodeMask = 0;
        ar_pending.CommitReceived = false;
        ar_pending.CacheAdmitted = false;
    } else if (ar_pending.RootGeneration != a_rootGeneration) {
        return false;
    }
    ar_pending.Attempts = 0;
    return true;
}

[[nodiscard]] bool MergePoseChunk(PendingActorPose& ar_pending, const DecodedPoseChunk& a_chunk) noexcept
{
    if (!PreparePendingFrame(ar_pending, a_chunk.Sequence, a_chunk.RootGeneration))
        return false;

    auto& pose = ar_pending.Nodes[static_cast<std::size_t>(a_chunk.Node)];
    if (pose.Sequence == 0) {
        pose.Node = a_chunk.Node;
        pose.Sequence = a_chunk.Sequence;
        pose.RootGeneration = a_chunk.RootGeneration;
    } else if (pose.Sequence != a_chunk.Sequence || pose.RootGeneration != a_chunk.RootGeneration) {
        return false;
    }

    if (!a_chunk.Basis) {
        pose.Position = a_chunk.Vector;
        pose.Scale = a_chunk.Scale;
        pose.PositionValid = true;
        return true;
    }

    for (std::size_t component = 0; component < 3; ++component)
        pose.Rotation.entry[a_chunk.Axis][component] = (&a_chunk.Vector.x)[component];
    pose.BasisMask = static_cast<std::uint8_t>(pose.BasisMask | (1u << a_chunk.Axis));
    return true;
}

[[nodiscard]] bool IsCommittedFrameComplete(const PendingActorPose& a_pending) noexcept
{
    if (!a_pending.CommitReceived || a_pending.ExpectedNodeMask == 0)
        return false;
    for (std::size_t index = 0; index < a_pending.Nodes.size(); ++index) {
        if ((a_pending.ExpectedNodeMask & (1u << static_cast<std::uint32_t>(index))) == 0)
            continue;
        const auto& pose = a_pending.Nodes[index];
        if (!pose.Complete() || pose.Sequence != a_pending.Sequence ||
            pose.RootGeneration != a_pending.RootGeneration)
            return false;
    }
    return true;
}

[[nodiscard]] RemoteSolvedPosePresentation::Quaternion MatrixToQuaternion(
    const RE::NiMatrix3& a_matrix) noexcept
{
    const auto trace = a_matrix.entry[0][0] + a_matrix.entry[1][1] + a_matrix.entry[2][2];
    RemoteSolvedPosePresentation::Quaternion result{};
    if (trace > 0.0F) {
        const auto scale = std::sqrt(trace + 1.0F) * 2.0F;
        result = {(a_matrix.entry[2][1] - a_matrix.entry[1][2]) / scale,
                  (a_matrix.entry[0][2] - a_matrix.entry[2][0]) / scale,
                  (a_matrix.entry[1][0] - a_matrix.entry[0][1]) / scale, 0.25F * scale};
    } else if (a_matrix.entry[0][0] > a_matrix.entry[1][1] && a_matrix.entry[0][0] > a_matrix.entry[2][2]) {
        const auto scale = std::sqrt(1.0F + a_matrix.entry[0][0] - a_matrix.entry[1][1] - a_matrix.entry[2][2]) * 2.0F;
        result = {0.25F * scale, (a_matrix.entry[0][1] + a_matrix.entry[1][0]) / scale,
                  (a_matrix.entry[0][2] + a_matrix.entry[2][0]) / scale, (a_matrix.entry[2][1] - a_matrix.entry[1][2]) / scale};
    } else if (a_matrix.entry[1][1] > a_matrix.entry[2][2]) {
        const auto scale = std::sqrt(1.0F + a_matrix.entry[1][1] - a_matrix.entry[0][0] - a_matrix.entry[2][2]) * 2.0F;
        result = {(a_matrix.entry[0][1] + a_matrix.entry[1][0]) / scale, 0.25F * scale,
                  (a_matrix.entry[1][2] + a_matrix.entry[2][1]) / scale, (a_matrix.entry[0][2] - a_matrix.entry[2][0]) / scale};
    } else {
        const auto scale = std::sqrt(1.0F + a_matrix.entry[2][2] - a_matrix.entry[0][0] - a_matrix.entry[1][1]) * 2.0F;
        result = {(a_matrix.entry[0][2] + a_matrix.entry[2][0]) / scale,
                  (a_matrix.entry[1][2] + a_matrix.entry[2][1]) / scale, 0.25F * scale,
                  (a_matrix.entry[1][0] - a_matrix.entry[0][1]) / scale};
    }
    return RemoteSolvedPosePresentation::Normalize(result);
}

[[nodiscard]] bool AdmitCommittedPose(
    const PendingActorPose& a_pending,
    const RE::Actor& a_actor,
    const RE::NiAVObject& a_root) noexcept
{
    if (!IsCommittedFrameComplete(a_pending) || a_pending.Identity.LifecycleEpoch == 0)
        return false;
    RemoteSolvedPosePresentation::Frame frame{};
    frame.ActorHandle = a_pending.TargetHandle;
    frame.ActorAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_actor));
    frame.RootAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_root));
    frame.ServerInstanceNonce = a_pending.Identity.ServerInstanceNonce;
    frame.ConnectionGeneration = a_pending.Identity.ConnectionGeneration;
    frame.LifecycleEpoch = a_pending.Identity.LifecycleEpoch;
    frame.RootGeneration = a_pending.RootGeneration;
    frame.Sequence = a_pending.Sequence;
    frame.AdmittedAtMilliseconds = GetTickCount64();
    frame.NodeMask = a_pending.ExpectedNodeMask;
    for (std::size_t index = 0; index < a_pending.Nodes.size(); ++index) {
        if ((frame.NodeMask & (1u << index)) == 0)
            continue;
        const auto& pose = a_pending.Nodes[index];
        // Do not let quaternion conversion normalize malformed/non-finite
        // source bases into an apparently safe identity rotation.
        if (!IsOrthonormal(pose.Rotation))
            return false;
        frame.Nodes[index] = {pose.Position.x, pose.Position.y, pose.Position.z,
                              MatrixToQuaternion(pose.Rotation), pose.Scale};
    }
    return RemoteSolvedPosePresentation::GetFrameCache().Admit(frame);
}

[[nodiscard]] bool PreparePendingJointFrame(
    PendingActorJointPose& ar_pending,
    const std::uint32_t a_sequence,
    const std::uint32_t a_rootGeneration) noexcept
{
    if (ar_pending.Sequence != 0 && ar_pending.Sequence != a_sequence &&
        !IsNewerSequence(a_sequence, ar_pending.Sequence))
        return false;

    if (ar_pending.Sequence != a_sequence) {
        ar_pending.Joints = {};
        ar_pending.Sequence = a_sequence;
        ar_pending.RootGeneration = a_rootGeneration;
        ar_pending.ExpectedJointMask = 0;
        ar_pending.CommitReceived = false;
        ar_pending.CacheAdmitted = false;
    } else if (ar_pending.RootGeneration != a_rootGeneration) {
        return false;
    }
    ar_pending.Attempts = 0;
    return true;
}

[[nodiscard]] PendingActorJointPose* GetPendingJointActor(
    const std::uint64_t a_handle,
    RE::Actor& a_actor)
{
    if (const auto existing = s_pendingJointPoses.find(a_handle); existing != s_pendingJointPoses.end()) {
        existing->second.Actor = RE::ActorHandle{std::addressof(a_actor)};
        return std::addressof(existing->second);
    }
    if (s_pendingJointPoses.size() >= kMaximumPendingActors)
        return nullptr;

    auto [it, inserted] = s_pendingJointPoses.try_emplace(a_handle);
    if (!inserted)
        return nullptr;
    it->second.Actor = RE::ActorHandle{std::addressof(a_actor)};
    it->second.TargetHandle = a_handle;
    return std::addressof(it->second);
}

[[nodiscard]] bool MergeJointRotationChunk(
    PendingActorJointPose& ar_pending,
    const DecodedJointRotationChunk& a_chunk) noexcept
{
    if (!PreparePendingJointFrame(ar_pending, a_chunk.Sequence, a_chunk.RootGeneration))
        return false;

    auto& joint = ar_pending.Joints[a_chunk.Joint];
    if (joint.Sequence == 0) {
        joint.Sequence = a_chunk.Sequence;
        joint.RootGeneration = a_chunk.RootGeneration;
    } else if (joint.Sequence != a_chunk.Sequence || joint.RootGeneration != a_chunk.RootGeneration) {
        return false;
    }

    joint.Rotation = QuaternionToMatrix(a_chunk.X, a_chunk.Y, a_chunk.Z, a_chunk.W);
    joint.RotationValid = IsOrthonormal(joint.Rotation);
    return true;
}

[[nodiscard]] bool IsCommittedJointFrameComplete(const PendingActorJointPose& a_pending) noexcept
{
    if (!a_pending.CommitReceived || a_pending.ExpectedJointMask == 0)
        return false;
    for (std::uint32_t index = 0; index < kVrikJointRotationCount; ++index) {
        if ((a_pending.ExpectedJointMask & (1u << index)) == 0)
            continue;
        const auto& joint = a_pending.Joints[index];
        if (!joint.Complete() || joint.Sequence != a_pending.Sequence ||
            joint.RootGeneration != a_pending.RootGeneration || !IsOrthonormal(joint.Rotation))
            return false;
    }
    return true;
}

[[nodiscard]] bool AdmitCommittedJointPose(
    const PendingActorJointPose& a_pending,
    const RE::Actor& a_actor,
    const RE::NiAVObject& a_root) noexcept
{
    if (!IsCommittedJointFrameComplete(a_pending) || a_pending.Identity.LifecycleEpoch == 0)
        return false;
    RemoteSolvedPosePresentation::Frame frame{};
    frame.ActorHandle = a_pending.TargetHandle;
    frame.ActorAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_actor));
    frame.RootAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_root));
    frame.ServerInstanceNonce = a_pending.Identity.ServerInstanceNonce;
    frame.ConnectionGeneration = a_pending.Identity.ConnectionGeneration;
    frame.LifecycleEpoch = a_pending.Identity.LifecycleEpoch;
    frame.RootGeneration = a_pending.RootGeneration;
    frame.Sequence = a_pending.Sequence;
    frame.AdmittedAtMilliseconds = GetTickCount64();
    frame.JointMask = a_pending.ExpectedJointMask;
    for (std::size_t index = 0; index < a_pending.Joints.size(); ++index) {
        if ((frame.JointMask & (1u << index)) != 0)
            frame.Joints[index] = MatrixToQuaternion(a_pending.Joints[index].Rotation);
    }
    return RemoteSolvedPosePresentation::GetFrameCache().Admit(frame);
}

[[nodiscard]] bool ApplyCommittedJointPose(
    RE::Actor& a_actor,
    RE::NiAVObject& a_root,
    const PendingActorJointPose& a_pending) noexcept
{
    try {
        if (!a_pending.CommitReceived || a_pending.ExpectedJointMask == 0 || a_actor.IsInRagdollState())
            return false;

        const auto rootAddress = reinterpret_cast<std::uintptr_t>(std::addressof(a_root));
        const auto known = s_appliedJointRoots.find(a_pending.TargetHandle);
        if (known != s_appliedJointRoots.end() && known->second.Address != rootAddress &&
            known->second.RootGeneration != a_pending.RootGeneration &&
            !IsNewerSequence(a_pending.RootGeneration, known->second.RootGeneration))
            return false;
        const bool hasPublishedRoot = known != s_appliedJointRoots.end();

        std::array<PoseApplication, kVrikJointRotationCount> applications{};
        std::size_t applicationCount{};
        for (std::uint32_t index = 0; index < kVrikJointRotationCount; ++index) {
            if ((a_pending.ExpectedJointMask & (1u << index)) == 0)
                continue;

            const auto& joint = a_pending.Joints[index];
            if (!joint.Complete() || joint.Sequence != a_pending.Sequence ||
                joint.RootGeneration != a_pending.RootGeneration || !IsOrthonormal(joint.Rotation))
                return false;

            const auto* const name = JointNodeName(static_cast<std::uint8_t>(index));
            if (!name)
                return false;
            auto& application = applications[applicationCount];
            application.Node.reset(a_root.GetObjectByName(RE::BSFixedString(name)));
            if (!application.Node || IsPhysicsControlled(a_actor, *application.Node, a_root) ||
                !IsSafeTransform(application.Node->local, kMinimumLocalScale, kMaximumLocalScale))
                return false;
            application.OriginalLocal = application.Node->local;
            application.Local = application.OriginalLocal;
            application.Local.rotate = joint.Rotation;
            if (!IsOrthonormal(application.Local.rotate))
                return false;
            ++applicationCount;
        }

        if (applicationCount == 0)
            return false;

        decltype(s_appliedJointRoots)::node_type preparedRoot{};
        if (!hasPublishedRoot) {
            s_appliedJointRoots.reserve(s_appliedJointRoots.size() + 1);
            decltype(s_appliedJointRoots) preparedRoots;
            preparedRoots.emplace(a_pending.TargetHandle, AppliedJointRoot{rootAddress, a_pending.RootGeneration});
            preparedRoot = preparedRoots.extract(a_pending.TargetHandle);
        }

        const std::size_t noEndpointApplications{};
        ScopedPoseRollback rollback{a_root, applications.data(), applicationCount, applications.data(),
                                    noEndpointApplications};
        for (std::size_t index = 0; index < applicationCount; ++index)
            applications[index].Node->local = applications[index].Local;

        RE::NiUpdateData update{};
        a_root.UpdateDownwardPass(update, 0);
        a_root.UpdateWorldBound();
        if (hasPublishedRoot) {
            s_appliedJointRoots[a_pending.TargetHandle] = {rootAddress, a_pending.RootGeneration};
        } else {
            const auto eviction = s_appliedJointRoots.size() >= kMaximumPendingActors ?
                                      s_appliedJointRoots.begin() :
                                      s_appliedJointRoots.end();
            const auto inserted = s_appliedJointRoots.insert(std::move(preparedRoot));
            if (!inserted.inserted)
                inserted.position->second = {rootAddress, a_pending.RootGeneration};
            if (eviction != s_appliedJointRoots.end())
                s_appliedJointRoots.erase(eviction);
        }
        rollback.Release();
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool ApplyLegacyGraphFloats(RE::Actor& a_actor, const GameplayActionPayload& a_payload,
                                          const std::array<std::string_view, 4>& a_names) noexcept
{
    if (a_actor.IsInRagdollState())
        return false;

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!a_actor.GetAnimationGraphManager(manager) || !manager)
        return false;

    const std::array<float, 4> values{a_payload.ScalarA, a_payload.ScalarB, a_payload.ScalarC, a_payload.ScalarD};
    std::array<float, 4> previous{};
    for (std::size_t index = 0; index < a_names.size(); ++index) {
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(a_names[index].data()), previous[index]))
            return false;
    }

    std::size_t written{};
    for (; written < a_names.size(); ++written) {
        if (!a_actor.SetGraphVariableFloat(RE::BSFixedString(a_names[written].data()), values[written])) {
            for (std::size_t rollback = 0; rollback < written; ++rollback)
                a_actor.SetGraphVariableFloat(RE::BSFixedString(a_names[rollback].data()), previous[rollback]);
            return false;
        }
    }
    return true;
}
} // namespace

CommandStatus VRBodyPoseManager::Execute(const CommandRecord& a_command) noexcept
{
    try {
        if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayAction)
            return CommandStatus::Malformed;

        const auto& payload = a_command.Payload.ApplyGameplayAction;
        const auto action = static_cast<GameplayAction>(payload.Action);
        if (a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0 ||
            payload.Reserved0 != 0 || !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
            !std::isfinite(payload.ScalarC) || !std::isfinite(payload.ScalarD) ||
            static_cast<GameplayDomain>(payload.Domain) != GameplayDomain::VrBodyPose ||
            !IsActionInDomain(GameplayDomain::VrBodyPose, action) || payload.TargetHandle.Value == 0)
            return CommandStatus::Malformed;
        for (const auto value : payload.ReservedTail) {
            if (value != 0)
                return CommandStatus::Malformed;
        }

        DecodedPoseChunk poseChunk{};
        DecodedJointRotationChunk jointChunk{};
        std::uint32_t commitSequence{};
        std::uint32_t commitRootGeneration{};
        std::uint32_t commitNodeMask{};
        std::uint32_t jointCommitSequence{};
        std::uint32_t jointCommitRootGeneration{};
        std::uint32_t jointCommitMask{};
        const bool isPoseChunk = action == GameplayAction::VrPoseChunk;
        const bool isPhysicalPose = isPoseChunk && DecodePoseChunk(payload, poseChunk);
        const bool isPoseCommit = action == GameplayAction::VrPoseCommit;
        const bool isPhysicalCommit = isPoseCommit &&
            DecodePoseCommit(payload, commitSequence, commitRootGeneration, commitNodeMask);
        const bool isJointRotation = action == GameplayAction::VrJointRotationChunk &&
            DecodeJointRotationChunk(payload, jointChunk);
        const bool isJointCommit = action == GameplayAction::VrJointRotationCommit &&
            DecodeJointRotationCommit(payload, jointCommitSequence, jointCommitRootGeneration, jointCommitMask);
        const bool isLegacyPose = isPoseChunk && IsLegacyGraphPayload(payload, 6.28318530717958647692f);
        const bool isLegacyCalibration = action == GameplayAction::VrCalibration && IsLegacyGraphPayload(payload, 10000.0f);
        const bool isVrikFingerPayload = action == GameplayAction::VrCalibration && IsVrikFingerPayload(payload);
        if (!isPhysicalPose && !isPhysicalCommit && !isJointRotation && !isJointCommit && !isLegacyPose &&
            !isLegacyCalibration && !isVrikFingerPayload) {
            // Current VrCalibration carries VRIK finger samples.  VRIK exposes
            // no remote-actor application API, so accepting it as a graph or
            // local-player call would be both unsafe and semantically wrong.
            return CommandStatus::Malformed;
        }

        RE::NiPointer<RE::Actor> actor;
        const auto actorStatus = AvatarManager::Get().ResolveGameplayActor(a_command, actor);
        if (actorStatus != CommandStatus::Success)
            return actorStatus;
        if ((isPhysicalPose || isPhysicalCommit || isJointRotation || isJointCommit) &&
            !AvatarManager::Get().IsManagedRemoteActor(actor.get()))
            return CommandStatus::InvalidHandle;

        if (isLegacyPose) {
            constexpr std::array<std::string_view, 4> kLegacyPoseVariables{
                "Pitch", "PitchOffset", "1stPRot", "1stPRotDamped"};
            return ApplyLegacyGraphFloats(*actor, payload, kLegacyPoseVariables) ? CommandStatus::Success :
                                                                                     CommandStatus::EngineRejected;
        }
        if (isLegacyCalibration) {
            constexpr std::array<std::string_view, 4> kLegacyCalibrationVariables{
                "TurnDelta", "Direction", "SpeedSampled", "SpeedDamped"};
            return ApplyLegacyGraphFloats(*actor, payload, kLegacyCalibrationVariables) ? CommandStatus::Success :
                                                                                            CommandStatus::EngineRejected;
        }
        if (isVrikFingerPayload)
            return CommandStatus::Unsupported;

        if (actor->IsInRagdollState()) {
            s_pendingPoses.erase(payload.TargetHandle.Value);
            s_pendingJointPoses.erase(payload.TargetHandle.Value);
            s_appliedPoseRoots.erase(payload.TargetHandle.Value);
            s_appliedJointRoots.erase(payload.TargetHandle.Value);
            return CommandStatus::Unsupported;
        }

        if (isJointRotation || isJointCommit) {
            auto* pendingJoint = GetPendingJointActor(payload.TargetHandle.Value, *actor);
            if (!pendingJoint)
                return CommandStatus::QueueOverflow;
            if (isJointRotation)
                return MergeJointRotationChunk(*pendingJoint, jointChunk) ? CommandStatus::Success :
                                                                           CommandStatus::StaleEntity;

            if (!PreparePendingJointFrame(*pendingJoint, jointCommitSequence, jointCommitRootGeneration))
                return CommandStatus::StaleEntity;
            pendingJoint->ExpectedJointMask = jointCommitMask;
            pendingJoint->CommitReceived = true;
            pendingJoint->Identity = a_command.Header.Identity;
            if (!IsCommittedJointFrameComplete(*pendingJoint))
                return CommandStatus::Success;

            RE::NiPointer<RE::NiAVObject> root{actor->Get3D()};
            if (!root)
                return CommandStatus::Success;
            if (!pendingJoint->CacheAdmitted)
                pendingJoint->CacheAdmitted = AdmitCommittedJointPose(*pendingJoint, *actor, *root);
            if (ApplyCommittedJointPose(*actor, *root, *pendingJoint))
                s_pendingJointPoses.erase(payload.TargetHandle.Value);
            return CommandStatus::Success;
        }

        auto* pending = GetPendingActor(payload.TargetHandle.Value, *actor);
        if (!pending)
            return CommandStatus::QueueOverflow;

        if (isPhysicalPose) {
            if (!MergePoseChunk(*pending, poseChunk))
                return CommandStatus::StaleEntity;
            return CommandStatus::Success;
        }

        if (!PreparePendingFrame(*pending, commitSequence, commitRootGeneration))
            return CommandStatus::StaleEntity;
        pending->ExpectedNodeMask = commitNodeMask;
        pending->CommitReceived = true;
        pending->Identity = a_command.Header.Identity;
        if (!IsCommittedFrameComplete(*pending))
            return CommandStatus::Success;

        RE::NiPointer<RE::NiAVObject> root{actor->Get3D()};
        if (!root)
            return CommandStatus::Success;

        if (!pending->CacheAdmitted)
            pending->CacheAdmitted = AdmitCommittedPose(*pending, *actor, *root);
        if (ApplyCommittedPose(*actor, *root, *pending))
            s_pendingPoses.erase(payload.TargetHandle.Value);
        return CommandStatus::Success;
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

void VRBodyPoseManager::ProcessPending() noexcept
{
    try {
        RemoteSolvedPosePresentation::ProcessDiagnostics();
        for (auto it = s_pendingPoses.begin(); it != s_pendingPoses.end();) {
            auto actor = it->second.Actor.get();
            if (!actor || !AvatarManager::Get().IsManagedRemoteActor(actor.get()) ||
                ++it->second.Attempts >= kMaximumPendingAttempts) {
                s_appliedPoseRoots.erase(it->first);
                it = s_pendingPoses.erase(it);
                continue;
            }
            if (actor->IsInRagdollState()) {
                s_appliedPoseRoots.erase(it->first);
                it = s_pendingPoses.erase(it);
                continue;
            }
            if (!IsCommittedFrameComplete(it->second)) {
                ++it;
                continue;
            }
            RE::NiPointer<RE::NiAVObject> root{actor->Get3D()};
            if (!root) {
                ++it;
                continue;
            }
            if (!it->second.CacheAdmitted)
                it->second.CacheAdmitted = AdmitCommittedPose(it->second, *actor, *root);
            it = ApplyCommittedPose(*actor, *root, it->second) ? s_pendingPoses.erase(it) : std::next(it);
        }
        for (auto it = s_pendingJointPoses.begin(); it != s_pendingJointPoses.end();) {
            auto actor = it->second.Actor.get();
            if (!actor || !AvatarManager::Get().IsManagedRemoteActor(actor.get()) ||
                ++it->second.Attempts >= kMaximumPendingAttempts) {
                s_appliedJointRoots.erase(it->first);
                it = s_pendingJointPoses.erase(it);
                continue;
            }
            if (actor->IsInRagdollState()) {
                s_appliedJointRoots.erase(it->first);
                it = s_pendingJointPoses.erase(it);
                continue;
            }
            if (!IsCommittedJointFrameComplete(it->second)) {
                ++it;
                continue;
            }
            RE::NiPointer<RE::NiAVObject> root{actor->Get3D()};
            if (!root) {
                ++it;
                continue;
            }
            if (!it->second.CacheAdmitted)
                it->second.CacheAdmitted = AdmitCommittedJointPose(it->second, *actor, *root);
            if (ApplyCommittedJointPose(*actor, *root, it->second))
                it = s_pendingJointPoses.erase(it);
            else
                ++it;
        }
    } catch (...) {
    }
}

void VRBodyPoseManager::Reset() noexcept
{
    s_pendingPoses.clear();
    s_pendingJointPoses.clear();
    s_appliedPoseRoots.clear();
    s_appliedJointRoots.clear();
    RemoteSolvedPosePresentation::GetFrameCache().Reset();
}
} // namespace SkyrimTogetherVR::GameplayAdapter
