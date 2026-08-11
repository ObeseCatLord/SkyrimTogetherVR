#include <VR/VRBodyPoseCapture.h>

#include <Windows.h>

#include <array>
#include <cmath>

#include <Misc/BSFixedString.h>
#include <NetImmerse/NiAVObject.h>
#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>

namespace SkyrimTogetherVR::BodyPoseCapture
{
namespace
{
constexpr std::uint32_t kBodyPoseFormatVersion = 3;
constexpr std::uint32_t kBodyJointPoseFormatVersion = 1;

enum class BodyNode : std::size_t
{
    Pelvis,
    Spine0,
    Spine1,
    Spine2,
    Neck,
    LeftClavicle,
    LeftUpperArm,
    LeftForearm,
    RightClavicle,
    RightUpperArm,
    RightForearm,
    LeftThigh,
    LeftCalf,
    LeftFoot,
    RightThigh,
    RightCalf,
    RightFoot,
    Count,
};

SRWLOCK s_mailboxLock = SRWLOCK_INIT;
VRBodyPoseData s_latestPose{};
std::uint64_t s_latestCaptureTimeMilliseconds{0};
std::array<BSFixedString, static_cast<std::size_t>(BodyNode::Count)> s_nodeNames{};
std::array<BSFixedString, kVRBodyJointRotationCount> s_jointNodeNames{};
std::uintptr_t s_lastRootAddress{0};
std::uint32_t s_rootGeneration{0};
std::uint32_t s_captureSequence{0};
volatile LONG s_active{0};

VRBodyPoseData InvalidBodyPose() noexcept
{
    VRBodyPoseData result{};
    result.FormatVersion = kBodyPoseFormatVersion;
    return result;
}

void CopyLocalRotation(const NiAVObject& acNode, VRPoseNodeData& aOut) noexcept
{
    const auto& local = acNode.local;
    aOut = {};
    aOut.Valid = true;
    aOut.AxisX = {local.rotate[0][0], local.rotate[0][1], local.rotate[0][2]};
    aOut.AxisY = {local.rotate[1][0], local.rotate[1][1], local.rotate[1][2]};
    aOut.AxisZ = {local.rotate[2][0], local.rotate[2][1], local.rotate[2][2]};
    aOut.Scale = 1.0f;
}

// SkyrimVR FBT's waist bridge can correct the spine world transforms without
// touching their locals. Capture the effective local rotation from the final
// post-VRIK/post-HIGGS worlds so that correction survives replication.
bool CopyEffectiveParentLocalRotation(const NiAVObject& acNode, VRPoseNodeData& aOut) noexcept
{
    if (!SkyrimTogetherVR::IsReadableVrMemory(acNode.parent, sizeof(void*)))
        return false;

    const auto& parentWorld = acNode.parent->world;
    const auto& world = acNode.world;
    float local[3][3]{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t inner = 0; inner < 3; ++inner)
                local[row][column] += parentWorld.rotate[inner][row] * world.rotate[inner][column];
        }
    }

    aOut = {};
    aOut.Valid = true;
    aOut.AxisX = {local[0][0], local[0][1], local[0][2]};
    aOut.AxisY = {local[1][0], local[1][1], local[1][2]};
    aOut.AxisZ = {local[2][0], local[2][1], local[2][2]};
    aOut.Scale = 1.0f;
    return true;
}

void CopyLocalRotation(const NiAVObject& acNode, VRBodyJointRotationData& aOut) noexcept
{
    const auto& local = acNode.local;
    const auto m00 = local.rotate[0][0];
    const auto m01 = local.rotate[0][1];
    const auto m02 = local.rotate[0][2];
    const auto m10 = local.rotate[1][0];
    const auto m11 = local.rotate[1][1];
    const auto m12 = local.rotate[1][2];
    const auto m20 = local.rotate[2][0];
    const auto m21 = local.rotate[2][1];
    const auto m22 = local.rotate[2][2];
    const auto trace = m00 + m11 + m22;

    if (trace > 0.0f) {
        const auto scale = std::sqrt(trace + 1.0f) * 2.0f;
        aOut.W = 0.25f * scale;
        aOut.X = (m21 - m12) / scale;
        aOut.Y = (m02 - m20) / scale;
        aOut.Z = (m10 - m01) / scale;
    } else if (m00 > m11 && m00 > m22) {
        const auto scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        aOut.W = (m21 - m12) / scale;
        aOut.X = 0.25f * scale;
        aOut.Y = (m01 + m10) / scale;
        aOut.Z = (m02 + m20) / scale;
    } else if (m11 > m22) {
        const auto scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        aOut.W = (m02 - m20) / scale;
        aOut.X = (m01 + m10) / scale;
        aOut.Y = 0.25f * scale;
        aOut.Z = (m12 + m21) / scale;
    } else {
        const auto scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        aOut.W = (m10 - m01) / scale;
        aOut.X = (m02 + m20) / scale;
        aOut.Y = (m12 + m21) / scale;
        aOut.Z = 0.25f * scale;
    }
}

bool ResolveBodyNodes(NiAVObject* apRoot, std::array<NiAVObject*, static_cast<std::size_t>(BodyNode::Count)>& aNodes) noexcept
{
    if (!SkyrimTogetherVR::IsReadableVrMemory(apRoot, sizeof(void*)))
        return false;

    for (std::size_t index = 0; index < aNodes.size(); ++index)
    {
        aNodes[index] = apRoot->GetObjectByName(s_nodeNames[index]);
        if (!SkyrimTogetherVR::IsReadableVrMemory(aNodes[index], sizeof(void*)))
            return false;
    }

    auto* const pPelvis = aNodes[static_cast<std::size_t>(BodyNode::Pelvis)];
    auto* const pSpine0 = aNodes[static_cast<std::size_t>(BodyNode::Spine0)];
    auto* const pSpine1 = aNodes[static_cast<std::size_t>(BodyNode::Spine1)];
    auto* const pSpine2 = aNodes[static_cast<std::size_t>(BodyNode::Spine2)];
    auto* const pNeck = aNodes[static_cast<std::size_t>(BodyNode::Neck)];
    auto* const pLeftClavicle = aNodes[static_cast<std::size_t>(BodyNode::LeftClavicle)];
    auto* const pLeftUpperArm = aNodes[static_cast<std::size_t>(BodyNode::LeftUpperArm)];
    auto* const pLeftForearm = aNodes[static_cast<std::size_t>(BodyNode::LeftForearm)];
    auto* const pRightClavicle = aNodes[static_cast<std::size_t>(BodyNode::RightClavicle)];
    auto* const pRightUpperArm = aNodes[static_cast<std::size_t>(BodyNode::RightUpperArm)];
    auto* const pRightForearm = aNodes[static_cast<std::size_t>(BodyNode::RightForearm)];
    auto* const pLeftThigh = aNodes[static_cast<std::size_t>(BodyNode::LeftThigh)];
    auto* const pLeftCalf = aNodes[static_cast<std::size_t>(BodyNode::LeftCalf)];
    auto* const pLeftFoot = aNodes[static_cast<std::size_t>(BodyNode::LeftFoot)];
    auto* const pRightThigh = aNodes[static_cast<std::size_t>(BodyNode::RightThigh)];
    auto* const pRightCalf = aNodes[static_cast<std::size_t>(BodyNode::RightCalf)];
    auto* const pRightFoot = aNodes[static_cast<std::size_t>(BodyNode::RightFoot)];

    return SkyrimTogetherVR::IsReadableVrMemory(pPelvis->parent, sizeof(void*)) &&
           SkyrimTogetherVR::IsReadableVrMemory(pSpine0->parent, sizeof(void*)) && pSpine1->parent == pSpine0 &&
           pSpine2->parent == pSpine1 && pNeck->parent == pSpine2 && pLeftClavicle->parent == pSpine2 &&
           pLeftUpperArm->parent == pLeftClavicle && pLeftForearm->parent == pLeftUpperArm &&
           pRightClavicle->parent == pSpine2 && pRightUpperArm->parent == pRightClavicle &&
           pRightForearm->parent == pRightUpperArm &&
           pLeftThigh->parent == pPelvis && pLeftCalf->parent == pLeftThigh && pLeftFoot->parent == pLeftCalf &&
           pRightThigh->parent == pPelvis && pRightCalf->parent == pRightThigh && pRightFoot->parent == pRightCalf;
}

VRBodyJointPoseData CaptureJointPose(
    NiAVObject& aRoot,
    const std::uint32_t aCaptureSequence,
    const std::uint32_t aRootGeneration) noexcept
{
    VRBodyJointPoseData result{};
    result.FormatVersion = kBodyJointPoseFormatVersion;
    for (std::size_t index = 0; index < s_jointNodeNames.size(); ++index)
    {
        auto* const node = aRoot.GetObjectByName(s_jointNodeNames[index]);
        if (!SkyrimTogetherVR::IsReadableVrMemory(node, sizeof(void*)))
            continue;
        CopyLocalRotation(*node, result.Rotations[index]);
        result.NodeMask |= 1u << index;
    }

    if (result.NodeMask == 0)
        return result;
    result.Valid = true;
    result.CaptureSequence = aCaptureSequence;
    result.RootGeneration = aRootGeneration;
    return result;
}

VRBodyPoseData CaptureBodyPose() noexcept
{
    auto* const pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
    if (!pPlayer)
        return InvalidBodyPose();

    auto* const pRoot = pPlayer->GetNiNode();
    std::array<NiAVObject*, static_cast<std::size_t>(BodyNode::Count)> nodes{};
    if (!ResolveBodyNodes(pRoot, nodes))
        return InvalidBodyPose();

    const auto rootAddress = reinterpret_cast<std::uintptr_t>(pRoot);
    if (rootAddress != s_lastRootAddress)
    {
        s_lastRootAddress = rootAddress;
        if (++s_rootGeneration == 0)
            ++s_rootGeneration;
    }
    if (++s_captureSequence == 0)
        ++s_captureSequence;

    VRBodyPoseData result{};
    result.FormatVersion = kBodyPoseFormatVersion;
    result.Valid = true;
    result.CaptureSequence = s_captureSequence;
    result.RootGeneration = s_rootGeneration;

    auto* const pPelvis = nodes[static_cast<std::size_t>(BodyNode::Pelvis)];
    CopyLocalRotation(*pPelvis, result.Pelvis);
    result.Pelvis.Position = pPelvis->local.translate;
    if (!CopyEffectiveParentLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::Spine0)], result.Spine0) ||
        !CopyEffectiveParentLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::Spine1)], result.Spine1) ||
        !CopyEffectiveParentLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::Spine2)], result.Spine2))
        return InvalidBodyPose();
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::Neck)], result.Neck);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftClavicle)], result.LeftClavicle);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftUpperArm)], result.LeftUpperArm);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftForearm)], result.LeftForearm);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightClavicle)], result.RightClavicle);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightUpperArm)], result.RightUpperArm);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightForearm)], result.RightForearm);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftThigh)], result.LeftThigh);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftCalf)], result.LeftCalf);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftFoot)], result.LeftFoot);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightThigh)], result.RightThigh);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightCalf)], result.RightCalf);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightFoot)], result.RightFoot);
    result.Joints = CaptureJointPose(*pRoot, result.CaptureSequence, result.RootGeneration);
    return result;
}
} // namespace

bool Activate() noexcept
{
    if (InterlockedCompareExchange(&s_active, 1, 0) != 0)
        return true;

    s_nodeNames[static_cast<std::size_t>(BodyNode::Pelvis)].Set("NPC Pelvis [Pelv]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::Spine0)].Set("NPC Spine [Spn0]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::Spine1)].Set("NPC Spine1 [Spn1]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::Spine2)].Set("NPC Spine2 [Spn2]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::Neck)].Set("NPC Neck [Neck]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftClavicle)].Set("NPC L Clavicle [LClv]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftUpperArm)].Set("NPC L UpperArm [LUar]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftForearm)].Set("NPC L Forearm [LLar]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightClavicle)].Set("NPC R Clavicle [RClv]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightUpperArm)].Set("NPC R UpperArm [RUar]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightForearm)].Set("NPC R Forearm [RLar]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftThigh)].Set("NPC L Thigh [LThg]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftCalf)].Set("NPC L Calf [LClf]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftFoot)].Set("NPC L Foot [Lft ]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightThigh)].Set("NPC R Thigh [RThg]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightCalf)].Set("NPC R Calf [RClf]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightFoot)].Set("NPC R Foot [Rft ]");

    constexpr std::array<const char*, kVRBodyJointRotationCount> kJointNodeNames{
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
    for (std::size_t index = 0; index < kJointNodeNames.size(); ++index)
        s_jointNodeNames[index].Set(kJointNodeNames[index]);
    return true;
}

void Retire() noexcept
{
    if (InterlockedExchange(&s_active, 0) == 0)
        return;

    AcquireSRWLockExclusive(&s_mailboxLock);
    s_latestPose = InvalidBodyPose();
    s_latestCaptureTimeMilliseconds = 0;
    ReleaseSRWLockExclusive(&s_mailboxLock);

    // The HIGGS callback can already be inside GetObjectByName when teardown
    // begins. Keep these process-lifetime interned names alive rather than
    // blocking shutdown or releasing storage underneath that callback.
}

bool CaptureFromPostHiggs(std::uint64_t aCallbackSequence) noexcept
{
    if (InterlockedCompareExchange(&s_active, 0, 0) == 0 || aCallbackSequence == 0)
        return false;

    const auto pose = CaptureBodyPose();
    if (!TryAcquireSRWLockExclusive(&s_mailboxLock))
        return false;

    s_latestPose = pose;
    s_latestCaptureTimeMilliseconds = GetTickCount64();
    ReleaseSRWLockExclusive(&s_mailboxLock);
    return true;
}

VRBodyPoseData CopyLatestFresh(std::uint64_t aMaxAgeMilliseconds) noexcept
{
    auto result = InvalidBodyPose();
    if (InterlockedCompareExchange(&s_active, 0, 0) == 0 || !TryAcquireSRWLockShared(&s_mailboxLock))
        return result;

    const auto capturedAt = s_latestCaptureTimeMilliseconds;
    const auto now = GetTickCount64();
    if (capturedAt != 0 && now >= capturedAt && now - capturedAt <= aMaxAgeMilliseconds)
        result = s_latestPose;

    ReleaseSRWLockShared(&s_mailboxLock);
    return result;
}
} // namespace SkyrimTogetherVR::BodyPoseCapture
