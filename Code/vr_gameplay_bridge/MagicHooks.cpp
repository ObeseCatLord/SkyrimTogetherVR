#include "MagicHooks.h"

#include "AvatarManager.h"
#include "BridgeEndpoint.h"
#include "VRInteractionManager.h"
#include "VrHookDetachPolicy.h"

#include <MinHook.h>
#include <RE/B/BGSPerk.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
{
namespace
{
using InterruptCastImpl = void (*)(RE::ActorMagicCaster*, bool);
using SpellCast = void (*)(RE::ActorMagicCaster*, bool, std::uint32_t, RE::MagicItem*);
using AddTarget = bool (*)(RE::MagicTarget*, RE::MagicTarget::AddTargetData&);
using CheckAddEffect = bool (*)(RE::MagicTarget::AddTargetData*, RE::ActiveEffectFactory::CheckTargetArgs&, float);
using AdjustForPerks = void (*)(RE::ActiveEffect*, RE::Actor*, RE::MagicTarget*);
using HasPerk = bool (*)(const RE::Actor*, RE::BGSPerk*);
using RemoveSpell = bool (*)(RE::Actor*, RE::SpellItem*);

constexpr std::uint32_t kMagicEffectAreaTarget = 1u << 0;
constexpr std::uint32_t kMagicEffectDualCasted = 1u << 1;
constexpr std::uint32_t kMagicEffectHostile = 1u << 2;
constexpr std::uint32_t kMagicEffectApplyHealPerkBonus = 1u << 3;
constexpr std::uint32_t kMagicEffectApplyStaminaPerkBonus = 1u << 4;
constexpr RE::FormID kMagicRestoreHealthKeywordId = 0x0001CEB0;
constexpr RE::FormID kMagicWardKeywordId = 0x0001EA69;
constexpr RE::FormID kMagicInvisibilityKeywordId = 0x0001EA6F;
constexpr RE::FormID kMagicNightEyeKeywordId = 0x000AD7C6;
constexpr RE::FormID kHealingPerkFormId = 0x000581F8;
constexpr RE::FormID kStaminaPerkFormId = 0x000581F9;
constexpr RE::FormID kCourageSpellFormId = 0x0004DEE8;
constexpr RE::FormID kRallySpellFormId = 0x0004DEEC;
constexpr RE::FormID kCallToArmsSpellFormId = 0x0007E8DD;
constexpr RE::FormID kBowOfShadowsSpellLocalFormId = 0x00000805;
constexpr std::string_view kBowOfShadowsPluginName = "ccbgssse038-bowofshadows.esl";
constexpr std::uint64_t kAddTargetVrRva = 0x05579C0;
constexpr std::array<std::uint8_t, 16> kAddTargetVrPrologue{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
    0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00,
};
constexpr std::uint64_t kCheckAddEffectVrRva = 0x0557830;
constexpr std::array<std::uint8_t, 16> kCheckAddEffectVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x48, 0x83,
    0xEC, 0x40, 0x48, 0x89, 0x6C, 0x24, 0x50, 0x48,
};
constexpr std::uint64_t kActiveEffectVtableVrRva = 0x16AE840;
constexpr std::uint64_t kAdjustForPerksVrRva = 0x0540CC0;
constexpr std::array<std::uint8_t, 16> kAdjustForPerksVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
};
constexpr std::uint64_t kHasPerkVrRva = 0x06025A0;
constexpr std::array<std::uint8_t, 16> kHasPerkVrPrologue{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x81, 0xF0,
    0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x16,
};
constexpr std::uint64_t kRemoveSpellVrRva = 0x06385F0;
constexpr std::array<std::uint8_t, 16> kRemoveSpellVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x20, 0x45, 0x32, 0xF6, 0x48, 0x8B, 0xFA, 0x48,
};

InterruptCastImpl g_originalInterruptCast{};
SpellCast g_originalSpellCast{};
AddTarget g_originalAddTarget{};
CheckAddEffect g_originalCheckAddEffect{};
AdjustForPerks g_originalAdjustForPerks{};
HasPerk g_originalHasPerk{};
RemoveSpell g_originalRemoveSpell{};
std::atomic<bool> g_installing{};
std::atomic<bool> g_installed{};
std::atomic<std::uint64_t> g_nextActionId{};
thread_local std::uint32_t g_remoteMagicApplicationDepth{};
struct AuthoritativeRemoteAddTargetContext
{
    const RE::MagicTarget::AddTargetData* Data{};
    RE::MagicTarget* Target{};
    const RE::TESObjectREFR* CasterReference{};
    const RE::Actor* Caster{};
    const RE::Effect* Effect{};
    const RE::MagicItem* MagicItem{};
    bool ApplyHealPerkBonus{};
    bool ApplyStaminaPerkBonus{};
};
thread_local const AuthoritativeRemoteAddTargetContext* g_authoritativeRemoteAddTargetContext{};
// Respite is queried by AdjustForPerks. Restrict the override to that exact
// call instead of the entire AddTarget invocation, which can re-enter other
// engine work on the same thread.
thread_local const AuthoritativeRemoteAddTargetContext* g_adjustingAuthoritativeEffectContext{};

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    return a_address >= text.address() && a_address < text.address() + text.size();
}

[[nodiscard]] bool IsValidFormId(const RE::FormID a_formId) noexcept
{
    return a_formId != 0 && a_formId != std::numeric_limits<RE::FormID>::max() &&
           RE::TESForm::LookupByID(a_formId) != nullptr;
}

[[nodiscard]] bool IsValidCastingSource(const RE::MagicSystem::CastingSource a_source) noexcept
{
    const auto value = static_cast<std::int32_t>(a_source);
    return value >= static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kLeftHand) &&
           value <= static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kInstant);
}

[[nodiscard]] bool IsValidAddTargetCastingSource(const RE::MagicSystem::CastingSource a_source) noexcept
{
    return a_source == RE::MagicSystem::CastingSource::kNone || IsValidCastingSource(a_source);
}

[[nodiscard]] bool IsBoundedMagicScalar(const float a_value) noexcept
{
    return std::isfinite(a_value) && a_value >= 0.0F && a_value <= kMaximumProjectilePower;
}

[[nodiscard]] bool IsKnownCastingType(const RE::MagicSystem::CastingType a_castingType) noexcept
{
    switch (a_castingType) {
    case RE::MagicSystem::CastingType::kConstantEffect:
    case RE::MagicSystem::CastingType::kFireAndForget:
    case RE::MagicSystem::CastingType::kConcentration:
    case RE::MagicSystem::CastingType::kScroll:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsKnownEffectArchetype(const RE::EffectArchetype a_archetype) noexcept
{
    const auto value = static_cast<std::int32_t>(a_archetype);
    return value >= static_cast<std::int32_t>(RE::EffectArchetype::kNone) &&
           value <= static_cast<std::int32_t>(RE::EffectArchetype::kVampireLord);
}

[[nodiscard]] bool IsBowOfShadowsInvisibilityException(const RE::MagicItem& a_magicItem) noexcept
{
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        return false;

    const auto* bowOfShadows = dataHandler->LookupForm<RE::MagicItem>(
        kBowOfShadowsSpellLocalFormId, kBowOfShadowsPluginName);
    return bowOfShadows && bowOfShadows->GetFormID() == a_magicItem.GetFormID();
}

struct AddTargetEffectPolicy
{
    bool IsConcentration{};
    bool IsHealing{};
    bool IsSupportedBuff{};
    bool IsWard{};
    bool IsInvisibility{};
    bool IsBoundWeapon{};
};

[[nodiscard]] bool ClassifyAddTargetEffectPolicy(
    const RE::MagicItem& a_magicItem,
    AddTargetEffectPolicy& a_policy) noexcept
{
    const auto castingType = a_magicItem.GetCastingType();
    if (!IsKnownCastingType(castingType) || a_magicItem.effects.empty())
        return false;

    const auto* restoreHealthKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(kMagicRestoreHealthKeywordId);
    const auto* wardKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(kMagicWardKeywordId);
    const auto* invisibilityKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(kMagicInvisibilityKeywordId);
    if (!restoreHealthKeyword || !wardKeyword || !invisibilityKeyword)
        return false;

    a_policy.IsConcentration = castingType == RE::MagicSystem::CastingType::kConcentration;
    a_policy.IsHealing = a_magicItem.HasKeyword(restoreHealthKeyword);
    switch (a_magicItem.GetFormID()) {
    case kCourageSpellFormId:
    case kRallySpellFormId:
    case kCallToArmsSpellFormId:
        a_policy.IsSupportedBuff = true;
        break;
    default:
        break;
    }
    a_policy.IsWard = a_magicItem.HasKeyword(wardKeyword);
    const bool isBowOfShadows = IsBowOfShadowsInvisibilityException(a_magicItem);
    a_policy.IsInvisibility = !isBowOfShadows && a_magicItem.HasKeyword(invisibilityKeyword);

    for (const auto* effect : a_magicItem.effects) {
        const auto* baseEffect = effect ? effect->baseEffect : nullptr;
        if (!baseEffect || !IsKnownEffectArchetype(baseEffect->GetArchetype()))
            return false;

        a_policy.IsHealing = a_policy.IsHealing || baseEffect->HasKeyword(restoreHealthKeyword);
        a_policy.IsWard = a_policy.IsWard || baseEffect->HasKeyword(wardKeyword);
        a_policy.IsInvisibility = a_policy.IsInvisibility ||
                                  (!isBowOfShadows && baseEffect->HasKeyword(invisibilityKeyword));
        a_policy.IsBoundWeapon = a_policy.IsBoundWeapon ||
                                 baseEffect->HasArchetype(RE::EffectArchetype::kBoundWeapon);
    }

    return true;
}

[[nodiscard]] bool ShouldPublishAddTarget(
    const RE::MagicItem& a_magicItem,
    const RE::EffectSetting& a_baseEffect) noexcept
{
    AddTargetEffectPolicy policy{};
    if (!ClassifyAddTargetEffectPolicy(a_magicItem, policy))
        return false;

    return (!policy.IsConcentration || policy.IsHealing) && !policy.IsWard && !policy.IsInvisibility &&
           !policy.IsBoundWeapon && !a_baseEffect.HasArchetype(RE::EffectArchetype::kSummonCreature);
}

[[nodiscard]] bool IsHealingOrSupportedBuff(const AddTargetEffectPolicy& a_policy) noexcept
{
    return a_policy.IsHealing || a_policy.IsSupportedBuff;
}

[[nodiscard]] bool IsLocalPlayerNoSyncEffect(const RE::MagicTarget::AddTargetData& a_data) noexcept
{
    const auto* baseEffect = a_data.effect ? a_data.effect->baseEffect : nullptr;
    if (!baseEffect)
        return false;

    if (baseEffect->HasArchetype(RE::EffectArchetype::kSlowTime))
        return true;

    const auto* nightEyeKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(kMagicNightEyeKeywordId);
    return nightEyeKeyword && baseEffect->HasKeyword(nightEyeKeyword);
}

class ScopedAuthoritativeRemoteAddTarget final
{
public:
    ScopedAuthoritativeRemoteAddTarget(
        RE::MagicTarget& a_target,
        const RE::MagicTarget::AddTargetData& a_data,
        const RE::Actor* a_caster,
        const bool a_applyHealPerkBonus,
        const bool a_applyStaminaPerkBonus) noexcept
        : _context{&a_data, &a_target, a_data.caster, a_caster, a_data.effect, a_data.magicItem,
                   a_applyHealPerkBonus, a_applyStaminaPerkBonus}
        , _previous(g_authoritativeRemoteAddTargetContext)
    {
        g_authoritativeRemoteAddTargetContext = &_context;
    }

    ~ScopedAuthoritativeRemoteAddTarget() noexcept
    {
        g_authoritativeRemoteAddTargetContext = _previous;
    }

    ScopedAuthoritativeRemoteAddTarget(const ScopedAuthoritativeRemoteAddTarget&) = delete;
    ScopedAuthoritativeRemoteAddTarget& operator=(const ScopedAuthoritativeRemoteAddTarget&) = delete;

private:
    AuthoritativeRemoteAddTargetContext _context{};
    const AuthoritativeRemoteAddTargetContext* _previous{};
};

class ScopedAuthoritativeAdjustForPerks final
{
public:
    explicit ScopedAuthoritativeAdjustForPerks(
        const AuthoritativeRemoteAddTargetContext* a_context) noexcept
        : _previous(g_adjustingAuthoritativeEffectContext)
    {
        g_adjustingAuthoritativeEffectContext = a_context;
    }

    ~ScopedAuthoritativeAdjustForPerks() noexcept
    {
        g_adjustingAuthoritativeEffectContext = _previous;
    }

    ScopedAuthoritativeAdjustForPerks(const ScopedAuthoritativeAdjustForPerks&) = delete;
    ScopedAuthoritativeAdjustForPerks& operator=(const ScopedAuthoritativeAdjustForPerks&) = delete;

private:
    const AuthoritativeRemoteAddTargetContext* _previous{};
};

[[nodiscard]] bool HasCompleteAuthoritativeIdentity(
    const AuthoritativeRemoteAddTargetContext& a_context) noexcept
{
    const bool casterIdentityMatches =
        (!a_context.CasterReference && !a_context.Caster) ||
        (a_context.CasterReference && a_context.Caster && a_context.CasterReference == a_context.Caster);
    return a_context.Data && a_context.Target && a_context.Effect && a_context.MagicItem && casterIdentityMatches;
}

[[nodiscard]] bool MatchesAuthoritativeCheck(
    const AuthoritativeRemoteAddTargetContext& a_context,
    const RE::MagicTarget::AddTargetData* a_data,
    const RE::ActiveEffectFactory::CheckTargetArgs& a_args) noexcept
{
    if (!HasCompleteAuthoritativeIdentity(a_context) || a_data != a_context.Data ||
        a_data->caster != a_context.CasterReference || a_data->effect != a_context.Effect ||
        a_data->magicItem != a_context.MagicItem)
        return false;

    const auto* baseEffect = a_context.Effect->baseEffect;
    return baseEffect && a_args.target == a_context.Target && a_args.caster == a_context.Caster &&
           a_args.effectSetting == baseEffect && a_args.spell == a_context.MagicItem &&
           a_args.magnitude == a_data->magnitude && a_args.dualCast == a_data->dualCasted;
}

[[nodiscard]] bool MatchesAuthoritativeActiveEffect(
    const AuthoritativeRemoteAddTargetContext& a_context,
    const RE::ActiveEffect* a_effect,
    const RE::Actor* a_caster,
    const RE::MagicTarget* a_target) noexcept
{
    // ActiveEffect does not retain its source AddTargetData. Its typed target,
    // caster argument, spell, and Effect pointers are the complete identity the
    // engine preserves past AddTarget's synchronous factory call.
    return HasCompleteAuthoritativeIdentity(a_context) && a_effect && a_caster == a_context.Caster &&
           a_target == a_context.Target && a_effect->target == a_context.Target &&
           a_effect->spell == a_context.MagicItem && a_effect->effect == a_context.Effect;
}

[[nodiscard]] std::uint64_t NextActionId() noexcept
{
    auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    if (actionId == 0)
        actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    return actionId;
}

[[nodiscard]] bool PublishMagicAction(
    const GameplayAction a_action,
    const GameplayActionPayload& a_payload) noexcept
{
    try {
        auto& endpoint = BridgeEndpoint::Get();
        if (!endpoint.IsOperational() || !IsActionInDomain(GameplayDomain::Magic, a_action) ||
            a_payload.TargetHandle.Value != kLocalPlayerHandle.Value || a_payload.TargetLocalFormId == 0 ||
            a_payload.SecondaryHandle.Value != 0 || !std::isfinite(a_payload.ScalarA) ||
            !std::isfinite(a_payload.ScalarB) || !std::isfinite(a_payload.ScalarC) ||
            !std::isfinite(a_payload.ScalarD))
            return false;

        const auto* mapping = endpoint.Mapping();
        if (!mapping || !HasCapability(mapping->Header.ActiveCapabilities.load(std::memory_order_acquire),
                                       Capability::CombatAndMagic))
            return false;

        EventRecord record{};
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = NextActionId();
        if (record.Header.Identity.ActionId == 0)
            return false;
        record.Payload.LocalGameplayAction = a_payload;
        record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::Magic);
        record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
        return endpoint.TryPushEvent(record);
    } catch (...) {
        return false;
    }
}

void PublishSpellCast(const RE::ActorMagicCaster& a_caster, const RE::Actor& a_actor, RE::MagicItem* a_spell) noexcept
{
    if (!a_spell)
        return;

    const auto casterFormId = a_actor.GetFormID();
    const auto spellFormId = a_spell->GetFormID();
    const auto source = a_caster.GetCastingSource();
    if (!IsValidFormId(casterFormId) || !IsValidFormId(spellFormId) || !IsValidCastingSource(source))
        return;

    std::uint32_t desiredTargetFormId{};
    const auto desiredTarget = std::as_const(a_caster.desiredTarget).get();
    if (desiredTarget) {
        const auto formId = desiredTarget->GetFormID();
        if (IsValidFormId(formId))
            desiredTargetFormId = formId;
    }

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = casterFormId;
    payload.LocalFormIdA = spellFormId;
    payload.LocalFormIdB = desiredTargetFormId;
    payload.ValueA = static_cast<std::int32_t>(source);
    payload.ActionFlags = a_caster.GetIsDualCasting() ? 1u : 0u;
    PublishMagicAction(GameplayAction::CastSpell, payload);
}

void PublishInterruptCast(const RE::ActorMagicCaster& a_caster, const RE::Actor& a_actor) noexcept
{
    const auto casterFormId = a_actor.GetFormID();
    const auto source = a_caster.GetCastingSource();
    if (!IsValidFormId(casterFormId) || !IsValidCastingSource(source))
        return;

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = casterFormId;
    payload.ValueA = static_cast<std::int32_t>(source);
    PublishMagicAction(GameplayAction::InterruptCast, payload);
}

[[nodiscard]] bool BuildAddTargetPayload(
    RE::MagicTarget& a_target,
    const RE::MagicTarget::AddTargetData& a_data,
    GameplayActionPayload& ar_payload) noexcept
{
    auto* targetActor = a_target.GetTargetAsActor();
    auto* casterActor = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
    const auto* magicItem = a_data.magicItem;
    const auto* effect = a_data.effect;
    const auto* baseEffect = effect ? effect->baseEffect : nullptr;
    if (!targetActor || !magicItem || !effect || !baseEffect || !IsBoundedMagicScalar(a_data.magnitude) ||
        !IsBoundedMagicScalar(a_data.power) || !IsValidAddTargetCastingSource(a_data.castingSource))
        return false;
    if (!ShouldPublishAddTarget(*magicItem, *baseEffect))
        return false;

    const auto targetFormId = targetActor->GetFormID();
    const auto magicItemFormId = magicItem->GetFormID();
    const auto baseEffectFormId = baseEffect->GetFormID();
    if (!IsValidFormId(targetFormId) || !IsValidFormId(magicItemFormId) || !IsValidFormId(baseEffectFormId))
        return false;

    std::uint32_t casterFormId{};
    if (casterActor) {
        casterFormId = casterActor->GetFormID();
        if (!IsValidFormId(casterFormId))
            return false;
    }

    auto& avatars = AvatarManager::Get();
    if (avatars.IsManagedRemoteActor(targetActor) && casterActor && avatars.IsManagedRemoteActor(casterActor))
        return false;

    bool applyHealPerkBonus{};
    bool applyStaminaPerkBonus{};
    if (avatars.IsManagedRemoteActor(targetActor) && casterActor == RE::PlayerCharacter::GetSingleton()) {
        AddTargetEffectPolicy policy{};
        if (ClassifyAddTargetEffectPolicy(*magicItem, policy) && policy.IsHealing) {
            if (auto* healingPerk = RE::TESForm::LookupByID<RE::BGSPerk>(kHealingPerkFormId))
                applyHealPerkBonus = casterActor->HasPerk(healingPerk);
            if (auto* staminaPerk = RE::TESForm::LookupByID<RE::BGSPerk>(kStaminaPerkFormId))
                applyStaminaPerkBonus = casterActor->HasPerk(staminaPerk);
        }
    }

    ar_payload = {};
    ar_payload.TargetHandle = kLocalPlayerHandle;
    ar_payload.TargetLocalFormId = targetFormId;
    ar_payload.LocalFormIdA = magicItemFormId;
    ar_payload.LocalFormIdB = baseEffectFormId;
    ar_payload.LocalFormIdC = casterFormId;
    ar_payload.ValueA = static_cast<std::int32_t>(a_data.castingSource);
    ar_payload.ScalarA = a_data.magnitude;
    ar_payload.ScalarB = a_data.power;
    ar_payload.ActionFlags = (a_data.areaTarget ? kMagicEffectAreaTarget : 0u) |
                            (a_data.dualCasted ? kMagicEffectDualCasted : 0u) |
                            (baseEffect->IsHostile() ? kMagicEffectHostile : 0u) |
                            (applyHealPerkBonus ? kMagicEffectApplyHealPerkBonus : 0u) |
                            (applyStaminaPerkBonus ? kMagicEffectApplyStaminaPerkBonus : 0u);
    return true;
}

[[nodiscard]] bool CallAddTargetAndPublish(
    RE::MagicTarget* a_target,
    RE::MagicTarget::AddTargetData& a_data,
    const bool a_syncableEffect)
{
    GameplayActionPayload payload{};
    const bool shouldPublish = a_syncableEffect && BuildAddTargetPayload(*a_target, a_data, payload);
    const bool result = g_originalAddTarget(a_target, a_data);
    if (result && shouldPublish)
        PublishMagicAction(GameplayAction::ApplyMagicEffect, payload);
    return result;
}

void PublishRemoveSpell(const RE::Actor& a_actor, const RE::SpellItem& a_spell) noexcept
{
    if (&a_actor != RE::PlayerCharacter::GetSingleton() || !IsValidFormId(a_actor.GetFormID()) ||
        !IsValidFormId(a_spell.GetFormID()))
        return;

    GameplayActionPayload payload{};
    payload.TargetHandle = kLocalPlayerHandle;
    payload.TargetLocalFormId = a_actor.GetFormID();
    payload.LocalFormIdA = a_spell.GetFormID();
    PublishMagicAction(GameplayAction::RemoveSpell, payload);
}

void HookInterruptCast(RE::ActorMagicCaster* a_caster, const bool a_depleteEnergy) noexcept
{
    try {
        auto* actor = a_caster ? a_caster->actor : nullptr;
        if (actor && AvatarManager::Get().IsManagedRemoteActor(actor) && g_remoteMagicApplicationDepth == 0)
            return;

        if (g_originalInterruptCast)
            g_originalInterruptCast(a_caster, a_depleteEnergy);

        if (g_remoteMagicApplicationDepth == 0 && a_caster && actor)
            PublishInterruptCast(*a_caster, *actor);
    } catch (...) {
    }
}

void HookSpellCast(
    RE::ActorMagicCaster* a_caster,
    const bool a_doCast,
    const std::uint32_t a_targetCount,
    RE::MagicItem* a_spell) noexcept
{
    try {
        auto* actor = a_caster ? a_caster->actor : nullptr;
        if (actor && AvatarManager::Get().IsManagedRemoteActor(actor) && g_remoteMagicApplicationDepth == 0)
            return;

        if (g_originalSpellCast)
            g_originalSpellCast(a_caster, a_doCast, a_targetCount, a_spell);

        if (g_remoteMagicApplicationDepth == 0 && a_doCast && a_caster && actor)
            PublishSpellCast(*a_caster, *actor, a_spell);
    } catch (...) {
    }
}

bool HookCheckAddEffect(
    RE::MagicTarget::AddTargetData* a_data,
    RE::ActiveEffectFactory::CheckTargetArgs& a_args,
    const float a_resistance) noexcept
{
    try {
        // The authoritative exception is valid only for this exact synchronous
        // AddTarget invocation. Nested engine work must retain vanilla checks.
        const auto* context = g_authoritativeRemoteAddTargetContext;
        if (context && MatchesAuthoritativeCheck(*context, a_data, a_args))
            return true;

        return g_originalCheckAddEffect ? g_originalCheckAddEffect(a_data, a_args, a_resistance) : false;
    } catch (...) {
        return false;
    }
}

void HookAdjustForPerks(
    RE::ActiveEffect* a_effect,
    RE::Actor* a_caster,
    RE::MagicTarget* a_target) noexcept
{
    try {
        if (!g_originalAdjustForPerks)
            return;
        const auto* context = g_authoritativeRemoteAddTargetContext;
        const bool matchesContext = context && MatchesAuthoritativeActiveEffect(*context, a_effect, a_caster, a_target);
        ScopedAuthoritativeAdjustForPerks perkScope{matchesContext ? context : nullptr};
        g_originalAdjustForPerks(a_effect, a_caster, a_target);

        if (matchesContext && context->ApplyHealPerkBonus)
            a_effect->magnitude *= 1.5F;
    } catch (...) {
    }
}

bool HookHasPerk(const RE::Actor* a_actor, RE::BGSPerk* a_perk) noexcept
{
    try {
        const auto* context = g_adjustingAuthoritativeEffectContext;
        if (context && context->ApplyStaminaPerkBonus && HasCompleteAuthoritativeIdentity(*context) &&
            a_actor == context->Caster && a_perk &&
            a_perk->GetFormID() == kStaminaPerkFormId)
            return true;

        return g_originalHasPerk ? g_originalHasPerk(a_actor, a_perk) : false;
    } catch (...) {
        return false;
    }
}

bool HookAddTarget(RE::MagicTarget* a_target, RE::MagicTarget::AddTargetData& a_data) noexcept
{
    try {
        if (!g_originalAddTarget || !a_target)
            return false;

        // A server replay must execute the engine path once but may never echo
        // back into the local gameplay stream.
        if (g_remoteMagicApplicationDepth != 0)
            return g_originalAddTarget(a_target, a_data);

        auto* targetActor = a_target->GetTargetAsActor();
        auto* casterActor = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
        const auto* magicItem = a_data.magicItem;
        const auto* effect = a_data.effect;
        const auto* baseEffect = effect ? effect->baseEffect : nullptr;
        if (!targetActor || !magicItem || !effect || !baseEffect)
            return g_originalAddTarget(a_target, a_data);

        auto& avatars = AvatarManager::Get();
        const auto targetIsRemote = avatars.IsManagedRemoteActor(targetActor);
        const auto casterIsRemote = avatars.IsManagedRemoteActor(casterActor);
        const auto* localPlayer = RE::PlayerCharacter::GetSingleton();

        // These are client-local effects in the original hook. They must run
        // through the unmodified engine path before ownership/PVP decisions.
        if (targetActor == localPlayer && IsLocalPlayerNoSyncEffect(a_data))
            return g_originalAddTarget(a_target, a_data);

        AddTargetEffectPolicy policy{};
        const auto hasPolicy = ClassifyAddTargetEffectPolicy(*magicItem, policy);
        const auto syncableEffect = hasPolicy && ShouldPublishAddTarget(*magicItem, *baseEffect);

        if (targetIsRemote) {
            if (!casterActor || casterActor != localPlayer || !hasPolicy || !IsHealingOrSupportedBuff(policy))
                return false;

            return CallAddTargetAndPublish(a_target, a_data, syncableEffect);
        }

        if (targetActor == localPlayer && casterIsRemote) {
            if (!VRInteractionManager::IsPvpEnabled())
                return false;
            if (hasPolicy && IsHealingOrSupportedBuff(policy))
                return false;

            return CallAddTargetAndPublish(a_target, a_data, syncableEffect);
        }

        if (casterActor == localPlayer) {
            return CallAddTargetAndPublish(a_target, a_data, syncableEffect);
        }

        // The remote caster is already replaying against this client. Applying
        // it to a non-player actor here duplicates the server-authoritative hit.
        if (casterIsRemote)
            return false;

        // Match the original target-IsLocal branch for locally owned NPCs,
        // including non-player local casters. The payload is frozen before
        // AddTarget is allowed to mutate its borrowed data.
        if (targetActor != localPlayer && !targetIsRemote)
            return CallAddTargetAndPublish(a_target, a_data, syncableEffect);

        return false;
    } catch (...) {
        return false;
    }
}

bool HookRemoveSpell(RE::Actor* a_actor, RE::SpellItem* a_spell) noexcept
{
    try {
        if (!g_originalRemoveSpell || !a_actor || !a_spell)
            return false;
        if (AvatarManager::Get().IsManagedRemoteActor(a_actor) && g_remoteMagicApplicationDepth == 0)
            return false;

        const auto result = g_originalRemoveSpell(a_actor, a_spell);
        if (result && g_remoteMagicApplicationDepth == 0)
            PublishRemoveSpell(*a_actor, *a_spell);
        return result;
    } catch (...) {
        return false;
    }
}

void LogHookFailure(const char* a_operation, const int a_status) noexcept
{
    try {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} failed ({})", a_operation, a_status);
    } catch (...) {
    }
}

void LogHookFailure(const char* a_operation) noexcept
{
    try {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {}", a_operation);
    } catch (...) {
    }
}

enum class MagicHookIndex : std::size_t
{
    InterruptCast,
    SpellCast,
    AddTarget,
    CheckAddEffect,
    AdjustForPerks,
    HasPerk,
    RemoveSpell,
    Count,
};

struct HookRecord
{
    const char* Name{};
    void* Target{};
    bool Created{};
    bool Enabled{};
};

using HookRecords = std::array<HookRecord, static_cast<std::size_t>(MagicHookIndex::Count)>;

HookRecords g_hookRecords{};

[[nodiscard]] constexpr std::size_t HookOffset(const MagicHookIndex a_index) noexcept
{
    return static_cast<std::size_t>(a_index);
}

[[nodiscard]] HookRecords MakeHookRecords(
    void* a_interruptTarget,
    void* a_spellTarget,
    void* a_addTargetTarget,
    void* a_checkAddEffectTarget,
    void* a_adjustForPerksTarget,
    void* a_hasPerkTarget,
    void* a_removeSpellTarget) noexcept
{
    return {{
        {"ActorMagicCaster::InterruptCastImpl", a_interruptTarget},
        {"ActorMagicCaster::SpellCast", a_spellTarget},
        {"MagicTarget::AddTarget", a_addTargetTarget},
        {"MagicTarget::AddTargetData::CheckAddEffect", a_checkAddEffectTarget},
        {"ActiveEffect::AdjustForPerks", a_adjustForPerksTarget},
        {"Actor::HasPerk", a_hasPerkTarget},
        {"Actor::RemoveSpell", a_removeSpellTarget},
    }};
}

void ResetHookTrampolines() noexcept
{
    g_originalInterruptCast = nullptr;
    g_originalSpellCast = nullptr;
    g_originalAddTarget = nullptr;
    g_originalCheckAddEffect = nullptr;
    g_originalAdjustForPerks = nullptr;
    g_originalHasPerk = nullptr;
    g_originalRemoveSpell = nullptr;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHookRecord(void* a_context) noexcept
{
    const auto& hook = *static_cast<const HookRecord*>(a_context);
    const auto status = MH_DisableHook(hook.Target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogHookFailure(hook.Name, static_cast<int>(status));
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHookRecord(void* a_context) noexcept
{
    const auto& hook = *static_cast<const HookRecord*>(a_context);
    const auto status = MH_RemoveHook(hook.Target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogHookFailure(hook.Name, static_cast<int>(status));
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHookRecord(HookRecord& a_hook) noexcept
{
    auto state = VrHookDetachPolicy::HookState{a_hook.Created, a_hook.Enabled};
    const auto detached = VrHookDetachPolicy::Detach(
        state, {DisableHookRecord, RemoveHookRecord, &a_hook});
    a_hook.Created = state.Created;
    a_hook.Enabled = state.Enabled;
    return detached;
}

[[nodiscard]] bool CleanupHookRecords(HookRecords& a_hooks) noexcept
{
    bool detached = true;
    for (auto iterator = a_hooks.rbegin(); iterator != a_hooks.rend(); ++iterator)
        detached = DetachHookRecord(*iterator) && detached;
    return detached;
}

[[nodiscard]] bool HasTrackedHooks() noexcept
{
    for (const auto& hook : g_hookRecords) {
        if (hook.Created)
            return true;
    }
    return false;
}

void StoreHookRecords(HookRecords&& a_hooks) noexcept
{
    g_hookRecords = std::move(a_hooks);
}

class HookInstallTransaction final
{
public:
    explicit HookInstallTransaction(HookRecords a_hooks) noexcept
        : _hooks(std::move(a_hooks))
    {}

    ~HookInstallTransaction() noexcept
    {
        if (!_committed)
            static_cast<void>(Rollback());
    }

    [[nodiscard]] bool Create(
        const MagicHookIndex a_index,
        void* a_detour,
        void** a_original) noexcept
    {
        auto& hook = _hooks[HookOffset(a_index)];
        const auto status = MH_CreateHook(hook.Target, a_detour, a_original);
        if (status != MH_OK) {
            LogHookFailure(hook.Name, static_cast<int>(status));
            return false;
        }
        hook.Created = true;
        return true;
    }

    [[nodiscard]] bool Enable(const MagicHookIndex a_index) noexcept
    {
        auto& hook = _hooks[HookOffset(a_index)];
        // A failing enable is not evidence that the target was left pristine.
        // Keep rollback on the disable-first path until MinHook proves it safe.
        hook.Enabled = true;
        const auto status = MH_EnableHook(hook.Target);
        if (status != MH_OK) {
            LogHookFailure(hook.Name, static_cast<int>(status));
            return false;
        }
        return true;
    }

    void Commit() noexcept
    {
        StoreHookRecords(std::move(_hooks));
        _committed = true;
    }

    [[nodiscard]] bool Rollback() noexcept
    {
        if (_committed)
            return !HasTrackedHooks();
        const auto detached = CleanupHookRecords(_hooks);
        if (detached)
            ResetHookTrampolines();
        else
            StoreHookRecords(std::move(_hooks));
        _committed = true;
        return detached;
    }

private:
    HookRecords _hooks;
    bool _committed{};
};

[[nodiscard]] bool CleanupInstalledHooks() noexcept
{
    if (!HasTrackedHooks()) {
        ResetHookTrampolines();
        g_installed.store(false, std::memory_order_release);
        return true;
    }
    if (!CleanupHookRecords(g_hookRecords)) {
        LogHookFailure("magic hook uninstall could not prove detachment; retaining targets and trampolines so a possible live detour remains callable");
        return false;
    }
    g_hookRecords = {};
    ResetHookTrampolines();
    g_installed.store(false, std::memory_order_release);
    return true;
}
} // namespace

ScopedRemoteMagicApplication::ScopedRemoteMagicApplication() noexcept
{
    ++g_remoteMagicApplicationDepth;
}

ScopedRemoteMagicApplication::~ScopedRemoteMagicApplication() noexcept
{
    if (g_remoteMagicApplicationDepth != 0)
        --g_remoteMagicApplicationDepth;
}

RemoteAddTargetResult ApplyRemoteAddTarget(
    RE::MagicTarget& a_target,
    RE::MagicTarget::AddTargetData& a_data,
    const bool a_applyHealPerkBonus,
    const bool a_applyStaminaPerkBonus) noexcept
{
    try {
        auto* caster = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
        if (a_applyHealPerkBonus || a_applyStaminaPerkBonus) {
            AddTargetEffectPolicy policy{};
            if (!a_data.magicItem || !ClassifyAddTargetEffectPolicy(*a_data.magicItem, policy) || !policy.IsHealing)
                return RemoteAddTargetResult::EngineRejected;
        }
        if ((a_applyHealPerkBonus || a_applyStaminaPerkBonus) && !caster)
            return RemoteAddTargetResult::EngineRejected;

        ScopedRemoteMagicApplication suppressEcho;
        ScopedAuthoritativeRemoteAddTarget replayContext{
            a_target, a_data, caster, a_applyHealPerkBonus, a_applyStaminaPerkBonus};
        return a_target.AddTarget(a_data) ? RemoteAddTargetResult::Success : RemoteAddTargetResult::EngineRejected;
    } catch (...) {
        return RemoteAddTargetResult::EngineRejected;
    }
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire) || HasTrackedHooks())
        return true;

    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    const auto finish = [](const bool a_result) noexcept {
        g_installing.store(false, std::memory_order_release);
        return a_result;
    };

    ResetHookTrampolines();
    try {
        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            LogHookFailure("MinHook initialization for magic hooks", static_cast<int>(initialize));
            return finish(false);
        }

        const auto vtable = REL::Relocation<void**>(RE::VTABLE_ActorMagicCaster[0]);
        void* const interruptTarget = vtable.get()[8];
        void* const spellTarget = vtable.get()[9];
        REL::Relocation<AddTarget> addTarget{REL::ID(33742)};
        REL::Relocation<CheckAddEffect> checkAddEffect{REL::ID(33741)};
        const auto activeEffectVtable = REL::Relocation<void**>(RE::VTABLE_ActiveEffect[0]);
        void* const adjustForPerksTarget = activeEffectVtable.get()[0];
        REL::Relocation<HasPerk> hasPerk{REL::ID(36690)};
        REL::Relocation<RemoveSpell> removeSpell{RELOCATION_ID(37772, 38717)};
        void* const addTargetTarget = reinterpret_cast<void*>(addTarget.address());
        void* const checkAddEffectTarget = reinterpret_cast<void*>(checkAddEffect.address());
        void* const hasPerkTarget = reinterpret_cast<void*>(hasPerk.address());
        void* const removeSpellTarget = reinterpret_cast<void*>(removeSpell.address());
        if (!interruptTarget || !spellTarget || !addTargetTarget || !checkAddEffectTarget || !adjustForPerksTarget ||
            !hasPerkTarget || !removeSpellTarget ||
            !IsExecutableTarget(reinterpret_cast<std::uintptr_t>(interruptTarget)) ||
            !IsExecutableTarget(reinterpret_cast<std::uintptr_t>(spellTarget)) ||
            !IsExecutableTarget(addTarget.address()) || !IsExecutableTarget(checkAddEffect.address()) ||
            !IsExecutableTarget(reinterpret_cast<std::uintptr_t>(adjustForPerksTarget)) ||
            !IsExecutableTarget(hasPerk.address()) ||
            !IsExecutableTarget(removeSpell.address())) {
            LogHookFailure("magic hook target resolution returned a non-executable address");
            return finish(false);
        }
        if (addTarget.offset() != kAddTargetVrRva ||
            std::memcmp(addTargetTarget, kAddTargetVrPrologue.data(), kAddTargetVrPrologue.size()) != 0) {
            LogHookFailure("MagicTarget::AddTarget VR address or prologue mismatch");
            return finish(false);
        }
        if (checkAddEffect.offset() != kCheckAddEffectVrRva ||
            std::memcmp(checkAddEffectTarget, kCheckAddEffectVrPrologue.data(), kCheckAddEffectVrPrologue.size()) != 0) {
            LogHookFailure("MagicTarget::AddTargetData::CheckAddEffect VR address or prologue mismatch");
            return finish(false);
        }
        if (activeEffectVtable.offset() != kActiveEffectVtableVrRva ||
            reinterpret_cast<std::uintptr_t>(adjustForPerksTarget) != REL::Module::get().base() + kAdjustForPerksVrRva ||
            std::memcmp(adjustForPerksTarget, kAdjustForPerksVrPrologue.data(), kAdjustForPerksVrPrologue.size()) != 0) {
            LogHookFailure("ActiveEffect::AdjustForPerks VR vtable slot, address, or prologue mismatch");
            return finish(false);
        }
        if (hasPerk.offset() != kHasPerkVrRva ||
            std::memcmp(hasPerkTarget, kHasPerkVrPrologue.data(), kHasPerkVrPrologue.size()) != 0) {
            LogHookFailure("Actor::HasPerk VR address or prologue mismatch");
            return finish(false);
        }
        if (removeSpell.offset() != kRemoveSpellVrRva ||
            std::memcmp(removeSpellTarget, kRemoveSpellVrPrologue.data(), kRemoveSpellVrPrologue.size()) != 0) {
            LogHookFailure("Actor::RemoveSpell VR address or prologue mismatch");
            return finish(false);
        }

        HookInstallTransaction hooks{MakeHookRecords(
            interruptTarget, spellTarget, addTargetTarget, checkAddEffectTarget, adjustForPerksTarget,
            hasPerkTarget, removeSpellTarget)};
        if (!hooks.Create(MagicHookIndex::InterruptCast, reinterpret_cast<void*>(&HookInterruptCast),
                          reinterpret_cast<void**>(&g_originalInterruptCast)) ||
            !hooks.Create(MagicHookIndex::SpellCast, reinterpret_cast<void*>(&HookSpellCast),
                          reinterpret_cast<void**>(&g_originalSpellCast)) ||
            !hooks.Create(MagicHookIndex::AddTarget, reinterpret_cast<void*>(&HookAddTarget),
                          reinterpret_cast<void**>(&g_originalAddTarget)) ||
            !hooks.Create(MagicHookIndex::CheckAddEffect, reinterpret_cast<void*>(&HookCheckAddEffect),
                          reinterpret_cast<void**>(&g_originalCheckAddEffect)) ||
            !hooks.Create(MagicHookIndex::AdjustForPerks, reinterpret_cast<void*>(&HookAdjustForPerks),
                          reinterpret_cast<void**>(&g_originalAdjustForPerks)) ||
            !hooks.Create(MagicHookIndex::HasPerk, reinterpret_cast<void*>(&HookHasPerk),
                          reinterpret_cast<void**>(&g_originalHasPerk)) ||
            !hooks.Create(MagicHookIndex::RemoveSpell, reinterpret_cast<void*>(&HookRemoveSpell),
                          reinterpret_cast<void**>(&g_originalRemoveSpell))) {
            if (!hooks.Rollback()) {
                LogHookFailure("magic hook install rollback could not prove detachment; retaining targets and trampolines so a possible live detour remains callable");
                BridgeEndpoint::Get().Fault("magic hook install rollback could not prove detachment");
                return finish(true);
            }
            return finish(false);
        }

        if (!hooks.Enable(MagicHookIndex::InterruptCast) || !hooks.Enable(MagicHookIndex::SpellCast) ||
            !hooks.Enable(MagicHookIndex::AddTarget) || !hooks.Enable(MagicHookIndex::CheckAddEffect) ||
            !hooks.Enable(MagicHookIndex::AdjustForPerks) || !hooks.Enable(MagicHookIndex::HasPerk) ||
            !hooks.Enable(MagicHookIndex::RemoveSpell)) {
            if (!hooks.Rollback()) {
                LogHookFailure("magic hook install rollback could not prove detachment; retaining targets and trampolines so a possible live detour remains callable");
                BridgeEndpoint::Get().Fault("magic hook install rollback could not prove detachment");
                return finish(true);
            }
            return finish(false);
        }

        hooks.Commit();
        g_installed.store(true, std::memory_order_release);
        try {
            SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed ActorMagicCaster slots 8/9, MagicTarget hooks, ActiveEffect::AdjustForPerks, Actor::HasPerk, and Actor::RemoveSpell hooks");
        } catch (...) {
        }
        return finish(true);
    } catch (...) {
        LogHookFailure("exception while installing magic hooks");
        if (HasTrackedHooks()) {
            LogHookFailure("magic hook exception rollback retained targets and trampolines so a possible live detour remains callable");
            BridgeEndpoint::Get().Fault("magic hook exception rollback could not prove detachment");
            return finish(true);
        }
        return finish(false);
    }
}

bool Uninstall() noexcept
{
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    const bool detached = CleanupInstalledHooks();
    g_installing.store(false, std::memory_order_release);
    return detached;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
