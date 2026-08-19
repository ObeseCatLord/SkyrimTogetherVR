#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::ProgressionHooks
{
// These targets are independently verified against the pinned Skyrim VR
// 1.4.15.0 executable. Do not replace them with translated desktop IDs.
inline constexpr std::uint64_t kAddSkillExperienceVrAddressLibraryId = 39413;
inline constexpr std::uintptr_t kAddSkillExperienceVrRva = 0x06C30B0;
inline constexpr std::array<std::uint8_t, 23> kAddSkillExperienceVrPrologue{
    0x48, 0x83, 0xEC, 0x48, 0x0F, 0x57, 0xC0, 0x0F, 0x2F, 0xD0, 0x76, 0x1E, 0x8B, 0x44, 0x24, 0x70, 0x48, 0x8B, 0x89, 0xB0, 0x10, 0x00, 0x00,
};

inline constexpr std::uintptr_t kCalculateExperienceVrRva = 0x03F2FF0;
inline constexpr std::array<std::uint8_t, 16> kCalculateExperienceVrPrologue{
    0x81, 0xF9, 0xA3, 0x00, 0x00, 0x00, 0x77, 0x52, 0x48, 0x8B, 0x05, 0xC9, 0xFA, 0xB8, 0x01, 0x48,
};

inline constexpr std::uint32_t kFirstSkillActorValue = 6;
inline constexpr std::uint32_t kLastSkillActorValue = 23;

[[nodiscard]] constexpr bool IsCombatSkillActorValue(const std::uint32_t a_actorValue) noexcept
{
    return a_actorValue == 6 || a_actorValue == 7 || a_actorValue == 8 || a_actorValue == 9 || a_actorValue == 18 || a_actorValue == 19 || a_actorValue == 20 ||
           a_actorValue == 21 || a_actorValue == 22;
}

[[nodiscard]] inline bool IsValidExperienceDelta(const float a_delta, const float a_maximumExperience) noexcept
{
    return std::isfinite(a_delta) && a_delta > 0.0F && a_delta <= a_maximumExperience;
}

[[nodiscard]] constexpr bool ShouldSuppressExperienceCapture(const bool a_remoteApplication, const bool a_suppressionTokenConsumed) noexcept
{
    return a_remoteApplication || a_suppressionTokenConsumed;
}

[[nodiscard]] inline bool ShouldPublishExactExperience(
    const std::uint32_t a_actorValue, const float a_delta, const float a_maximumExperience, const bool a_remoteApplication, const bool a_suppressionTokenConsumed) noexcept
{
    return IsCombatSkillActorValue(a_actorValue) && !ShouldSuppressExperienceCapture(a_remoteApplication, a_suppressionTokenConsumed) &&
           IsValidExperienceDelta(a_delta, a_maximumExperience);
}

// Scope only the engine call that applies inbound party XP. The verified
// CalculateExperience detour reads this thread-local state after the engine
// computes its normal values and normalizes only the remote replay result.
class ScopedRemoteExperienceApplication final
{
public:
    ScopedRemoteExperienceApplication() noexcept;
    ~ScopedRemoteExperienceApplication() noexcept;

    ScopedRemoteExperienceApplication(const ScopedRemoteExperienceApplication&) = delete;
    ScopedRemoteExperienceApplication& operator=(const ScopedRemoteExperienceApplication&) = delete;
};

[[nodiscard]] bool IsRemoteExperienceApplication() noexcept;
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::ProgressionHooks
