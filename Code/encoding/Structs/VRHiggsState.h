#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/VRHiggsRelayState.h>
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
    // HIGGS 1.10.5+ exposes the grabbed render node. The bounded, NUL-free
    // identity selects that node for hand-relative replay; zero is root-only.
    static constexpr std::size_t kMaximumGrabbedNodeNameBytes = 48;
    std::array<char, kMaximumGrabbedNodeNameBytes> GrabbedNodeName{};
    uint8_t GrabbedNodeNameLength{0};
    VRHiggsFingerState Fingers{};
    VRHiggsGrabTransform GrabTransform{};
    bool IsDecodedValid{true};
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
    // Stash/consume callbacks receive a base form, not a reference. ObjectId
    // remains the synchronously captured held reference; this is metadata only.
    GameId InventoryBaseForm{};
    float Mass{0.0f};
    float SeparatingVelocity{0.0f};
    VRHiggsGrabTransform GrabTransform{};
    std::array<char, VRHiggsHandState::kMaximumGrabbedNodeNameBytes> GrabbedNodeName{};
    uint8_t GrabbedNodeNameLength{0};
    bool IsDecodedValid{true};
};

inline constexpr std::size_t kMaximumHiggsMutationEvents = 32;

struct VRHiggsState
{
    bool operator==(const VRHiggsState& acRhs) const noexcept;
    bool operator!=(const VRHiggsState& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    [[nodiscard]] bool IsMutationReplayValid() const noexcept;

    uint32_t Sequence{0};
    // Nonzero bridge-producer identity. A different epoch atomically rebases
    // HIGGS ledgers, pending edges, visual bindings, and server leases.
    uint64_t ProducerEpoch{0};
    // Sequence is sampled bridge telemetry. Mutation events have their own
    // ordered sequence space and are replayed independently of telemetry.
    uint32_t MutationSequence{0};
    // The bounded mutation window evicted an unseen edge. Receivers must
    // discard stale mutation/binding state and rebuild from Left/Right.
    bool MutationReplayRebased{false};
    bool BridgeLoaded{false};
    bool Detected{false};
    bool InterfaceAvailable{false};
    bool CallbacksRegistered{false};
    bool SnapshotAvailable{false};
    uint32_t SnapshotSequence{0};
    bool TwoHanding{false};
    VRHiggsHandState Left{};
    VRHiggsHandState Right{};
    std::array<VRHiggsEventSnapshot, kMaximumHiggsMutationEvents> MutationEvents{};
    uint8_t MutationEventCount{0};
    // Decode-only validity. It is intentionally not serialized so protocol
    // The negotiated wire format stays stable while malformed bounded counts are
    // rejected by both relay endpoints.
    bool IsDecodedValid{true};
};

[[nodiscard]] constexpr bool IsVRHiggsRelayOperational(const VRHiggsState& acState) noexcept
{
    return IsVRHiggsRelayOperational(VRHiggsRelayState{
        acState.BridgeLoaded,
        acState.Detected,
        acState.InterfaceAvailable,
        acState.CallbacksRegistered,
        acState.SnapshotAvailable,
        acState.SnapshotSequence,
    });
}
