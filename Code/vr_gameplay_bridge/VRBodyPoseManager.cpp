#include "VRBodyPoseManager.h"

#include "AvatarManager.h"

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
    PendingNodes Nodes{};
    std::uint32_t Sequence{};
    std::uint32_t RootGeneration{};
    std::uint32_t ExpectedNodeMask{};
    std::uint16_t Attempts{};
    bool CommitReceived{};
};
std::unordered_map<std::uint64_t, PendingActorPose> s_pendingPoses;

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
};

[[nodiscard]] bool ApplyCommittedPose(
    RE::Actor& a_actor,
    RE::NiAVObject& a_root,
    const PendingActorPose& a_pending) noexcept
{
    if (!a_pending.CommitReceived || a_pending.ExpectedNodeMask == 0 || a_actor.IsInRagdollState())
        return false;

    std::array<PoseApplication, static_cast<std::size_t>(PoseNode::Count)> applications{};
    std::size_t applicationCount{};
    for (std::size_t index = 0; index < a_pending.Nodes.size(); ++index) {
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
        auto& application = applications[applicationCount];
        application.Node = a_root.GetObjectByName(RE::BSFixedString(name));
        if (!application.Node || IsPhysicsControlled(a_actor, *application.Node, a_root) ||
            !BuildLocalPose(*application.Node, pose, application.Local))
            return false;
        ++applicationCount;
    }

    if (applicationCount == 0)
        return false;
    for (std::size_t index = 0; index < applicationCount; ++index)
        applications[index].Node->local = applications[index].Local;

    RE::NiUpdateData update{};
    a_root.UpdateDownwardPass(update, 0);
    a_root.UpdateWorldBound();
    return true;
}

[[nodiscard]] bool IsNewerSequence(const std::uint32_t a_candidate, const std::uint32_t a_current) noexcept
{
    return static_cast<std::int32_t>(a_candidate - a_current) > 0;
}

[[nodiscard]] PendingActorPose* GetPendingActor(
    const std::uint64_t a_handle, RE::Actor& a_actor)
{
    if (const auto existing = s_pendingPoses.find(a_handle); existing != s_pendingPoses.end()) {
        existing->second.Actor = RE::ActorHandle{std::addressof(a_actor)};
        return std::addressof(existing->second);
    }
    if (s_pendingPoses.size() >= kMaximumPendingActors)
        return nullptr;

    auto [it, inserted] = s_pendingPoses.try_emplace(a_handle);
    if (!inserted)
        return nullptr;
    it->second.Actor = RE::ActorHandle{std::addressof(a_actor)};
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
        std::uint32_t commitSequence{};
        std::uint32_t commitRootGeneration{};
        std::uint32_t commitNodeMask{};
        const bool isPoseChunk = action == GameplayAction::VrPoseChunk;
        const bool isPhysicalPose = isPoseChunk && DecodePoseChunk(payload, poseChunk);
        const bool isPoseCommit = action == GameplayAction::VrPoseCommit;
        const bool isPhysicalCommit = isPoseCommit &&
            DecodePoseCommit(payload, commitSequence, commitRootGeneration, commitNodeMask);
        const bool isLegacyPose = isPoseChunk && IsLegacyGraphPayload(payload, 6.28318530717958647692f);
        const bool isLegacyCalibration = action == GameplayAction::VrCalibration && IsLegacyGraphPayload(payload, 10000.0f);
        const bool isVrikFingerPayload = action == GameplayAction::VrCalibration && IsVrikFingerPayload(payload);
        if (!isPhysicalPose && !isPhysicalCommit && !isLegacyPose && !isLegacyCalibration && !isVrikFingerPayload) {
            // Current VrCalibration carries VRIK finger samples.  VRIK exposes
            // no remote-actor application API, so accepting it as a graph or
            // local-player call would be both unsafe and semantically wrong.
            return CommandStatus::Malformed;
        }

        RE::NiPointer<RE::Actor> actor;
        const auto actorStatus = AvatarManager::Get().ResolveGameplayActor(a_command, actor);
        if (actorStatus != CommandStatus::Success)
            return actorStatus;

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
            return CommandStatus::Unsupported;
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
        if (!IsCommittedFrameComplete(*pending))
            return CommandStatus::Success;

        RE::NiPointer<RE::NiAVObject> root{actor->Get3D()};
        if (!root)
            return CommandStatus::Success;

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
        for (auto it = s_pendingPoses.begin(); it != s_pendingPoses.end();) {
            auto actor = it->second.Actor.get();
            if (!actor || ++it->second.Attempts >= kMaximumPendingAttempts) {
                it = s_pendingPoses.erase(it);
                continue;
            }
            if (actor->IsInRagdollState()) {
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
            it = ApplyCommittedPose(*actor, *root, it->second) ? s_pendingPoses.erase(it) : std::next(it);
        }
    } catch (...) {
    }
}

void VRBodyPoseManager::Reset() noexcept
{
    s_pendingPoses.clear();
}
} // namespace SkyrimTogetherVR::GameplayAdapter
