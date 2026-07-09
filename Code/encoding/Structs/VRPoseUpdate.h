#pragma once

#include <cstdint>

#include <TiltedCore/Buffer.hpp>
#include <glm/vec3.hpp>

struct VRPoseNodeData
{
    bool operator==(const VRPoseNodeData& acRhs) const noexcept;
    bool operator!=(const VRPoseNodeData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Valid{false};
    glm::vec3 Position{};
    glm::vec3 AxisX{1.0f, 0.0f, 0.0f};
    glm::vec3 AxisY{0.0f, 1.0f, 0.0f};
    glm::vec3 AxisZ{0.0f, 0.0f, 1.0f};
    float Scale{1.0f};
};

struct VRFingerCurlData
{
    bool operator==(const VRFingerCurlData& acRhs) const noexcept;
    bool operator!=(const VRFingerCurlData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Valid{false};
    float Thumb{0.0f};
    float Index{0.0f};
    float Middle{0.0f};
    float Ring{0.0f};
    float Pinky{0.0f};
};

struct VRVrikData
{
    bool operator==(const VRVrikData& acRhs) const noexcept;
    bool operator!=(const VRVrikData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Detected{false};
    bool InterfaceAvailable{false};
    VRFingerCurlData LeftFingers{};
    VRFingerCurlData RightFingers{};
    bool CameraOffsetsValid{false};
    glm::vec3 CameraOffset{};
    glm::vec3 FinalCameraOffset{};
    glm::vec3 FinalSmoothingOffset{};
};

struct VRPoseUpdate
{
    bool operator==(const VRPoseUpdate& acRhs) const noexcept;
    bool operator!=(const VRPoseUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    VRPoseNodeData Hmd{};
    VRPoseNodeData LeftHand{};
    VRPoseNodeData RightHand{};
    VRPoseNodeData SpellOrigin{};
    VRPoseNodeData SpellDestination{};
    VRPoseNodeData ArrowOrigin{};
    VRPoseNodeData ArrowDestination{};
    VRPoseNodeData BowAim{};
    VRPoseNodeData BowRotation{};
    VRPoseNodeData LeftWeaponOffset{};
    VRPoseNodeData RightWeaponOffset{};
    VRPoseNodeData PrimaryMagicOffset{};
    VRPoseNodeData PrimaryMagicAim{};
    VRPoseNodeData SecondaryMagicOffset{};
    VRPoseNodeData SecondaryMagicAim{};
    VRVrikData Vrik{};
};
