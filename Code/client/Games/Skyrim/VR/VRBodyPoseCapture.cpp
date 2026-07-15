#include <VR/VRBodyPoseCapture.h>

#include <Windows.h>

#include <array>

#include <Misc/BSFixedString.h>
#include <NetImmerse/NiAVObject.h>
#include <PlayerCharacter.h>
#include <VR/VRPlayerReadiness.h>

namespace SkyrimTogetherVR::BodyPoseCapture
{
namespace
{
constexpr std::uint32_t kBodyPoseFormatVersion = 1;

enum class BodyNode : std::size_t
{
    Pelvis,
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
    auto* const pLeftThigh = aNodes[static_cast<std::size_t>(BodyNode::LeftThigh)];
    auto* const pLeftCalf = aNodes[static_cast<std::size_t>(BodyNode::LeftCalf)];
    auto* const pLeftFoot = aNodes[static_cast<std::size_t>(BodyNode::LeftFoot)];
    auto* const pRightThigh = aNodes[static_cast<std::size_t>(BodyNode::RightThigh)];
    auto* const pRightCalf = aNodes[static_cast<std::size_t>(BodyNode::RightCalf)];
    auto* const pRightFoot = aNodes[static_cast<std::size_t>(BodyNode::RightFoot)];

    return SkyrimTogetherVR::IsReadableVrMemory(pPelvis->parent, sizeof(void*)) &&
           pLeftThigh->parent == pPelvis && pLeftCalf->parent == pLeftThigh && pLeftFoot->parent == pLeftCalf &&
           pRightThigh->parent == pPelvis && pRightCalf->parent == pRightThigh && pRightFoot->parent == pRightCalf;
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
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftThigh)], result.LeftThigh);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftCalf)], result.LeftCalf);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::LeftFoot)], result.LeftFoot);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightThigh)], result.RightThigh);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightCalf)], result.RightCalf);
    CopyLocalRotation(*nodes[static_cast<std::size_t>(BodyNode::RightFoot)], result.RightFoot);
    return result;
}
} // namespace

bool Activate() noexcept
{
    if (InterlockedCompareExchange(&s_active, 1, 0) != 0)
        return true;

    s_nodeNames[static_cast<std::size_t>(BodyNode::Pelvis)].Set("NPC Pelvis [Pelv]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftThigh)].Set("NPC L Thigh [LThg]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftCalf)].Set("NPC L Calf [LClf]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::LeftFoot)].Set("NPC L Foot [Lft ]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightThigh)].Set("NPC R Thigh [RThg]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightCalf)].Set("NPC R Calf [RClf]");
    s_nodeNames[static_cast<std::size_t>(BodyNode::RightFoot)].Set("NPC R Foot [Rft ]");
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
