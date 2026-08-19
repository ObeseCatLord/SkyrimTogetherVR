#pragma once

#include <array>
#include <cstdint>

namespace RE
{
class Sky;
class TESWeather;
}

namespace SkyrimTogetherVR::GameplayAdapter::WeatherNativeAccess
{
// Skyrim VR 1.4.15 native Sky weather entry points. These deliberately remain
// bridge-owned direct RVAs: CommonLib's desktop relocation IDs do not prove the
// VR targets or their ForceWeather/ReleaseWeatherOverride semantics.
inline constexpr std::uintptr_t kForceWeatherVrRva = 0x03C48C0;
inline constexpr std::uintptr_t kReleaseWeatherOverrideVrRva = 0x03C4970;
inline constexpr bool kForceWeatherOverrideArgument = true;

inline constexpr std::array<std::uint8_t, 25> kForceWeatherVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x0F, 0xB6, 0xD8, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0xF9,
};

inline constexpr std::array<std::uint8_t, 25> kReleaseWeatherOverrideVrPrologue{
    0x48, 0x83, 0x79, 0x60, 0x00, 0x74, 0x12, 0x81,
    0x89, 0xDC, 0x01, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x00, 0x48, 0xC7, 0x41, 0x60, 0x00, 0x00, 0x00,
    0x00,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kForceWeatherVrRva == 0x03C48C0 &&
           kReleaseWeatherOverrideVrRva == 0x03C4970 &&
           kForceWeatherOverrideArgument &&
           kForceWeatherVrPrologue.size() == 25 &&
           kReleaseWeatherOverrideVrPrologue.size() == 25;
}

// This is intentionally exposed for load-time preflight. Calls validate again
// so a failed preflight can never be treated as runtime authorization.
[[nodiscard]] bool ValidateTargets() noexcept;
[[nodiscard]] bool ForceWeather(RE::Sky& a_sky, RE::TESWeather& a_weather) noexcept;
[[nodiscard]] bool ReleaseWeatherOverride(RE::Sky& a_sky) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::WeatherNativeAccess
