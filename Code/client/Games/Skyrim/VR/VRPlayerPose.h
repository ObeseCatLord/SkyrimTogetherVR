#pragma once

#include <cstdint>

#include <Games/Primitives.h>

struct NiNode;

struct VRPoseNode
{
    NiNode* Node{nullptr};

    [[nodiscard]] bool IsValid() const noexcept { return Node != nullptr; }
    [[nodiscard]] std::uintptr_t Address() const noexcept { return reinterpret_cast<std::uintptr_t>(Node); }
};

struct VRPlayerPose
{
    bool Available{false};
    VRPoseNode Hmd;
    VRPoseNode LeftHand;
    VRPoseNode RightHand;
    VRPoseNode SpellOrigin;
    VRPoseNode SpellDestination;
    VRPoseNode ArrowOrigin;
    VRPoseNode ArrowDestination;
    VRPoseNode BowAim;
    VRPoseNode BowRotation;
    VRPoseNode LeftWeaponOffset;
    VRPoseNode RightWeaponOffset;
    VRPoseNode PrimaryMagicOffset;
    VRPoseNode PrimaryMagicAim;
    VRPoseNode SecondaryMagicOffset;
    VRPoseNode SecondaryMagicAim;
};

struct VRPoseTransform
{
    bool Valid{false};
    std::uintptr_t NodeAddress{0};
    NiPoint3 AxisX{};
    NiPoint3 AxisY{};
    NiPoint3 AxisZ{};
    NiPoint3 Position{};
    float Scale{1.0f};
};

struct VRPlayerPoseSnapshot
{
    bool Available{false};
    VRPoseTransform Hmd;
    VRPoseTransform LeftHand;
    VRPoseTransform RightHand;
    VRPoseTransform SpellOrigin;
    VRPoseTransform SpellDestination;
    VRPoseTransform ArrowOrigin;
    VRPoseTransform ArrowDestination;
    VRPoseTransform BowAim;
    VRPoseTransform BowRotation;
    VRPoseTransform LeftWeaponOffset;
    VRPoseTransform RightWeaponOffset;
    VRPoseTransform PrimaryMagicOffset;
    VRPoseTransform PrimaryMagicAim;
    VRPoseTransform SecondaryMagicOffset;
    VRPoseTransform SecondaryMagicAim;
};

namespace SkyrimVR
{
bool CaptureLocalPlayerPose(VRPlayerPose& aOut) noexcept;
bool CaptureLocalPlayerPoseSnapshot(VRPlayerPoseSnapshot& aOut) noexcept;
}
