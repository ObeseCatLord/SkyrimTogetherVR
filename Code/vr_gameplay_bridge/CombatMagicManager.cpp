#include "CombatMagicManager.h"

#include "AvatarManager.h"
#include "MagicHooks.h"
#include "ProjectileHooks.h"

#include <vr_common/VRCanonicalEntity.h>

#include <cstddef>
#include <algorithm>
#include <cmath>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::uint32_t kMeleeHitPlanckAlreadyApplied = 1u << 0;
constexpr std::uint32_t kMagicCastNoHitEffectArt = 1u << 0;
constexpr std::uint32_t kMagicCastHostileEffectivenessOnly = 1u << 1;
constexpr std::uint32_t kMagicCastDual = 1u << 2;
constexpr std::uint32_t kMagicEffectAreaTarget = 1u << 0;
constexpr std::uint32_t kMagicEffectDualCasted = 1u << 1;

[[nodiscard]] bool HasOnlyFlags(const std::uint32_t a_flags, const std::uint32_t a_knownFlags) noexcept
{
    return (a_flags & ~a_knownFlags) == 0;
}

[[nodiscard]] bool HasFiniteScalars(const GameplayActionPayload& a_payload) noexcept
{
    return std::isfinite(a_payload.ScalarA) && std::isfinite(a_payload.ScalarB) &&
           std::isfinite(a_payload.ScalarC) && std::isfinite(a_payload.ScalarD);
}

[[nodiscard]] bool IsCastingSource(const std::int32_t a_value) noexcept
{
    using CastingSource = RE::MagicSystem::CastingSource;
    return a_value == static_cast<std::int32_t>(CastingSource::kLeftHand) ||
           a_value == static_cast<std::int32_t>(CastingSource::kRightHand) ||
           a_value == static_cast<std::int32_t>(CastingSource::kOther) ||
           a_value == static_cast<std::int32_t>(CastingSource::kInstant);
}

[[nodiscard]] bool IsMagicEffectCastingSource(const std::int32_t a_value) noexcept
{
    return IsCastingSource(a_value) ||
           a_value == static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kTotal);
}

[[nodiscard]] RE::MagicSystem::CastingSource ToCastingSource(const std::int32_t a_value) noexcept
{
    return static_cast<RE::MagicSystem::CastingSource>(a_value);
}

template <class T>
[[nodiscard]] T* ResolveLocalForm(const std::uint32_t a_formId) noexcept
{
    return a_formId != 0 ? RE::TESForm::LookupByID<T>(a_formId) : nullptr;
}

[[nodiscard]] CommandStatus ValidateCommand(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    const auto domain = static_cast<GameplayDomain>(payload.Domain);
    const auto action = static_cast<GameplayAction>(payload.Action);

    if (a_command.Header.Kind != static_cast<std::uint16_t>(CommandKind::ApplyGameplayAction) ||
        a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 || identity.Reserved0 != 0 ||
        identity.SequenceId != 0 || identity.ActionId == 0 || payload.TargetHandle.Value == 0 || payload.Reserved0 != 0 ||
        !std::all_of(std::begin(payload.ReservedTail), std::end(payload.ReservedTail), [](std::uint8_t a_value) { return a_value == 0; }) ||
        !CanonicalEntity::IsValid(identity.EntityId, identity.EntityGeneration) || !HasFiniteScalars(payload) ||
        !IsActionInDomain(domain, action))
        return CommandStatus::Malformed;

    switch (domain) {
    case GameplayDomain::Combat:
        return action == GameplayAction::MeleeHit || action == GameplayAction::SetCombatTarget ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayDomain::Projectile:
        return action == GameplayAction::LaunchProjectile ? CommandStatus::Success : CommandStatus::Malformed;
    case GameplayDomain::Magic:
        return action >= GameplayAction::CastSpell && action <= GameplayAction::RemoveSpell ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    default:
        return CommandStatus::Unsupported;
    }
}

[[nodiscard]] CommandStatus ValidateCanonicalTarget(
    const GameplayActionPayload& a_payload,
    const RE::Actor& a_actor) noexcept
{
    if (a_payload.TargetLocalFormId == 0)
        return CommandStatus::Success;

    const auto* localTarget = ResolveLocalForm<RE::Actor>(a_payload.TargetLocalFormId);
    return localTarget == &a_actor ? CommandStatus::Success : CommandStatus::InvalidHandle;
}

[[nodiscard]] CommandStatus ValidateActionLayout(const GameplayActionPayload& a_payload) noexcept
{
    switch (static_cast<GameplayAction>(a_payload.Action)) {
    case GameplayAction::MeleeHit:
        return a_payload.LocalFormIdA != 0 && a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
                   a_payload.SecondaryHandle.Value == 0 &&
                   a_payload.ValueB == 0 && a_payload.ScalarB == 0.0f && a_payload.ScalarC == 0.0f &&
                   a_payload.ScalarD == 0.0f && a_payload.ScalarA >= 0.0f &&
                   HasOnlyFlags(a_payload.ActionFlags, kMeleeHitPlanckAlreadyApplied) ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::SetCombatTarget:
        return a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
                   a_payload.ValueA == 0 && a_payload.ValueB == 0 && a_payload.ScalarA == 0.0f &&
                   a_payload.ScalarB == 0.0f && a_payload.ScalarC == 0.0f && a_payload.ScalarD == 0.0f &&
                   a_payload.ActionFlags == 0 ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::LaunchProjectile:
        return a_payload.SecondaryHandle.Value == 0 && IsCastingSource(a_payload.ValueA) &&
                   a_payload.ValueB >= 0 && a_payload.ScalarD >= 0.0f &&
                   HasOnlyFlags(a_payload.ActionFlags, 0xFu) ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::CastSpell:
        return a_payload.LocalFormIdA != 0 && a_payload.LocalFormIdC == 0 && a_payload.LocalFormIdD == 0 &&
                   a_payload.ValueB == 0 && a_payload.ScalarA >= 0.0f && IsCastingSource(a_payload.ValueA) &&
                   a_payload.SecondaryHandle.Value == 0 &&
                   HasOnlyFlags(a_payload.ActionFlags, kMagicCastNoHitEffectArt | kMagicCastHostileEffectivenessOnly | kMagicCastDual) ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::InterruptCast:
        return a_payload.LocalFormIdA == 0 && a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 &&
                   a_payload.LocalFormIdD == 0 && a_payload.ValueB == 0 && a_payload.ScalarA == 0.0f &&
                   a_payload.ScalarB == 0.0f && a_payload.ScalarC == 0.0f && a_payload.ScalarD == 0.0f &&
                   a_payload.SecondaryHandle.Value == 0 && IsCastingSource(a_payload.ValueA) && a_payload.ActionFlags == 0 ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::ApplyMagicEffect:
        return a_payload.LocalFormIdA != 0 && a_payload.LocalFormIdB != 0 && a_payload.LocalFormIdC == 0 &&
                   a_payload.LocalFormIdD == 0 && a_payload.ValueB >= 0 && a_payload.ScalarA >= 0.0f &&
                   a_payload.ScalarB >= 0.0f && a_payload.ScalarC == 0.0f && a_payload.ScalarD == 0.0f &&
                   IsMagicEffectCastingSource(a_payload.ValueA) &&
                   HasOnlyFlags(a_payload.ActionFlags, kMagicEffectAreaTarget | kMagicEffectDualCasted) ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    case GameplayAction::AddSpell:
    case GameplayAction::RemoveSpell:
        return a_payload.LocalFormIdA != 0 && a_payload.LocalFormIdB == 0 && a_payload.LocalFormIdC == 0 &&
                   a_payload.LocalFormIdD == 0 && a_payload.ValueA == 0 && a_payload.ValueB == 0 &&
                   a_payload.ScalarA == 0.0f && a_payload.ScalarB == 0.0f && a_payload.ScalarC == 0.0f &&
                   a_payload.ScalarD == 0.0f && a_payload.ActionFlags == 0 && a_payload.SecondaryHandle.Value == 0 ?
                   CommandStatus::Success :
                   CommandStatus::Malformed;
    default:
        return CommandStatus::Malformed;
    }
}

[[nodiscard]] CommandStatus ExecuteMeleeHit(const GameplayActionPayload& a_payload, RE::Actor& a_source) noexcept
{
    if (a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        a_payload.ValueB != 0 || a_payload.ScalarB != 0.0f || a_payload.ScalarC != 0.0f || a_payload.ScalarD != 0.0f ||
        a_payload.ScalarA < 0.0f || !HasOnlyFlags(a_payload.ActionFlags, kMeleeHitPlanckAlreadyApplied))
        return CommandStatus::Malformed;

    auto* target = ResolveLocalForm<RE::Actor>(a_payload.LocalFormIdA);
    if (!target)
        return CommandStatus::MissingForm;
    if (a_payload.LocalFormIdB != 0 && !ResolveLocalForm<RE::TESObjectWEAP>(a_payload.LocalFormIdB))
        return CommandStatus::MissingForm;

    // PLANCK (or the native collision path) is authoritative when it already
    // applied this action. The canonical action-id ledger prevents replaying a
    // second direct-health mutation for the same remote hit.
    if ((a_payload.ActionFlags & kMeleeHitPlanckAlreadyApplied) != 0)
        return CommandStatus::Success;

    target->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -a_payload.ScalarA);
    (void)a_source;
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ExecuteCombatTarget(const CommandRecord& a_command, RE::Actor& a_actor) noexcept
{
    const auto& a_payload = a_command.Payload.ApplyGameplayAction;
    if (a_payload.LocalFormIdB != 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        a_payload.ValueA != 0 || a_payload.ValueB != 0 || a_payload.ScalarA != 0.0f || a_payload.ScalarB != 0.0f ||
        a_payload.ScalarC != 0.0f || a_payload.ScalarD != 0.0f || a_payload.ActionFlags != 0)
        return CommandStatus::Malformed;

    if (a_payload.LocalFormIdA == 0 && a_payload.SecondaryHandle.Value == 0) {
        a_actor.StopCombat();
        return CommandStatus::Success;
    }

    RE::NiPointer<RE::Actor> target;
    if (a_payload.SecondaryHandle.Value != 0) {
        const auto status = AvatarManager::Get().ResolveActorByHandle(
            a_command.Header.Identity, a_payload.SecondaryHandle, target);
        if (status != CommandStatus::Success)
            return status;
    } else {
        target.reset(ResolveLocalForm<RE::Actor>(a_payload.LocalFormIdA));
    }
    return target && a_actor.StartCombat(target.get()) ? CommandStatus::Success : CommandStatus::MissingForm;
}

[[nodiscard]] CommandStatus ExecuteCastSpell(const GameplayActionPayload& a_payload, RE::Actor& a_actor) noexcept
{
    if (a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 || a_payload.ValueB != 0 ||
        a_payload.ScalarA < 0.0f || !IsCastingSource(a_payload.ValueA) ||
        !HasOnlyFlags(a_payload.ActionFlags, kMagicCastNoHitEffectArt | kMagicCastHostileEffectivenessOnly | kMagicCastDual))
        return CommandStatus::Malformed;

    auto* spell = ResolveLocalForm<RE::MagicItem>(a_payload.LocalFormIdA);
    if (!spell)
        return CommandStatus::MissingForm;
    auto* target = a_payload.LocalFormIdB != 0 ? ResolveLocalForm<RE::TESObjectREFR>(a_payload.LocalFormIdB) : &a_actor;
    if (!target)
        return CommandStatus::MissingForm;
    auto* caster = a_actor.GetMagicCaster(ToCastingSource(a_payload.ValueA));
    if (!caster)
        return CommandStatus::EngineRejected;
    caster->SetDualCasting((a_payload.ActionFlags & kMagicCastDual) != 0);

    MagicHooks::ScopedRemoteMagicApplication suppressEcho;
    caster->CastSpellImmediate(
        spell,
        (a_payload.ActionFlags & kMagicCastNoHitEffectArt) != 0,
        target,
        a_payload.ScalarA,
        (a_payload.ActionFlags & kMagicCastHostileEffectivenessOnly) != 0,
        a_payload.ScalarB,
        &a_actor);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ExecuteInterruptCast(const GameplayActionPayload& a_payload, RE::Actor& a_actor) noexcept
{
    if (a_payload.LocalFormIdA != 0 || a_payload.LocalFormIdB != 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        a_payload.ValueB != 0 || a_payload.ScalarA != 0.0f || a_payload.ScalarB != 0.0f || a_payload.ScalarC != 0.0f ||
        a_payload.ScalarD != 0.0f || !IsCastingSource(a_payload.ValueA) || a_payload.ActionFlags != 0)
        return CommandStatus::Malformed;

    auto* caster = a_actor.GetMagicCaster(ToCastingSource(a_payload.ValueA));
    if (!caster)
        return CommandStatus::EngineRejected;
    MagicHooks::ScopedRemoteMagicApplication suppressEcho;
    caster->InterruptCast(true);
    return CommandStatus::Success;
}

[[nodiscard]] CommandStatus ExecuteMagicEffect(const CommandRecord& a_command, RE::Actor& a_actor) noexcept
{
    const auto& a_payload = a_command.Payload.ApplyGameplayAction;
    if (a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdB == 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        a_payload.ValueB < 0 || a_payload.ScalarA < 0.0f || a_payload.ScalarB < 0.0f ||
        a_payload.ScalarC != 0.0f || a_payload.ScalarD != 0.0f || !IsMagicEffectCastingSource(a_payload.ValueA) ||
        !HasOnlyFlags(a_payload.ActionFlags, kMagicEffectAreaTarget | kMagicEffectDualCasted))
        return CommandStatus::Malformed;

    auto* spell = ResolveLocalForm<RE::MagicItem>(a_payload.LocalFormIdA);
    if (!spell)
        return CommandStatus::MissingForm;
    RE::Effect* effect{};
    for (auto* candidate : spell->effects) {
        if (candidate && candidate->baseEffect && candidate->baseEffect->GetFormID() == a_payload.LocalFormIdB) {
            effect = candidate;
            break;
        }
    }
    if (!effect)
        return CommandStatus::MissingForm;
    auto* target = a_actor.GetMagicTarget();
    if (!target)
        return CommandStatus::EngineRejected;

    RE::NiPointer<RE::Actor> caster;
    if (a_payload.SecondaryHandle.Value != 0) {
        const auto casterStatus = AvatarManager::Get().ResolveActorByHandle(
            a_command.Header.Identity, a_payload.SecondaryHandle, caster);
        if (casterStatus != CommandStatus::Success)
            return casterStatus;
    }

    RE::MagicTarget::AddTargetData data{};
    data.caster = caster.get();
    data.magicItem = spell;
    data.effect = effect;
    data.explosionPoint = a_actor.GetPosition();
    data.magnitude = a_payload.ScalarA;
    data.power = a_payload.ScalarB;
    data.castingSource = ToCastingSource(a_payload.ValueA);
    data.areaTarget = (a_payload.ActionFlags & kMagicEffectAreaTarget) != 0;
    data.dualCasted = (a_payload.ActionFlags & kMagicEffectDualCasted) != 0;
    MagicHooks::ScopedRemoteMagicApplication suppressEcho;
    return target->AddTarget(data) ? CommandStatus::Success : CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ExecuteAddRemoveSpell(const GameplayActionPayload& a_payload, RE::Actor& a_actor) noexcept
{
    if (a_payload.LocalFormIdA == 0 || a_payload.LocalFormIdB != 0 || a_payload.LocalFormIdC != 0 || a_payload.LocalFormIdD != 0 ||
        a_payload.ValueA != 0 || a_payload.ValueB != 0 || a_payload.ScalarA != 0.0f || a_payload.ScalarB != 0.0f ||
        a_payload.ScalarC != 0.0f || a_payload.ScalarD != 0.0f || a_payload.ActionFlags != 0)
        return CommandStatus::Malformed;
    auto* spell = ResolveLocalForm<RE::SpellItem>(a_payload.LocalFormIdA);
    if (!spell)
        return CommandStatus::MissingForm;
    const auto action = static_cast<GameplayAction>(a_payload.Action);
    return (action == GameplayAction::AddSpell ? a_actor.AddSpell(spell) : a_actor.RemoveSpell(spell)) ?
               CommandStatus::Success : CommandStatus::EngineRejected;
}
} // namespace

CommandStatus ExecuteCombatMagicAction(const CommandRecord& a_command) noexcept
{
    try {
        const auto validation = ValidateCommand(a_command);
        if (validation != CommandStatus::Success)
            return validation;
        const auto layoutValidation = ValidateActionLayout(a_command.Payload.ApplyGameplayAction);
        if (layoutValidation != CommandStatus::Success)
            return layoutValidation;

        RE::NiPointer<RE::Actor> actor;
        const auto resolved = AvatarManager::Get().ResolveGameplayActor(a_command, actor);
        if (resolved != CommandStatus::Success)
            return resolved;

        const auto targetValidation = ValidateCanonicalTarget(a_command.Payload.ApplyGameplayAction, *actor);
        if (targetValidation != CommandStatus::Success)
            return targetValidation;

        const auto& payload = a_command.Payload.ApplyGameplayAction;
        switch (static_cast<GameplayAction>(payload.Action)) {
        case GameplayAction::MeleeHit:
            return ExecuteMeleeHit(payload, *actor);
        case GameplayAction::SetCombatTarget:
            return ExecuteCombatTarget(a_command, *actor);
        case GameplayAction::LaunchProjectile:
            return CommandStatus::Unsupported;
        case GameplayAction::CastSpell:
            return ExecuteCastSpell(payload, *actor);
        case GameplayAction::InterruptCast:
            return ExecuteInterruptCast(payload, *actor);
        case GameplayAction::ApplyMagicEffect:
            return ExecuteMagicEffect(a_command, *actor);
        case GameplayAction::AddSpell:
        case GameplayAction::RemoveSpell:
            return ExecuteAddRemoveSpell(payload, *actor);
        default:
            return CommandStatus::Malformed;
        }
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

CommandStatus ApplyProjectileLaunch(const CommandRecord& a_command) noexcept
{
    try {
        const auto& identity = a_command.Header.Identity;
        const auto& payload = a_command.Payload.ApplyProjectileLaunch;
        const auto bounded = [](const float a_value, const float a_limit) noexcept {
            return std::isfinite(a_value) && a_value >= -a_limit && a_value <= a_limit;
        };
        if (a_command.Header.Kind != static_cast<std::uint16_t>(CommandKind::ApplyProjectileLaunch) ||
            a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 || identity.Reserved0 != 0 ||
            identity.SequenceId != 0 || identity.ActionId == 0 || !CanonicalEntity::IsValid(identity.EntityId, identity.EntityGeneration) ||
            payload.TargetHandle.Value == 0 || payload.LocalProjectileBaseFormId == 0 || payload.LocalParentCellFormId == 0 ||
            !bounded(payload.OriginX, kMaximumProjectileCoordinate) ||
            !bounded(payload.OriginY, kMaximumProjectileCoordinate) ||
            !bounded(payload.OriginZ, kMaximumProjectileCoordinate) ||
            !bounded(payload.AngleX, kMaximumProjectileAngle) || !bounded(payload.AngleZ, kMaximumProjectileAngle) ||
            !std::isfinite(payload.Power) || payload.Power < 0.0F || payload.Power > kMaximumProjectilePower ||
            !std::isfinite(payload.Scale) || payload.Scale < 0.0F || payload.Scale > kMaximumProjectileScale ||
            !IsCastingSource(payload.CastingSource) || payload.Area < 0 || payload.Area > kMaximumProjectileArea ||
            !HasOnlyFlags(payload.LaunchFlags, kProjectileLaunchKnownFlags) || payload.LocalShooterFormId != 0 ||
            !std::all_of(std::begin(payload.ReservedTail), std::end(payload.ReservedTail),
                         [](const std::uint8_t a_value) { return a_value == 0; }))
            return CommandStatus::Malformed;

        RE::NiPointer<RE::Actor> shooter;
        const auto shooterStatus = AvatarManager::Get().ResolveActorByHandle(identity, payload.TargetHandle, shooter);
        if (shooterStatus != CommandStatus::Success)
            return shooterStatus;

        auto* projectile = ResolveLocalForm<RE::BGSProjectile>(payload.LocalProjectileBaseFormId);
        auto* weapon = payload.LocalWeaponFormId != 0 ? ResolveLocalForm<RE::TESObjectWEAP>(payload.LocalWeaponFormId) : nullptr;
        auto* ammo = payload.LocalAmmoFormId != 0 ? ResolveLocalForm<RE::TESAmmo>(payload.LocalAmmoFormId) : nullptr;
        auto* spell = payload.LocalSpellFormId != 0 ? ResolveLocalForm<RE::MagicItem>(payload.LocalSpellFormId) : nullptr;
        auto* parentCell = ResolveLocalForm<RE::TESObjectCELL>(payload.LocalParentCellFormId);
        if (!projectile || !parentCell || (payload.LocalWeaponFormId != 0 && !weapon) ||
            (payload.LocalAmmoFormId != 0 && !ammo) || (payload.LocalSpellFormId != 0 && !spell))
            return CommandStatus::MissingForm;

        const RE::NiPoint3 origin{payload.OriginX, payload.OriginY, payload.OriginZ};
        const RE::Projectile::ProjectileRot rotation{payload.AngleX, payload.AngleZ};
        RE::Projectile::LaunchData launch{projectile, shooter.get(), origin, rotation};
        launch.parentCell = parentCell;
        launch.weaponSource = weapon;
        launch.ammoSource = ammo;
        launch.spell = spell;
        launch.castingSource = ToCastingSource(payload.CastingSource);
        launch.area = payload.Area;
        launch.power = payload.Power;
        launch.scale = payload.Scale;
        launch.alwaysHit = (payload.LaunchFlags & ProjectileAlwaysHit) != 0;
        launch.noDamageOutsideCombat = (payload.LaunchFlags & ProjectileNoDamageOutsideCombat) != 0;
        launch.autoAim = (payload.LaunchFlags & ProjectileAutoAim) != 0;
        launch.chainShatter = (payload.LaunchFlags & ProjectileChainShatter) != 0;
        launch.useOrigin = true;
        launch.deferInitialization = (payload.LaunchFlags & ProjectileDeferInitialization) != 0;
        launch.forceConeOfFire = (payload.LaunchFlags & ProjectileForceConeOfFire) != 0;
        RE::ProjectileHandle handle;
        ProjectileHooks::ScopedRemoteLaunch allowRemoteLaunch;
        RE::Projectile::Launch(&handle, launch);
        return handle ? CommandStatus::Success : CommandStatus::EngineRejected;
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
