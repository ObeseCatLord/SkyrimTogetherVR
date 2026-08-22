#pragma once

#include <array>
#include <cstdint>

namespace RE
{
class TESBoundObject;
class TESObjectREFR;
}

namespace SkyrimTogetherVR::GameplayAdapter::ActivationHookPolicy
{
// These direct values were verified against the pinned Skyrim VR 1.4.15.0
// executable. The project override now curates ID 19796 to this same body,
// but the hook deliberately resolves this direct RVA rather than trusting any
// raw/generated address-library lookup.
inline constexpr std::uintptr_t kActivateRefVrRva = 0x002A8300;
inline constexpr std::uint64_t kCuratedActivateRefOverrideId = 19796;
inline constexpr std::uintptr_t kCuratedActivateRefOverrideRva = 0x002A8300;
inline constexpr std::uintptr_t kLegacyBaseCsvActivateRefVrRva = 0x002B8310;
inline constexpr std::array<std::uint8_t, 32> kActivateRefVrPrologue{
    0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x44,
    0x88, 0x40, 0x18, 0x55, 0x56, 0x57, 0x48, 0x8D,
    0x68, 0xB1, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00,
    0x00, 0x48, 0xC7, 0x45, 0x17, 0xFE, 0xFF, 0xFF,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kActivateRefVrRva == 0x002A8300 &&
           kCuratedActivateRefOverrideId == 19796 &&
           kCuratedActivateRefOverrideRva == kActivateRefVrRva &&
           kLegacyBaseCsvActivateRefVrRva == 0x002B8310 &&
           kActivateRefVrPrologue.size() == 32;
}

[[nodiscard]] constexpr bool UsesDirectRvaHookTarget() noexcept
{
    return true;
}

// The capture decision is deliberately data-only so policy tests do not need
// the game runtime. A failed publication never changes this decision; the
// native activation always proceeds through its trampoline.
[[nodiscard]] constexpr bool ShouldCapturePreActivation(
    const bool a_localPlayerActivator,
    const bool a_validTarget,
    const bool a_validBase,
    const bool a_validCell,
    const bool a_isBook) noexcept
{
    return a_localPlayerActivator && a_validTarget && a_validBase && a_validCell && !a_isBook;
}

[[nodiscard]] constexpr bool MustCaptureBeforeOriginal() noexcept
{
    return true;
}

[[nodiscard]] constexpr bool MustPublishAfterSuccessfulOriginal(const bool a_originalAccepted) noexcept
{
    return a_originalAccepted;
}

[[nodiscard]] constexpr bool IsExactlyOneOriginalCallPolicy(
    const bool a_hasTrampoline,
    const std::uint32_t a_originalCallCount) noexcept
{
    return a_hasTrampoline && a_originalCallCount == 1;
}

[[nodiscard]] constexpr bool UsesTesActivateEventSink() noexcept
{
    return false;
}

[[nodiscard]] constexpr bool ShouldRetainTrampolineOnDetachFailure() noexcept
{
    return true;
}

[[nodiscard]] constexpr bool ShouldClearHookStateAfterDetach(const bool a_detached) noexcept
{
    return a_detached;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActivationHookPolicy

namespace SkyrimTogetherVR::GameplayAdapter::ActivationHooks
{
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::ActivationHooks
