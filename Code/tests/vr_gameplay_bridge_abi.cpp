#include <catch2/catch.hpp>

#include "../vr_common/VRGameplayBridge.h"
#include <Structs/MovementOrdering.h>

#include <atomic>
#include <bit>
#include <limits>
#include <thread>

namespace
{
using namespace SkyrimTogetherVR::GameplayBridge;

EventRecord MakeEvent(const std::uint64_t a_sequence) noexcept
{
    EventRecord event{};
    event.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalPlayerState);
    event.Header.PayloadSize = kFixedPayloadBytes;
    event.Header.Identity.ServerInstanceNonce = 0x1122334455667788ull;
    event.Header.Identity.ConnectionGeneration = 7;
    event.Header.Identity.LifecycleEpoch = 9;
    event.Header.Identity.EntityId = 42;
    event.Header.Identity.EntityGeneration = 3;
    event.Header.Identity.SequenceId = a_sequence;
    event.Payload.LocalPlayerState.LocalPlayerHandle = kLocalPlayerHandle;
    event.Payload.LocalPlayerState.LocalCellFormId = 0x1234;
    event.Payload.LocalPlayerState.LocalWorldspaceFormId = 0x5678;
    event.Payload.LocalPlayerState.Root.PositionX = 1.0f;
    event.Payload.LocalPlayerState.Root.RotationW = 1.0f;
    event.Payload.LocalPlayerState.Root.Scale = 1.0f;
    event.Payload.LocalPlayerState.SnapshotFlags = 1;
    event.Payload.LocalPlayerState.LocalActorBaseFormId = 0x7;
    return event;
}
} // namespace

TEST_CASE("VR gameplay bridge ABI constants and layout", "[skyrim-vr][gameplay-bridge]")
{
    REQUIRE(kMappingMagic == 0x42564753);
    REQUIRE(kMappingAbiVersion == 2);
    REQUIRE(kCapabilityRevision == 2);
    REQUIRE(kSkyrimVrRuntimeVersion == 0x010400F0);
    REQUIRE(kSkseVrInterfaceRuntimeVersion == 0x010400F1);
    REQUIRE(kSkseVrInterfaceRuntimeVersion != kSkyrimVrRuntimeVersion);
    REQUIRE(kMinimumSkseVrVersion == 0x020000C0);
    REQUIRE(kMinimumSkseVrReleaseIndex == 60);
    REQUIRE(kFixedPayloadBytes == 64);
    REQUIRE(kDefaultEventRingCapacity == 64);
    REQUIRE(kDefaultCommandRingCapacity == 64);
    REQUIRE(kLocalPlayerHandle.Value == 1);
    REQUIRE(kFirstRemoteAvatarHandle == 2);
    REQUIRE(sizeof(MessageHeader) == 0x40);
    REQUIRE(sizeof(EventRecord) == 0x80);
    REQUIRE(sizeof(CommandRecord) == 0x80);
    REQUIRE(sizeof(MappingHeader) == 0x90);
    REQUIRE(sizeof(EventRing) == 0x2220);
    REQUIRE(sizeof(CommandRing) == 0x2220);
    REQUIRE(sizeof(GameplayBridgeMapping) == 0x44D0);
    REQUIRE(offsetof(GameplayBridgeMapping, Events) == 0x90);
    REQUIRE(offsetof(GameplayBridgeMapping, Commands) == 0x22B0);
    REQUIRE(HasCapability(kInitialCapabilities, Capability::Lifecycle));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalPlayerDiscovery));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalPlayerSnapshot));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteAvatarLifecycle));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteRootTransform));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteSpatialTransfer));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalAnimationGraphSnapshot));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteAnimationGraphSnapshot));
}

TEST_CASE("VR gameplay bridge records preserve identity and payloads", "[skyrim-vr][gameplay-bridge]")
{
    const auto event = MakeEvent(55);
    const auto eventRoundTrip = event;

    REQUIRE(eventRoundTrip.Header.Kind == static_cast<std::uint16_t>(EventKind::LocalPlayerState));
    REQUIRE(eventRoundTrip.Header.PayloadSize == kFixedPayloadBytes);
    REQUIRE(eventRoundTrip.Header.Identity.ServerInstanceNonce == 0x1122334455667788ull);
    REQUIRE(eventRoundTrip.Header.Identity.ConnectionGeneration == 7);
    REQUIRE(eventRoundTrip.Header.Identity.LifecycleEpoch == 9);
    REQUIRE(eventRoundTrip.Header.Identity.EntityId == 42);
    REQUIRE(eventRoundTrip.Header.Identity.EntityGeneration == 3);
    REQUIRE(eventRoundTrip.Header.Identity.SequenceId == 55);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalPlayerHandle.Value == kLocalPlayerHandle.Value);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalCellFormId == 0x1234);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalWorldspaceFormId == 0x5678);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.Root.PositionX == 1.0f);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.Root.RotationW == 1.0f);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalActorBaseFormId == 0x7);

    CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(CommandKind::UpdateRemoteRootTransform);
    command.Header.PayloadSize = kFixedPayloadBytes;
    command.Header.Identity.ServerInstanceNonce = 0xAABBCCDDEEFF0011ull;
    command.Header.Identity.ConnectionGeneration = 8;
    command.Header.Identity.LifecycleEpoch = 10;
    command.Header.Identity.EntityId = 84;
    command.Header.Identity.EntityGeneration = 4;
    command.Header.Identity.SequenceId = 56;
    command.Header.Identity.ActionId = 57;
    command.Payload.UpdateRemoteRootTransform.AvatarHandle.Value = 0xDEADBEEF;
    command.Payload.UpdateRemoteRootTransform.Root.PositionZ = 33.0f;
    command.Payload.UpdateRemoteRootTransform.Root.RotationW = 1.0f;
    command.Payload.UpdateRemoteRootTransform.Root.Scale = 1.0f;
    command.Payload.UpdateRemoteRootTransform.UpdateFlags = 2;
    command.Payload.UpdateRemoteRootTransform.LocalCellFormId = 0x1234;
    command.Payload.UpdateRemoteRootTransform.LocalWorldspaceFormId = 0x5678;
    const auto commandRoundTrip = command;

    REQUIRE(commandRoundTrip.Header.Kind == static_cast<std::uint16_t>(CommandKind::UpdateRemoteRootTransform));
    REQUIRE(commandRoundTrip.Header.Identity.ActionId == 57);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.AvatarHandle.Value == 0xDEADBEEF);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.Root.PositionZ == 33.0f);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.UpdateFlags == 2);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.LocalCellFormId == 0x1234);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.LocalWorldspaceFormId == 0x5678);
}

TEST_CASE("VR animation graph chunks are bounded and preserve fixed-width values", "[skyrim-vr][gameplay-bridge]")
{
    namespace Animation = SkyrimTogetherVR::AnimationGraphProtocol;
    REQUIRE(sizeof(AnimationGraphChunkPayload) == kFixedPayloadBytes);
    REQUIRE(sizeof(RemoteAnimationGraphStatePayload) == kFixedPayloadBytes);
    REQUIRE(sizeof(RemoteSpatialTransferStatePayload) == kFixedPayloadBytes);
    REQUIRE(Animation::kBooleanCount == 60);
    REQUIRE(Animation::kFloatCount == 13);
    REQUIRE(Animation::kIntegerCount == 14);
    REQUIRE(Animation::kValuesPerChunk == 7);
    REQUIRE(Animation::ExpectedChunkMask(Animation::ValueType::BooleanBits) == 1);
    REQUIRE(Animation::ExpectedChunkMask(Animation::ValueType::Float) == 3);
    REQUIRE(Animation::ExpectedChunkMask(Animation::ValueType::Integer) == 3);
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::BooleanBits, 0, 60, 60));
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::Float, 0, 7, 13));
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::Float, 7, 6, 13));
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::Integer, 7, 7, 14));
    REQUIRE_FALSE(Animation::IsValidChunk(Animation::ValueType::Float, 1, 7, 13));
    REQUIRE_FALSE(Animation::IsValidChunk(Animation::ValueType::Integer, 7, 6, 14));

    CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(CommandKind::ApplyRemoteAnimationGraphChunk);
    command.Payload.ApplyRemoteAnimationGraphChunk.AvatarHandle.Value = 99;
    command.Payload.ApplyRemoteAnimationGraphChunk.SnapshotId = 12;
    command.Payload.ApplyRemoteAnimationGraphChunk.DescriptorVersion = Animation::kDescriptorVersion;
    command.Payload.ApplyRemoteAnimationGraphChunk.ValueType = static_cast<std::uint16_t>(Animation::ValueType::Float);
    command.Payload.ApplyRemoteAnimationGraphChunk.StartIndex = 7;
    command.Payload.ApplyRemoteAnimationGraphChunk.ValueCount = 6;
    command.Payload.ApplyRemoteAnimationGraphChunk.TotalCount = Animation::kFloatCount;
    command.Payload.ApplyRemoteAnimationGraphChunk.ChunkFlags = Animation::FullSnapshot;
    command.Payload.ApplyRemoteAnimationGraphChunk.Values[0] = 0x3F800000;
    const auto roundTrip = command;
    REQUIRE(roundTrip.Payload.ApplyRemoteAnimationGraphChunk.SnapshotId == 12);
    REQUIRE(roundTrip.Payload.ApplyRemoteAnimationGraphChunk.Values[0] == 0x3F800000);
}

TEST_CASE("movement tick ordering accepts an initial zero and rejects stale updates", "[skyrim-vr][movement]")
{
    using SkyrimTogether::Protocol::IsNewerMovementTick;
    REQUIRE(IsNewerMovementTick(false, 0, 0));
    REQUIRE(IsNewerMovementTick(false, 100, 1));
    REQUIRE(IsNewerMovementTick(true, 0, 1));
    REQUIRE(IsNewerMovementTick(true, 41, 42));
    REQUIRE_FALSE(IsNewerMovementTick(true, 42, 42));
    REQUIRE_FALSE(IsNewerMovementTick(true, 42, 41));
    REQUIRE_FALSE(IsNewerMovementTick(true, 42, std::numeric_limits<std::uint64_t>::max()));
    REQUIRE_FALSE(IsNewerMovementTick(true, 0, std::uint64_t{1} << 63));
    REQUIRE(IsNewerMovementTick(true, std::numeric_limits<std::uint64_t>::max() - 1, 0));
}

TEST_CASE("animation graph assembly commits only complete current snapshots", "[skyrim-vr][animation]")
{
    namespace Animation = SkyrimTogetherVR::AnimationGraphProtocol;
    Animation::SnapshotBuffer snapshot{};
    std::uint32_t booleanValues[Animation::kValuesPerChunk]{};
    booleanValues[0] = 1u << 3;
    std::uint32_t floatValues0[Animation::kValuesPerChunk]{};
    std::uint32_t floatValues1[Animation::kValuesPerChunk]{};
    std::uint32_t integerValues0[Animation::kValuesPerChunk]{};
    std::uint32_t integerValues1[Animation::kValuesPerChunk]{};
    for (std::size_t index = 0; index < Animation::kValuesPerChunk; ++index)
    {
        floatValues0[index] = std::bit_cast<std::uint32_t>(static_cast<float>(index));
        integerValues0[index] = static_cast<std::uint32_t>(index + 10);
        integerValues1[index] = static_cast<std::uint32_t>(index + 20);
    }
    for (std::size_t index = 0; index < Animation::kFloatCount - Animation::kValuesPerChunk; ++index)
        floatValues1[index] = std::bit_cast<std::uint32_t>(static_cast<float>(index + 7));

    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Float, 7, 6, 13, 1.0f, floatValues1) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::BooleanBits, 0, 60, 60, 1.0f, booleanValues) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Integer, 7, 7, 14, 1.0f, integerValues1) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Float, 0, 7, 13, 1.0f, floatValues0) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Integer, 0, 7, 14, 1.0f, integerValues0) ==
            Animation::ChunkAcceptResult::Complete);
    REQUIRE(snapshot.IsComplete());
    REQUIRE(snapshot.Booleans[3]);
    REQUIRE(snapshot.Floats[12] == 12.0f);
    REQUIRE(snapshot.Integers[13] == 26);

    REQUIRE(Animation::AcceptChunk(snapshot, 3, Animation::ValueType::Float, 0, 7, 13, 1.0f, floatValues0) ==
            Animation::ChunkAcceptResult::Stale);
    floatValues0[0] = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(Animation::AcceptChunk(snapshot, 5, Animation::ValueType::Float, 0, 7, 13, 1.0f, floatValues0) ==
            Animation::ChunkAcceptResult::Malformed);
    REQUIRE_FALSE(snapshot.IsComplete());
    REQUIRE(snapshot.SnapshotId == 0);
}

TEST_CASE("VR gameplay bridge ring rejects full pushes and wraps", "[skyrim-vr][gameplay-bridge]")
{
    BoundedMpmcRing<EventRecord, 4> ring{};
    InitializeRing(ring);

    EventRecord output{};
    REQUIRE_FALSE(TryPop(ring, output));
    REQUIRE(ring.EmptyPopCount.load() == 1);

    for (std::uint64_t sequence = 0; sequence < 4; ++sequence)
        REQUIRE(TryPush(ring, MakeEvent(sequence)));

    REQUIRE_FALSE(TryPush(ring, MakeEvent(4)));
    REQUIRE(ring.DroppedPushCount.load() == 1);

    for (std::uint64_t sequence = 0; sequence < 4; ++sequence)
    {
        REQUIRE(TryPop(ring, output));
        REQUIRE(output.Header.Identity.SequenceId == sequence);
    }

    REQUIRE(TryPush(ring, MakeEvent(4)));
    REQUIRE(TryPop(ring, output));
    REQUIRE(output.Header.Identity.SequenceId == 4);
}

TEST_CASE("VR gameplay bridge ring retains each MPMC record once", "[skyrim-vr][gameplay-bridge]")
{
    constexpr std::uint64_t kProducerCount = 2;
    constexpr std::uint64_t kConsumerCount = 2;
    constexpr std::uint64_t kRecordsPerProducer = 64;
    constexpr std::uint64_t kTotalRecords = kProducerCount * kRecordsPerProducer;

    BoundedMpmcRing<EventRecord, 8> ring{};
    InitializeRing(ring);

    std::atomic<std::uint32_t> seen[kTotalRecords]{};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> invalid{0};

    std::thread producers[kProducerCount];
    for (std::uint64_t producer = 0; producer < kProducerCount; ++producer)
    {
        producers[producer] = std::thread([&, producer] {
            const auto first = producer * kRecordsPerProducer;
            for (std::uint64_t index = 0; index < kRecordsPerProducer; ++index)
            {
                const auto sequence = first + index;
                while (!TryPush(ring, MakeEvent(sequence)))
                    std::this_thread::yield();
            }
        });
    }

    std::thread consumers[kConsumerCount];
    for (auto& consumer : consumers)
    {
        consumer = std::thread([&] {
            EventRecord output{};
            while (consumed.load(std::memory_order_acquire) < kTotalRecords)
            {
                if (!TryPop(ring, output))
                {
                    std::this_thread::yield();
                    continue;
                }

                const auto sequence = output.Header.Identity.SequenceId;
                if (sequence >= kTotalRecords)
                    invalid.fetch_add(1, std::memory_order_relaxed);
                else
                    seen[sequence].fetch_add(1, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (auto& producer : producers)
        producer.join();
    for (auto& consumer : consumers)
        consumer.join();

    REQUIRE(consumed.load() == kTotalRecords);
    REQUIRE(invalid.load() == 0);
    for (const auto& count : seen)
        REQUIRE(count.load() == 1);
}
