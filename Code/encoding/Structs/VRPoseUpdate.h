#pragma once

#include <array>
#include <cstddef>
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

// The post-VRIK/post-HIGGS callback captures the final local rotations of the
// humanoid finger chains. The set is named by the producer/consumer ordering
// and intentionally excludes translations and scale, which are not portable
// between an avatar's source skeleton and a remote actor's bind pose.
inline constexpr std::size_t kVRBodyJointRotationCount = 30;

struct VRBodyJointRotationData
{
    bool operator==(const VRBodyJointRotationData& acRhs) const noexcept;
    bool operator!=(const VRBodyJointRotationData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // Unit local-rotation quaternion. This reduces the 30-joint payload from
    // nine floats per joint to four while still reconstructing an orthonormal
    // matrix at the remote actor boundary.
    float X{0.0f};
    float Y{0.0f};
    float Z{0.0f};
    float W{1.0f};
};

struct VRBodyJointPoseData
{
    bool operator==(const VRBodyJointPoseData& acRhs) const noexcept;
    bool operator!=(const VRBodyJointPoseData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t FormatVersion{0};
    bool Valid{false};
    uint32_t CaptureSequence{0};
    uint32_t RootGeneration{0};
    uint32_t NodeMask{0};
    std::array<VRBodyJointRotationData, kVRBodyJointRotationCount> Rotations{};
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
    // Format 3 adds the final post-VRIK/post-HIGGS upper-body chain. These
    // are parent-local rotations; positions remain source-skeleton specific.
    VRPoseNodeData Spine0{};
    VRPoseNodeData Spine1{};
    VRPoseNodeData Spine2{};
    VRPoseNodeData Neck{};
    VRPoseNodeData LeftClavicle{};
    VRPoseNodeData LeftUpperArm{};
    VRPoseNodeData LeftForearm{};
    VRPoseNodeData RightClavicle{};
    VRPoseNodeData RightUpperArm{};
    VRPoseNodeData RightForearm{};
    VRPoseNodeData LeftThigh{};
    VRPoseNodeData LeftCalf{};
    VRPoseNodeData LeftFoot{};
    VRPoseNodeData RightThigh{};
    VRPoseNodeData RightCalf{};
    VRPoseNodeData RightFoot{};
    VRBodyJointPoseData Joints{};
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
bool IsSupportedVRBodyPoseFormatVersion(uint32_t aFormatVersion) noexcept;
bool IsVRBodyPoseDataSafe(const VRBodyPoseData& acBody) noexcept;
bool IsVRBodyJointPoseDataSafe(const VRBodyJointPoseData& acJoints) noexcept;
bool IsVRPoseUpdateSafe(const VRPoseUpdate& acPose) noexcept;
bool HasAnyVRPosePayload(const VRPoseUpdate& acPose) noexcept;
