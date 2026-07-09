#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>
#include <glm/vec3.hpp>

struct VRHiggsFingerState
{
    bool operator==(const VRHiggsFingerState& acRhs) const noexcept;
    bool operator!=(const VRHiggsFingerState& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Valid{false};
    float Thumb{0.0f};
    float Index{0.0f};
    float Middle{0.0f};
    float Ring{0.0f};
    float Pinky{0.0f};
};

struct VRHiggsGrabTransform
{
    bool operator==(const VRHiggsGrabTransform& acRhs) const noexcept;
    bool operator!=(const VRHiggsGrabTransform& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Valid{false};
    Vector3_NetQuantize Translate{};
    glm::vec3 AxisX{1.0f, 0.0f, 0.0f};
    glm::vec3 AxisY{0.0f, 1.0f, 0.0f};
    glm::vec3 AxisZ{0.0f, 0.0f, 1.0f};
    float Scale{1.0f};
};

struct VRHiggsHandState
{
    bool operator==(const VRHiggsHandState& acRhs) const noexcept;
    bool operator!=(const VRHiggsHandState& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool Valid{false};
    bool HoldingObject{false};
    bool CanGrabObject{false};
    bool HandInGrabbableState{false};
    bool Disabled{false};
    bool WeaponCollisionDisabled{false};
    GameId GrabbedObject{};
    VRHiggsFingerState Fingers{};
    VRHiggsGrabTransform GrabTransform{};
};

struct VRHiggsEventSnapshot
{
    enum class Kind : uint8_t
    {
        kUnknown = 0,
        kPulled,
        kGrabbed,
        kDropped,
        kStashed,
        kConsumed,
        kCollision,
        kStartTwoHanding,
        kStopTwoHanding,
    };

    bool operator==(const VRHiggsEventSnapshot& acRhs) const noexcept;
    bool operator!=(const VRHiggsEventSnapshot& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    Kind EventKind{Kind::kUnknown};
    bool HasHand{false};
    bool IsLeft{false};
    GameId ObjectId{};
    float Mass{0.0f};
    float SeparatingVelocity{0.0f};
};

struct VRHiggsState
{
    bool operator==(const VRHiggsState& acRhs) const noexcept;
    bool operator!=(const VRHiggsState& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    bool BridgeLoaded{false};
    bool Detected{false};
    bool InterfaceAvailable{false};
    bool CallbacksRegistered{false};
    bool SnapshotAvailable{false};
    uint32_t SnapshotSequence{0};
    bool TwoHanding{false};
    VRHiggsHandState Left{};
    VRHiggsHandState Right{};
    bool LastEventValid{false};
    VRHiggsEventSnapshot LastEvent{};
};
