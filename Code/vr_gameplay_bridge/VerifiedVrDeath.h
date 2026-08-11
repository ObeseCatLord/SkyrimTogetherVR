#pragma once

#include "pch.h"

namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrDeath
{
using CommandStatus = SkyrimTogetherVR::GameplayBridge::CommandStatus;

struct DeathPolicyTargets
{
    using SetNoBleedoutRecovery = void(RE::Actor*, bool);
    using SetActorBaseFlag = void(RE::TESActorBaseData*, RE::ACTOR_BASE_DATA::Flag, bool, bool);

    SetNoBleedoutRecovery* SetNoBleedout{};
    SetActorBaseFlag* SetBaseFlag{};
};

// Resolves both mutators together so a caller can validate all native targets
// before it changes any actor or base-form state.
[[nodiscard]] bool ResolveDeathPolicyTargets(DeathPolicyTargets& ar_targets) noexcept;

struct RespawnTargets
{
    using SetNoBleedoutRecovery = void(RE::Actor*, bool);
    using DispelAllSpells = void(RE::MagicTarget*, bool);
    using GetCocPlacementInfo = void(RE::TESObjectCELL*, RE::NiPoint3*, RE::NiPoint3*, bool);
    using MoveTo = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*,
                        const RE::NiPoint3&, const RE::NiPoint3&);

    SetNoBleedoutRecovery* SetNoBleedout{};
    DispelAllSpells* DispelAll{};
    GetCocPlacementInfo* GetCocPlacement{};
    MoveTo* MoveToCell{};
};

// Respawn is available only when every raw target has exact Skyrim VR 1.4.15
// prologue proof, so the literal desktop call order never observes a partial
// target set.
[[nodiscard]] bool ResolveRespawnTargets(RespawnTargets& ar_targets) noexcept;

[[nodiscard]] CommandStatus FadeScreen(
    bool a_fadingOut,
    bool a_blackFade,
    float a_fadeDuration,
    bool a_remainVisible,
    float a_secondsToFade) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrDeath
