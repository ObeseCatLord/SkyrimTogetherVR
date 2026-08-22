#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation
{
inline constexpr std::size_t kMaximumActors = 64;
inline constexpr std::size_t kPoseNodeCount = 20;
inline constexpr std::size_t kJointCount = 30;
inline constexpr std::uint64_t kMaximumFrameAgeMilliseconds = 180;
inline constexpr std::uint64_t kInterpolationDelayMilliseconds = 50;

inline constexpr std::uintptr_t kCharacterVtableVrRva = 0x16D6DE0;
inline constexpr std::size_t kUpdateAnimationSlot = 0x7D;
inline constexpr std::uintptr_t kCharacterUpdateAnimationSlotVrRva = 0x16D71C8;
inline constexpr std::uintptr_t kActorUpdateAnimationVrRva = 0x5E1F10;
inline constexpr std::uintptr_t kActorUpdateAnimationVrEndRva = 0x5E1F8C;
inline constexpr std::array<std::uint8_t, 22> kActorUpdateAnimationVrPrefix{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20,
    0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0xE8, 0x1A, 0x02, 0x00, 0x00,
};

struct Quaternion
{
    float X{};
    float Y{};
    float Z{};
    float W{1.0F};
};

struct Transform
{
    float X{};
    float Y{};
    float Z{};
    Quaternion Rotation{};
    float Scale{1.0F};
};

struct Frame
{
    std::uint64_t ActorHandle{};
    std::uintptr_t ActorAddress{};
    std::uintptr_t RootAddress{};
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t LifecycleEpoch{};
    std::uint64_t ConnectionGeneration{};
    std::uint32_t RootGeneration{};
    std::uint32_t Sequence{};
    std::uint64_t AdmittedAtMilliseconds{};
    std::uint32_t NodeMask{};
    std::uint32_t JointMask{};
    std::array<Transform, kPoseNodeCount> Nodes{};
    std::array<Quaternion, kJointCount> Joints{};
};
static_assert(std::is_trivially_copyable_v<Frame>);

struct SlotIdentity
{
    std::uint64_t ActorHandle{};
    std::uintptr_t ActorAddress{};
    std::uintptr_t RootAddress{};
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t LifecycleEpoch{};
    std::uint64_t ConnectionGeneration{};
    std::uint32_t RootGeneration{};
};

enum class FrameChannel : std::uint8_t
{
    Body,
    Joints,
    Invalid,
};

struct FrameSelection
{
    Frame Older{};
    Frame Newer{};
    std::uint8_t Count{};
};

struct PoseSnapshot
{
    SlotIdentity Identity{};
    FrameSelection Body{};
    FrameSelection Joints{};
};

[[nodiscard]] constexpr FrameChannel ChannelFor(const Frame& a_frame) noexcept
{
    if (a_frame.NodeMask != 0 && a_frame.JointMask == 0)
        return FrameChannel::Body;
    if (a_frame.NodeMask == 0 && a_frame.JointMask != 0)
        return FrameChannel::Joints;
    return FrameChannel::Invalid;
}

[[nodiscard]] constexpr bool ShouldAdmitFrame(const Frame& a_frame) noexcept
{
    return a_frame.ActorHandle != 0 && a_frame.ActorAddress != 0 && a_frame.RootAddress != 0 &&
           a_frame.ServerInstanceNonce != 0 && a_frame.LifecycleEpoch != 0 &&
           a_frame.ConnectionGeneration != 0 && a_frame.RootGeneration != 0 &&
           a_frame.Sequence != 0 && a_frame.AdmittedAtMilliseconds != 0 &&
           ChannelFor(a_frame) != FrameChannel::Invalid;
}

[[nodiscard]] constexpr bool SameIdentity(const Frame& a_frame, const SlotIdentity& a_identity) noexcept
{
    return a_identity.ActorHandle != 0 && a_identity.ActorAddress != 0 && a_identity.RootAddress != 0 &&
           a_identity.ServerInstanceNonce != 0 && a_identity.LifecycleEpoch != 0 &&
           a_identity.ConnectionGeneration != 0 && a_identity.RootGeneration != 0 &&
           a_frame.ActorHandle == a_identity.ActorHandle && a_frame.ActorAddress == a_identity.ActorAddress &&
           a_frame.RootAddress == a_identity.RootAddress &&
           a_frame.ServerInstanceNonce == a_identity.ServerInstanceNonce &&
           a_frame.LifecycleEpoch == a_identity.LifecycleEpoch &&
           a_frame.ConnectionGeneration == a_identity.ConnectionGeneration &&
           a_frame.RootGeneration == a_identity.RootGeneration;
}

[[nodiscard]] constexpr bool IsFresh(
    const Frame& a_frame,
    const std::uint64_t a_currentTimeMilliseconds) noexcept
{
    return a_currentTimeMilliseconds >= a_frame.AdmittedAtMilliseconds &&
           a_currentTimeMilliseconds - a_frame.AdmittedAtMilliseconds <= kMaximumFrameAgeMilliseconds;
}

[[nodiscard]] constexpr bool MatchesFastMetadata(
    const std::uintptr_t a_slotActorAddress,
    const std::uintptr_t a_slotRootAddress,
    const std::uintptr_t a_actorAddress,
    const std::uintptr_t a_rootAddress) noexcept
{
    return a_actorAddress != 0 && a_rootAddress != 0 &&
           a_slotActorAddress == a_actorAddress && a_slotRootAddress == a_rootAddress;
}

[[nodiscard]] constexpr bool ShouldPresentActor(
    const bool a_isPlayerCharacter,
    const bool a_isRagdolled) noexcept
{
    return !a_isPlayerCharacter && !a_isRagdolled;
}

[[nodiscard]] inline Quaternion Normalize(const Quaternion a_value) noexcept
{
    const auto lengthSquared = a_value.X * a_value.X + a_value.Y * a_value.Y +
                               a_value.Z * a_value.Z + a_value.W * a_value.W;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001F)
        return {};
    const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {a_value.X * inverseLength, a_value.Y * inverseLength, a_value.Z * inverseLength, a_value.W * inverseLength};
}

[[nodiscard]] inline Quaternion Slerp(const Quaternion a_left, Quaternion a_right, const float a_alpha) noexcept
{
    const auto left = Normalize(a_left);
    a_right = Normalize(a_right);
    auto dot = left.X * a_right.X + left.Y * a_right.Y + left.Z * a_right.Z + left.W * a_right.W;
    if (dot < 0.0F) {
        dot = -dot;
        a_right = {-a_right.X, -a_right.Y, -a_right.Z, -a_right.W};
    }
    dot = dot > 1.0F ? 1.0F : dot;
    const auto alpha = a_alpha < 0.0F ? 0.0F : (a_alpha > 1.0F ? 1.0F : a_alpha);
    if (dot > 0.9995F)
        return Normalize({left.X + (a_right.X - left.X) * alpha, left.Y + (a_right.Y - left.Y) * alpha,
                          left.Z + (a_right.Z - left.Z) * alpha, left.W + (a_right.W - left.W) * alpha});
    const auto theta = std::acos(dot);
    const auto sine = std::sin(theta);
    if (sine <= 0.000001F)
        return left;
    const auto leftWeight = std::sin((1.0F - alpha) * theta) / sine;
    const auto rightWeight = std::sin(alpha * theta) / sine;
    return Normalize({left.X * leftWeight + a_right.X * rightWeight, left.Y * leftWeight + a_right.Y * rightWeight,
                      left.Z * leftWeight + a_right.Z * rightWeight, left.W * leftWeight + a_right.W * rightWeight});
}

[[nodiscard]] inline Frame Interpolate(const Frame& a_older, const Frame& a_newer, const float a_alpha) noexcept
{
    Frame result = a_newer;
    const auto alpha = a_alpha < 0.0F ? 0.0F : (a_alpha > 1.0F ? 1.0F : a_alpha);
    for (std::size_t index = 0; index < kPoseNodeCount; ++index) {
        const auto bit = 1u << index;
        if ((a_older.NodeMask & a_newer.NodeMask & bit) == 0)
            continue;
        const auto& older = a_older.Nodes[index];
        const auto& newer = a_newer.Nodes[index];
        result.Nodes[index] = {older.X + (newer.X - older.X) * alpha, older.Y + (newer.Y - older.Y) * alpha,
                               older.Z + (newer.Z - older.Z) * alpha, Slerp(older.Rotation, newer.Rotation, alpha),
                               older.Scale + (newer.Scale - older.Scale) * alpha};
    }
    for (std::size_t index = 0; index < kJointCount; ++index) {
        const auto bit = 1u << index;
        if ((a_older.JointMask & a_newer.JointMask & bit) != 0)
            result.Joints[index] = Slerp(a_older.Joints[index], a_newer.Joints[index], alpha);
    }
    return result;
}

[[nodiscard]] constexpr float InterpolationAlpha(
    const Frame& a_older,
    const Frame& a_newer,
    const std::uint64_t a_currentTimeMilliseconds) noexcept
{
    if (a_newer.AdmittedAtMilliseconds <= a_older.AdmittedAtMilliseconds)
        return 1.0F;
    const auto presentationTime = a_currentTimeMilliseconds > kInterpolationDelayMilliseconds ?
                                      a_currentTimeMilliseconds - kInterpolationDelayMilliseconds : 0;
    if (presentationTime <= a_older.AdmittedAtMilliseconds)
        return 0.0F;
    if (presentationTime >= a_newer.AdmittedAtMilliseconds)
        return 1.0F;
    return static_cast<float>(presentationTime - a_older.AdmittedAtMilliseconds) /
           static_cast<float>(a_newer.AdmittedAtMilliseconds - a_older.AdmittedAtMilliseconds);
}

[[nodiscard]] inline Frame ResolveSelection(
    const FrameSelection& a_selection,
    const std::uint64_t a_currentTimeMilliseconds) noexcept
{
    if (a_selection.Count < 2)
        return a_selection.Newer;
    return Interpolate(
        a_selection.Older,
        a_selection.Newer,
        InterpolationAlpha(a_selection.Older, a_selection.Newer, a_currentTimeMilliseconds));
}

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kCharacterUpdateAnimationSlotVrRva ==
               kCharacterVtableVrRva + kUpdateAnimationSlot * sizeof(void*) &&
           kActorUpdateAnimationVrEndRva > kActorUpdateAnimationVrRva &&
           kActorUpdateAnimationVrPrefix.size() <=
               kActorUpdateAnimationVrEndRva - kActorUpdateAnimationVrRva;
}

[[nodiscard]] constexpr bool ShouldPatchCharacterVtableSlot(
    const std::uintptr_t a_slotAddress,
    const std::uintptr_t a_expectedSlotAddress,
    const std::uintptr_t a_slotTarget,
    const std::uintptr_t a_expectedTarget,
    const bool a_isPlayerCharacterSlot) noexcept
{
    return !a_isPlayerCharacterSlot && a_slotAddress == a_expectedSlotAddress && a_slotTarget == a_expectedTarget;
}

[[nodiscard]] constexpr bool CanClearCallableState(
    const bool a_slotRestored,
    const bool a_slotProtectionRestored,
    const std::uint64_t a_inFlightCallbacks,
    const bool a_entryCountStable) noexcept
{
    return a_slotRestored && a_slotProtectionRestored &&
           a_inFlightCallbacks == 0 && a_entryCountStable;
}

class FrameCache final
{
public:
    [[nodiscard]] bool Admit(const Frame& a_frame) noexcept
    {
        if (!ShouldAdmitFrame(a_frame))
            return false;
        AtomicSlot* target{};
        AtomicSlot* empty{};
        for (auto& slot : _slots) {
            const auto handle = slot.ActorHandle.load(std::memory_order_acquire);
            if (handle == a_frame.ActorHandle) {
                target = std::addressof(slot);
                break;
            }
            if (!empty && handle == 0)
                empty = std::addressof(slot);
        }
        if (!target)
            target = empty;
        if (!target)
            return false;

        const SlotIdentity identity{
            a_frame.ActorHandle, a_frame.ActorAddress, a_frame.RootAddress,
            a_frame.ServerInstanceNonce, a_frame.LifecycleEpoch,
            a_frame.ConnectionGeneration, a_frame.RootGeneration};
        SlotIdentity existing{};
        if (!ReadMetadata(*target, existing) || existing.ActorHandle == 0 ||
            existing.ActorAddress != identity.ActorAddress || existing.RootAddress != identity.RootAddress ||
            existing.ServerInstanceNonce != identity.ServerInstanceNonce ||
            existing.LifecycleEpoch != identity.LifecycleEpoch ||
            existing.ConnectionGeneration != identity.ConnectionGeneration ||
            existing.RootGeneration != identity.RootGeneration)
            ResetSlot(*target, identity);

        const auto channel = ChannelFor(a_frame);
        auto& frames = channel == FrameChannel::Body ? target->BodyFrames : target->JointFrames;
        auto& next = channel == FrameChannel::Body ? target->NextBodyWrite : target->NextJointWrite;
        WriteFrame(frames[next.fetch_add(1, std::memory_order_relaxed) & 1u], a_frame);
        return true;
    }

    [[nodiscard]] bool TrySnapshot(
        const std::uintptr_t a_actorAddress,
        const std::uintptr_t a_rootAddress,
        const std::uint64_t a_currentTimeMilliseconds,
        PoseSnapshot& ar_snapshot) const noexcept
    {
        for (const auto& slot : _slots) {
            const auto actorAddress = slot.ActorAddress.load(std::memory_order_acquire);
            if (actorAddress != a_actorAddress)
                continue;
            const auto rootAddress = slot.RootAddress.load(std::memory_order_acquire);
            if (!MatchesFastMetadata(actorAddress, rootAddress, a_actorAddress, a_rootAddress))
                continue;

            SlotIdentity identity{};
            if (!ReadMetadata(slot, identity) || identity.ActorAddress != a_actorAddress ||
                identity.RootAddress != a_rootAddress)
                continue;
            ar_snapshot = {};
            ar_snapshot.Identity = identity;
            SelectFrames(slot.BodyFrames, FrameChannel::Body, identity, a_currentTimeMilliseconds, ar_snapshot.Body);
            SelectFrames(slot.JointFrames, FrameChannel::Joints, identity, a_currentTimeMilliseconds, ar_snapshot.Joints);
            if (ar_snapshot.Body.Count != 0 || ar_snapshot.Joints.Count != 0)
                return true;
        }
        return false;
    }

    void Evict(const std::uint64_t a_actorHandle) noexcept
    {
        if (a_actorHandle == 0)
            return;
        for (auto& slot : _slots) {
            if (slot.ActorHandle.load(std::memory_order_acquire) != a_actorHandle)
                continue;
            ResetSlot(slot, {});
            return;
        }
    }

    void Reset() noexcept
    {
        for (auto& slot : _slots)
            ResetSlot(slot, {});
    }

private:
    struct AtomicFrame
    {
        std::atomic<std::uint32_t> SequenceLock{};
        std::array<std::atomic<std::uint32_t>, sizeof(Frame) / sizeof(std::uint32_t)> Words{};
    };
    static_assert(sizeof(Frame) % sizeof(std::uint32_t) == 0);

    struct AtomicSlot
    {
        std::atomic<std::uint32_t> MetadataSequence{};
        std::atomic<std::uint64_t> ActorHandle{};
        std::atomic<std::uintptr_t> ActorAddress{};
        std::atomic<std::uintptr_t> RootAddress{};
        std::atomic<std::uint64_t> ServerInstanceNonce{};
        std::atomic<std::uint64_t> LifecycleEpoch{};
        std::atomic<std::uint64_t> ConnectionGeneration{};
        std::atomic<std::uint32_t> RootGeneration{};
        std::array<AtomicFrame, 2> BodyFrames{};
        std::array<AtomicFrame, 2> JointFrames{};
        std::atomic<std::uint32_t> NextBodyWrite{};
        std::atomic<std::uint32_t> NextJointWrite{};
    };

    [[nodiscard]] static bool ReadFrame(const AtomicFrame& a_source, Frame& ar_frame) noexcept
    {
        std::array<std::uint32_t, sizeof(Frame) / sizeof(std::uint32_t)> words{};
        for (std::size_t attempt = 0; attempt < 2; ++attempt) {
            const auto before = a_source.SequenceLock.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;
            for (std::size_t index = 0; index < words.size(); ++index)
                words[index] = a_source.Words[index].load(std::memory_order_relaxed);
            const auto after = a_source.SequenceLock.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0) {
                std::memcpy(std::addressof(ar_frame), words.data(), sizeof(ar_frame));
                return true;
            }
        }
        return false;
    }

    static void WriteFrame(AtomicFrame& ar_destination, const Frame& a_frame) noexcept
    {
        std::array<std::uint32_t, sizeof(Frame) / sizeof(std::uint32_t)> words{};
        std::memcpy(words.data(), std::addressof(a_frame), sizeof(a_frame));
        const auto sequence = ar_destination.SequenceLock.load(std::memory_order_relaxed);
        ar_destination.SequenceLock.store(sequence | 1u, std::memory_order_release);
        for (std::size_t index = 0; index < words.size(); ++index)
            ar_destination.Words[index].store(words[index], std::memory_order_relaxed);
        ar_destination.SequenceLock.store((sequence | 1u) + 1u, std::memory_order_release);
    }

    [[nodiscard]] static bool ReadMetadata(const AtomicSlot& a_slot, SlotIdentity& ar_identity) noexcept
    {
        for (std::size_t attempt = 0; attempt < 2; ++attempt) {
            const auto before = a_slot.MetadataSequence.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;
            SlotIdentity identity{
                a_slot.ActorHandle.load(std::memory_order_relaxed),
                a_slot.ActorAddress.load(std::memory_order_relaxed),
                a_slot.RootAddress.load(std::memory_order_relaxed),
                a_slot.ServerInstanceNonce.load(std::memory_order_relaxed),
                a_slot.LifecycleEpoch.load(std::memory_order_relaxed),
                a_slot.ConnectionGeneration.load(std::memory_order_relaxed),
                a_slot.RootGeneration.load(std::memory_order_relaxed)};
            const auto after = a_slot.MetadataSequence.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0) {
                ar_identity = identity;
                return true;
            }
        }
        return false;
    }

    static void ResetSlot(AtomicSlot& ar_slot, const SlotIdentity& a_identity) noexcept
    {
        const auto sequence = ar_slot.MetadataSequence.load(std::memory_order_relaxed);
        ar_slot.MetadataSequence.store(sequence | 1u, std::memory_order_release);
        ar_slot.ActorAddress.store(0, std::memory_order_relaxed);
        ar_slot.RootAddress.store(0, std::memory_order_relaxed);
        const Frame cleared{};
        for (auto& frame : ar_slot.BodyFrames)
            WriteFrame(frame, cleared);
        for (auto& frame : ar_slot.JointFrames)
            WriteFrame(frame, cleared);
        ar_slot.NextBodyWrite.store(0, std::memory_order_relaxed);
        ar_slot.NextJointWrite.store(0, std::memory_order_relaxed);
        ar_slot.ActorHandle.store(a_identity.ActorHandle, std::memory_order_relaxed);
        ar_slot.ServerInstanceNonce.store(a_identity.ServerInstanceNonce, std::memory_order_relaxed);
        ar_slot.LifecycleEpoch.store(a_identity.LifecycleEpoch, std::memory_order_relaxed);
        ar_slot.ConnectionGeneration.store(a_identity.ConnectionGeneration, std::memory_order_relaxed);
        ar_slot.RootGeneration.store(a_identity.RootGeneration, std::memory_order_relaxed);
        ar_slot.RootAddress.store(a_identity.RootAddress, std::memory_order_relaxed);
        ar_slot.ActorAddress.store(a_identity.ActorAddress, std::memory_order_relaxed);
        ar_slot.MetadataSequence.store((sequence | 1u) + 1u, std::memory_order_release);
    }

    static void SelectFrames(
        const std::array<AtomicFrame, 2>& a_frames,
        const FrameChannel a_channel,
        const SlotIdentity& a_identity,
        const std::uint64_t a_currentTimeMilliseconds,
        FrameSelection& ar_selection) noexcept
    {
        Frame candidates[2]{};
        bool valid[2]{};
        for (std::size_t index = 0; index < 2; ++index) {
            valid[index] = ReadFrame(a_frames[index], candidates[index]) &&
                           ChannelFor(candidates[index]) == a_channel &&
                           SameIdentity(candidates[index], a_identity) &&
                           IsFresh(candidates[index], a_currentTimeMilliseconds);
        }
        if (!valid[0] && !valid[1])
            return;
        if (valid[0] && valid[1]) {
            const bool firstOlder = candidates[0].AdmittedAtMilliseconds < candidates[1].AdmittedAtMilliseconds ||
                (candidates[0].AdmittedAtMilliseconds == candidates[1].AdmittedAtMilliseconds &&
                 static_cast<std::int32_t>(candidates[0].Sequence - candidates[1].Sequence) < 0);
            ar_selection.Older = firstOlder ? candidates[0] : candidates[1];
            ar_selection.Newer = firstOlder ? candidates[1] : candidates[0];
            ar_selection.Count = 2;
            return;
        }
        ar_selection.Newer = valid[0] ? candidates[0] : candidates[1];
        ar_selection.Count = 1;
    }

    std::array<AtomicSlot, kMaximumActors> _slots{};
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);

struct Diagnostics
{
    std::array<std::atomic<std::uint32_t>, 8> ObservedThreadIds{};
    std::atomic<std::uint64_t> Callbacks{};
    std::atomic<std::uint64_t> FastRejections{};
    std::atomic<std::uint64_t> PlayerRejections{};
    std::atomic<std::uint64_t> RagdollRejections{};
    std::atomic<std::uint64_t> SnapshotRejections{};
    std::atomic<std::uint64_t> ApplySuccesses{};
    std::atomic<std::uint64_t> ApplyFailures{};
};

[[nodiscard]] FrameCache& GetFrameCache() noexcept;
[[nodiscard]] Diagnostics& GetDiagnostics() noexcept;
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
void ProcessDiagnostics() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation
