#pragma once

#include <array>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHookPolicy
{
enum class Disposition : std::uint8_t
{
    CallOriginal,
    Suppress,
};

// This is the direct Skyrim VR 1.4.15.0 SummonCreatureEffect factory body.
// Do not resolve a generated address-library ID for this target.
inline constexpr std::uintptr_t kSummonCreatureEffectFactoryVrRva = 0x0569920;
inline constexpr std::uintptr_t kSummonCreatureEffectFactoryExtent = 0xB1;
inline constexpr std::array<std::uint8_t, 37> kSummonCreatureEffectFactoryVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89,
    0x5C, 0x24, 0x50, 0x48, 0x89, 0x6C, 0x24, 0x58, 0x49, 0x8B,
    0xF0, 0x48, 0x8B, 0xEA, 0x4C, 0x8B, 0xF1,
};

// The registration thunk's immediate loads independently bind the factory to
// archetype 0x12 without dereferencing any game-owned registration objects.
inline constexpr std::uintptr_t kSummonCreatureEffectRegistrationThunkVrRva = 0x00902B0;
inline constexpr std::uintptr_t kSummonCreatureEffectRegistrationFactoryLoadVrRva = 0x00902BE;
inline constexpr std::uintptr_t kSummonCreatureEffectRegistrationArchetypeLoadVrRva = 0x00902D1;
inline constexpr std::uintptr_t kSummonCreatureEffectRegistrationHelperVrRva = 0x0556930;
inline constexpr std::uint8_t kSummonCreatureEffectArchetype = 0x12;

[[nodiscard]] constexpr bool HasPinnedFactoryTargetConfiguration() noexcept
{
    return kSummonCreatureEffectFactoryVrRva == 0x0569920 &&
           kSummonCreatureEffectFactoryExtent == 0xB1 &&
           kSummonCreatureEffectFactoryExtent >= kSummonCreatureEffectFactoryVrPrologue.size() &&
           kSummonCreatureEffectFactoryVrPrologue.size() == 37;
}

[[nodiscard]] constexpr bool HasPinnedFactoryRegistrationContract() noexcept
{
    return kSummonCreatureEffectRegistrationThunkVrRva == 0x00902B0 &&
           kSummonCreatureEffectRegistrationFactoryLoadVrRva == 0x00902BE &&
           kSummonCreatureEffectRegistrationFactoryLoadVrRva - kSummonCreatureEffectRegistrationThunkVrRva == 0xE &&
           kSummonCreatureEffectRegistrationArchetypeLoadVrRva == 0x00902D1 &&
           kSummonCreatureEffectRegistrationArchetypeLoadVrRva - kSummonCreatureEffectRegistrationThunkVrRva == 0x21 &&
           kSummonCreatureEffectRegistrationHelperVrRva == 0x0556930 &&
           kSummonCreatureEffectArchetype == 0x12;
}

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return HasPinnedFactoryTargetConfiguration() && HasPinnedFactoryRegistrationContract();
}

[[nodiscard]] constexpr bool UsesDirectRvaFactoryTarget() noexcept
{
    return true;
}

// Retiring is independently suppressible because the retirement lease has
// closed admission before it is safe for the actor to receive new summons.
[[nodiscard]] constexpr Disposition Classify(
    const bool a_managedRemoteCaster,
    const bool a_retiringCaster) noexcept
{
    return a_managedRemoteCaster || a_retiringCaster ? Disposition::Suppress : Disposition::CallOriginal;
}

[[nodiscard]] constexpr bool MustUseManagedRemoteActorLease() noexcept
{
    return true;
}

[[nodiscard]] constexpr bool RemoteMagicApplicationBypassesSuppression() noexcept
{
    return false;
}

[[nodiscard]] constexpr bool DispatcherAcceptsNullFactoryResult() noexcept
{
    return true;
}

[[nodiscard]] constexpr bool NullFactoryResultSkipsPostInitialization() noexcept
{
    return true;
}

[[nodiscard]] constexpr bool IsExactlyOneOriginalCallPolicy(
    const Disposition a_disposition,
    const bool a_hasTrampoline,
    const std::uint32_t a_originalCallCount) noexcept
{
    return a_disposition == Disposition::Suppress ? a_originalCallCount == 0 :
                                                     a_hasTrampoline && a_originalCallCount == 1;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHookPolicy

namespace SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHooks
{
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHooks
