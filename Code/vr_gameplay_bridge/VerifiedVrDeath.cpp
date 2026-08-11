#include "VerifiedVrDeath.h"

#include <array>
#include <cstring>

namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrDeath
{
namespace
{
constexpr std::uint64_t kSetActorBaseFlagVrRva = 0x019C3B0;
// The generated VR row for the desktop ID 38533 is a getter. This is the
// separately mapped Actor::SetNoBleedoutRecovery setter.
constexpr std::uint64_t kSetNoBleedoutRecoveryVrRva = 0x062C950;
constexpr std::uint64_t kDispelAllSpellsVrRva = 0x0557070;
constexpr std::uint64_t kGetCocPlacementInfoVrRva = 0x027A4C0;
constexpr std::uint64_t kMoveToVrRva = 0x09E90E0;
constexpr std::uint64_t kFadeOutGameVrRva = 0x0903080;
constexpr REL::Version kSkyrimVrRuntime{1, 4, 15, 0};

constexpr std::array<std::uint8_t, 16> kSetActorBaseFlagVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x85,
};
constexpr std::array<std::uint8_t, 16> kSetNoBleedoutRecoveryVrPrologue{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x81,
    0xFC, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x8B,
};
constexpr std::array<std::uint8_t, 16> kDispelAllSpellsVrPrologue{
    0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x48,
    0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 16> kGetCocPlacementInfoVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
};
constexpr std::array<std::uint8_t, 16> kMoveToVrPrologue{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};
constexpr std::array<std::uint8_t, 16> kFadeOutGameVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44,
};

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kSkyrimVrRuntime;
}

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    if (!IsExpectedVrRuntime())
        return false;

    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (a_address < text.address() || a_address - text.address() >= text.size())
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0;
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedExecutableTarget(
    const std::uintptr_t a_address,
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    if (!IsExpectedVrRuntime())
        return false;

    const auto text = REL::Module::get().segment(REL::Segment::textx);
    return text.size() >= a_prologue.size() && a_address >= text.address() &&
           a_address - text.address() <= text.size() - a_prologue.size() &&
           IsExecutableTarget(a_address) &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
}

} // namespace

bool ResolveDeathPolicyTargets(DeathPolicyTargets& ar_targets) noexcept
{
    ar_targets = {};

    static REL::Relocation<DeathPolicyTargets::SetNoBleedoutRecovery> setNoBleedoutRecovery{
        REL::Offset(kSetNoBleedoutRecoveryVrRva)};
    static REL::Relocation<DeathPolicyTargets::SetActorBaseFlag> setActorBaseFlag{
        REL::Offset(kSetActorBaseFlagVrRva)};
    if (setNoBleedoutRecovery.offset() != kSetNoBleedoutRecoveryVrRva ||
        setActorBaseFlag.offset() != kSetActorBaseFlagVrRva ||
        !IsVerifiedExecutableTarget(setNoBleedoutRecovery.address(), kSetNoBleedoutRecoveryVrPrologue) ||
        !IsVerifiedExecutableTarget(setActorBaseFlag.address(), kSetActorBaseFlagVrPrologue))
        return false;

    ar_targets = {
        .SetNoBleedout = setNoBleedoutRecovery.get(),
        .SetBaseFlag = setActorBaseFlag.get(),
    };
    return true;
}

bool ResolveRespawnTargets(RespawnTargets& ar_targets) noexcept
{
    ar_targets = {};
    if (!IsExpectedVrRuntime())
        return false;

    static REL::Relocation<RespawnTargets::SetNoBleedoutRecovery> setNoBleedoutRecovery{
        REL::Offset(kSetNoBleedoutRecoveryVrRva)};
    static REL::Relocation<RespawnTargets::DispelAllSpells> dispelAllSpells{
        REL::Offset(kDispelAllSpellsVrRva)};
    static REL::Relocation<RespawnTargets::GetCocPlacementInfo> getCocPlacementInfo{
        REL::Offset(kGetCocPlacementInfoVrRva)};
    static REL::Relocation<RespawnTargets::MoveTo> moveTo{
        REL::Offset(kMoveToVrRva)};

    if (setNoBleedoutRecovery.offset() != kSetNoBleedoutRecoveryVrRva ||
        dispelAllSpells.offset() != kDispelAllSpellsVrRva ||
        getCocPlacementInfo.offset() != kGetCocPlacementInfoVrRva ||
        moveTo.offset() != kMoveToVrRva ||
        !IsVerifiedExecutableTarget(setNoBleedoutRecovery.address(), kSetNoBleedoutRecoveryVrPrologue) ||
        !IsVerifiedExecutableTarget(dispelAllSpells.address(), kDispelAllSpellsVrPrologue) ||
        !IsVerifiedExecutableTarget(getCocPlacementInfo.address(), kGetCocPlacementInfoVrPrologue) ||
        !IsVerifiedExecutableTarget(moveTo.address(), kMoveToVrPrologue))
        return false;

    ar_targets = {
        .SetNoBleedout = setNoBleedoutRecovery.get(),
        .DispelAll = dispelAllSpells.get(),
        .GetCocPlacement = getCocPlacementInfo.get(),
        .MoveToCell = moveTo.get(),
    };
    return true;
}

CommandStatus FadeScreen(
    const bool a_fadingOut,
    const bool a_blackFade,
    const float a_fadeDuration,
    const bool a_remainVisible,
    const float a_secondsToFade) noexcept
{
    using FadeOutGame = void(bool, bool, float, bool, float);
    static REL::Relocation<FadeOutGame> fadeOutGame{REL::Offset(kFadeOutGameVrRva)};
    if (fadeOutGame.offset() != kFadeOutGameVrRva ||
        !IsVerifiedExecutableTarget(fadeOutGame.address(), kFadeOutGameVrPrologue))
        return CommandStatus::Unsupported;

    fadeOutGame(a_fadingOut, a_blackFade, a_fadeDuration, a_remainVisible, a_secondsToFade);
    return CommandStatus::Success;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrDeath
