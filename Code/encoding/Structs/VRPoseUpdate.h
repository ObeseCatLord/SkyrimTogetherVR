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

struct VRBodyPoseData
{
    bool operator==(const VRBodyPoseData& acRhs) const noexcept;
    bool operator!=(const VRBodyPoseData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t FormatVersion{0};
    bool Valid{false};
    uint32_t CaptureSequence{0};
    uint32_t RootGeneration{0};
    VRPoseNodeData Pelvis{};
    VRPoseNodeData LeftThigh{};
    VRPoseNodeData LeftCalf{};
    VRPoseNodeData LeftFoot{};
    VRPoseNodeData RightThigh{};
    VRPoseNodeData RightCalf{};
    VRPoseNodeData RightFoot{};
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
    VRBodyPoseData Body{};
    VRVrikData Vrik{};
};

// Wire ingress validation shared by the server relay and client cache. These
// functions do not provide mixed-build negotiation; client and server builds
// must still use the same fixed-order VRPoseUpdate schema.
bool IsVRBodyPoseDataSafe(const VRBodyPoseData& acBody) noexcept;
bool IsVRPoseUpdateSafe(const VRPoseUpdate& acPose) noexcept;
bool HasAnyVRPosePayload(const VRPoseUpdate& acPose) noexcept;
