#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::DropHookPolicy
{
// The PlayerCharacter vtable at 0x16E2230 resolves slot 0xCD to this exact
// override in the pinned Skyrim VR 1.4.15.0 executable. The hook resolves the
// direct body only; it never follows a generated address-library alias.
inline constexpr std::uintptr_t kPlayerCharacterVtableVrRva = 0x016E2230;
inline constexpr std::size_t kPlayerCharacterDropObjectVtableSlot = 0xCD;
inline constexpr std::uintptr_t kPlayerDropObjectVrRva = 0x006C00F0;
inline constexpr std::uintptr_t kPlayerCharacterDropObjectVtableEntryRva =
    kPlayerCharacterVtableVrRva + kPlayerCharacterDropObjectVtableSlot * sizeof(std::uintptr_t);
inline constexpr std::uintptr_t kForbiddenUnrelatedVrRva = 0x00709970;
inline constexpr std::array<std::uint8_t, 19> kPlayerDropObjectVrPrologue{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
};

struct PendingDrop
{
    std::uintptr_t ActorAddress{};
    std::uintptr_t ObjectAddress{};
    std::uint32_t ActorFormId{};
    std::uint32_t ObjectFormId{};
    std::uint16_t StableUniqueId{};
    std::int32_t Count{};
    std::uint64_t Generation{};
    bool HasStableUniqueId{};
    bool Consumed{};
};

struct ContainerChangedEvent
{
    std::uint32_t OldContainer{};
    std::uint32_t NewContainer{};
    std::uint32_t ObjectFormId{};
    std::uint16_t UniqueId{};
    std::int32_t Count{};
};

enum class ContainerChangedDisposition : std::uint8_t
{
    NormalRemoval,
    MatchedDrop,
    RemoteSuppressed,
    NestedAmbiguity,
    Mismatch,
    AlreadyConsumed,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kPlayerCharacterVtableVrRva == 0x016E2230 &&
           kPlayerCharacterDropObjectVtableSlot == 0xCD &&
           kPlayerCharacterDropObjectVtableEntryRva == 0x016E2898 &&
           kPlayerDropObjectVrRva == 0x006C00F0 &&
           kPlayerDropObjectVrRva != kForbiddenUnrelatedVrRva &&
           kPlayerDropObjectVrPrologue.size() == 19;
}

[[nodiscard]] constexpr bool UsesDirectRvaHookTarget() noexcept
{
    return true;
}

[[nodiscard]] constexpr bool IsExactPlayerDropObjectTarget(const std::uintptr_t a_rva) noexcept
{
    return a_rva == kPlayerDropObjectVrRva;
}

[[nodiscard]] constexpr bool IsExactPendingDropMatch(
    const PendingDrop& a_pending,
    const ContainerChangedEvent& a_event) noexcept
{
    if (a_pending.ActorAddress == 0 || a_pending.ObjectAddress == 0 || a_pending.ActorFormId == 0 ||
        a_pending.ObjectFormId == 0 || a_pending.Count <= 0 || a_pending.Generation == 0 ||
        a_event.OldContainer != a_pending.ActorFormId || a_event.NewContainer != 0 ||
        a_event.ObjectFormId != a_pending.ObjectFormId || a_event.Count != a_pending.Count)
        return false;

    if (a_pending.HasStableUniqueId)
        return a_pending.StableUniqueId != 0 && a_event.UniqueId == a_pending.StableUniqueId;
    return a_event.UniqueId == 0;
}

// This remains data-only so the event policy can be tested without loading
// SkyrimVR.exe. Remote replay is checked first, and every uncertain branch is
// intentionally a normal inventory removal rather than an attributed drop.
[[nodiscard]] constexpr ContainerChangedDisposition ClassifyContainerChangedEvent(
    const PendingDrop* ap_pending,
    const ContainerChangedEvent& a_event,
    const bool a_remoteSuppressed,
    const bool a_nestedAmbiguity) noexcept
{
    if (a_remoteSuppressed)
        return ContainerChangedDisposition::RemoteSuppressed;
    if (!ap_pending)
        return ContainerChangedDisposition::NormalRemoval;
    if (a_nestedAmbiguity)
        return ContainerChangedDisposition::NestedAmbiguity;
    if (ap_pending->Consumed)
        return ContainerChangedDisposition::AlreadyConsumed;
    return IsExactPendingDropMatch(*ap_pending, a_event) ?
               ContainerChangedDisposition::MatchedDrop :
               ContainerChangedDisposition::Mismatch;
}

[[nodiscard]] constexpr bool ShouldPublishInventoryDrop(
    const ContainerChangedDisposition a_disposition) noexcept
{
    return a_disposition == ContainerChangedDisposition::MatchedDrop;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DropHookPolicy

namespace SkyrimTogetherVR::GameplayAdapter::DropHooks
{
// Called synchronously by LocalGameplayCapture's TESContainerChangedEvent
// sink. A MatchedDrop has already consumed its current thread-local scope, so
// duplicate container events cannot acquire the drop bit a second time.
[[nodiscard]] DropHookPolicy::ContainerChangedDisposition ConsumeContainerChangedEvent(
    const DropHookPolicy::ContainerChangedEvent& a_event,
    bool a_remoteSuppressed) noexcept;

[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::DropHooks
