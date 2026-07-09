#include <VR/VRPlayerPose.h>

#include <NetImmerse/NiNode.h>
#include <PlayerCharacter.h>

namespace
{
#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

VRPoseNode FirstValidNode(NiNode* apPreferred, NiNode* apFallback) noexcept
{
    return {apPreferred ? apPreferred : apFallback};
}

VRPoseTransform CaptureTransform(const VRPoseNode& aNode) noexcept
{
    VRPoseTransform result{};
    if (!aNode.IsValid())
        return result;

    const auto& transform = aNode.Node->world;

    result.Valid = true;
    result.NodeAddress = aNode.Address();
    result.AxisX = NiPoint3(glm::vec3(transform.rotate[0][0], transform.rotate[0][1], transform.rotate[0][2]));
    result.AxisY = NiPoint3(glm::vec3(transform.rotate[1][0], transform.rotate[1][1], transform.rotate[1][2]));
    result.AxisZ = NiPoint3(glm::vec3(transform.rotate[2][0], transform.rotate[2][1], transform.rotate[2][2]));
    result.Position = transform.translate;
    result.Scale = transform.scale;

    return result;
}
}

namespace SkyrimVR
{
bool CaptureLocalPlayerPose(VRPlayerPose& aOut) noexcept
{
    aOut = {};

#if TP_SKYRIM_VR
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    const auto* pVRNodes = pPlayer->GetVRNodeData();
    if (!pVRNodes)
        return false;

    auto* pLeftController = pVRNodes->LeftValveIndexControllerNode;
    auto* pRightController = pVRNodes->RightValveIndexControllerNode;

    aOut.Hmd = {pVRNodes->UprightHmdNode};
    aOut.LeftHand = FirstValidNode(pLeftController, pVRNodes->LeftWandNode);
    aOut.RightHand = FirstValidNode(pRightController, pVRNodes->RightWandNode);
    aOut.SpellOrigin = {pVRNodes->SpellOrigin};
    aOut.SpellDestination = {pVRNodes->SpellDestination};
    aOut.ArrowOrigin = {pVRNodes->ArrowOrigin};
    aOut.ArrowDestination = {pVRNodes->ArrowDestination};
    aOut.BowAim = {pVRNodes->BowAimNode};
    aOut.BowRotation = {pVRNodes->BowRotationNode};
    aOut.LeftWeaponOffset = {pVRNodes->LeftWeaponOffsetNode};
    aOut.RightWeaponOffset = {pVRNodes->RightWeaponOffsetNode};
    aOut.PrimaryMagicOffset = {pVRNodes->PrimaryMagicOffsetNode};
    aOut.PrimaryMagicAim = {pVRNodes->PrimaryMagicAimNode};
    aOut.SecondaryMagicOffset = {pVRNodes->SecondaryMagicOffsetNode};
    aOut.SecondaryMagicAim = {pVRNodes->SecondaryMagicAimNode};

    aOut.Available = aOut.Hmd.IsValid() || aOut.LeftHand.IsValid() || aOut.RightHand.IsValid() ||
                     aOut.SpellOrigin.IsValid() || aOut.ArrowOrigin.IsValid() ||
                     aOut.LeftWeaponOffset.IsValid() || aOut.RightWeaponOffset.IsValid();
    return aOut.Available;
#else
    return false;
#endif
}

bool CaptureLocalPlayerPoseSnapshot(VRPlayerPoseSnapshot& aOut) noexcept
{
    aOut = {};

#if TP_SKYRIM_VR
    VRPlayerPose pose{};
    if (!CaptureLocalPlayerPose(pose))
        return false;

    aOut.Hmd = CaptureTransform(pose.Hmd);
    aOut.LeftHand = CaptureTransform(pose.LeftHand);
    aOut.RightHand = CaptureTransform(pose.RightHand);
    aOut.SpellOrigin = CaptureTransform(pose.SpellOrigin);
    aOut.SpellDestination = CaptureTransform(pose.SpellDestination);
    aOut.ArrowOrigin = CaptureTransform(pose.ArrowOrigin);
    aOut.ArrowDestination = CaptureTransform(pose.ArrowDestination);
    aOut.BowAim = CaptureTransform(pose.BowAim);
    aOut.BowRotation = CaptureTransform(pose.BowRotation);
    aOut.LeftWeaponOffset = CaptureTransform(pose.LeftWeaponOffset);
    aOut.RightWeaponOffset = CaptureTransform(pose.RightWeaponOffset);
    aOut.PrimaryMagicOffset = CaptureTransform(pose.PrimaryMagicOffset);
    aOut.PrimaryMagicAim = CaptureTransform(pose.PrimaryMagicAim);
    aOut.SecondaryMagicOffset = CaptureTransform(pose.SecondaryMagicOffset);
    aOut.SecondaryMagicAim = CaptureTransform(pose.SecondaryMagicAim);

    aOut.Available = aOut.Hmd.Valid || aOut.LeftHand.Valid || aOut.RightHand.Valid ||
                     aOut.SpellOrigin.Valid || aOut.ArrowOrigin.Valid || aOut.BowAim.Valid ||
                     aOut.LeftWeaponOffset.Valid || aOut.RightWeaponOffset.Valid;
    return aOut.Available;
#else
    return false;
#endif
}
}
