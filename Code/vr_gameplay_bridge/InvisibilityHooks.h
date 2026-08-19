#pragma once

#include <array>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::InvisibilityHookPolicy
{
// This direct entry point is verified against the pinned Skyrim VR 1.4.15.0
// executable. The generated desktop address-library row is not valid for VR.
inline constexpr std::uintptr_t kInvisibilityEffectFinishVrRva = 0x0054F500;
inline constexpr std::array<std::uint8_t, 27> kInvisibilityEffectFinishVrPrologue{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9,
    0xE8, 0xA2, 0xEA, 0x01, 0x00, 0x48, 0x8B, 0x4F, 0x50,
    0x48, 0x85, 0xC9, 0x0F, 0x84, 0xD2, 0x00, 0x00, 0x00,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kInvisibilityEffectFinishVrRva == 0x0054F500 &&
           kInvisibilityEffectFinishVrPrologue.size() == 27;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::InvisibilityHookPolicy

namespace SkyrimTogetherVR::GameplayAdapter::InvisibilityHooks
{
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::InvisibilityHooks
