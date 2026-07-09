#pragma once

#include <NetImmerse/NiPointer.h>
#include <RuntimeLayout.h>
#include <TESObjectREFR.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

template <class T> struct BSTEventSink;

// Very nasty work around to avoid template code duplication
namespace details
{
void InternalRegisterSink(void* apEventDispatcher, void* apSink) noexcept;
void InternalUnRegisterSink(void* apEventDispatcher, void* apSink) noexcept;
void InternalPushEvent(void* apEventDispatcher, void* apEvent) noexcept;
} // namespace details

template <class T> struct EventDispatcher
{
    void RegisterSink(BSTEventSink<T>* apSink) noexcept { details::InternalRegisterSink(reinterpret_cast<void*>(this), reinterpret_cast<void*>(apSink)); }

    void UnRegisterSink(BSTEventSink<T>* apSink) noexcept { details::InternalUnRegisterSink(reinterpret_cast<void*>(this), reinterpret_cast<void*>(apSink)); }

    void PushEvent(const T* apEvent) noexcept { details::InternalPushEvent(reinterpret_cast<void*>(this), reinterpret_cast<void*>(apEvent)); }

    uint8_t pad0[0x58];
};

struct UnknownEvent
{
};

static_assert(sizeof(EventDispatcher<UnknownEvent>) == Skyrim::RuntimeLayout::BSTEventSourceLocalShimOffsets::Size);

struct BGSEventProcessedEvent
{
};

struct TESActivateEvent
{
    [[nodiscard]] TESObjectREFR* GetObjectActivatedData() const noexcept { return objectActivated.object; }
    [[nodiscard]] TESObjectREFR* GetActionRefData() const noexcept { return actionRef.object; }

    NiPointer<TESObjectREFR> objectActivated;
    NiPointer<TESObjectREFR> actionRef;
};

static_assert(offsetof(TESActivateEvent, objectActivated) == 0x0);
static_assert(offsetof(TESActivateEvent, actionRef) == 0x8);
static_assert(sizeof(TESActivateEvent) == 0x10);

struct TESActiveEffectApplyRemove
{
    TESObjectREFR* hCaster;
    TESObjectREFR* hTarget;
    uint16_t usActiveEffectUniqueID;
    bool bIsApplied;
};

struct TESActorLocationChangeEvent
{
};

struct TESBookReadEvent
{
};

struct TESCellAttachDetachEvent
{
};

struct TESCellFullyLoadedEvent
{
};

struct TESCombatEvent
{
};

struct TESContainerChangedEvent
{
    uint32_t oldContainerID;
    uint32_t newContainerID;
    uint8_t pad8[0xC];
};

struct TESDeathEvent
{
    TESObjectREFR* pActorDying;
    TESObjectREFR* pActorKiller;
    bool isDead;
};

struct TESDestructionStageChangedEvent
{
};

struct TESEnterBleedoutEvent
{
};

struct TESEquipEvent
{
};

struct TESFormDeleteEvent
{
};

struct TESFurnitureEvent
{
};

struct TESGrabReleaseEvent
{
    [[nodiscard]] TESObjectREFR* GetReferenceData() const noexcept { return ref.object; }
    [[nodiscard]] bool GetGrabbedData() const noexcept { return grabbed; }

    NiPointer<TESObjectREFR> ref;
    bool grabbed;
    uint8_t pad09;
    uint16_t pad0A;
    uint32_t pad0C;
};

static_assert(offsetof(TESGrabReleaseEvent, ref) == 0x0);
static_assert(offsetof(TESGrabReleaseEvent, grabbed) == 0x8);
static_assert(sizeof(TESGrabReleaseEvent) == 0x10);

struct TESHitEvent
{
    [[nodiscard]] TESObjectREFR* GetTargetData() const noexcept { return target.object; }
    [[nodiscard]] TESObjectREFR* GetCauseData() const noexcept { return cause.object; }
    [[nodiscard]] uint32_t GetSourceData() const noexcept { return source; }
    [[nodiscard]] uint32_t GetProjectileData() const noexcept { return projectile; }
    [[nodiscard]] uint8_t GetFlagsData() const noexcept { return flags; }
    [[nodiscard]] uint32_t GetRawFlagsData() const noexcept
    {
        return static_cast<uint32_t>(flags) |
               (static_cast<uint32_t>(pad19) << 8) |
               (static_cast<uint32_t>(pad1A) << 16);
    }

    NiPointer<TESObjectREFR> target;
    NiPointer<TESObjectREFR> cause;
    uint32_t source;
    uint32_t projectile;
    uint8_t flags;
    uint8_t pad19;
    uint16_t pad1A;
    uint32_t pad1C;
};

static_assert(offsetof(TESHitEvent, target) == 0x0);
static_assert(offsetof(TESHitEvent, cause) == 0x8);
static_assert(offsetof(TESHitEvent, source) == 0x10);
static_assert(offsetof(TESHitEvent, projectile) == 0x14);
static_assert(offsetof(TESHitEvent, flags) == 0x18);
static_assert(sizeof(TESHitEvent) == 0x20);

struct TESLoadGameEvent
{
};

static_assert(sizeof(TESLoadGameEvent) == 0x1);

struct TESLockChangedEvent
{
};

struct TESMagicEffectApplyEvent
{
    [[nodiscard]] TESObjectREFR* GetTargetData() const noexcept { return target.object; }
    [[nodiscard]] TESObjectREFR* GetCasterData() const noexcept { return caster.object; }
    [[nodiscard]] uint32_t GetMagicEffectData() const noexcept { return magicEffect; }

    NiPointer<TESObjectREFR> target;
    NiPointer<TESObjectREFR> caster;
    uint32_t magicEffect;
    uint32_t pad14;
};

static_assert(offsetof(TESMagicEffectApplyEvent, target) == 0x0);
static_assert(offsetof(TESMagicEffectApplyEvent, caster) == 0x8);
static_assert(offsetof(TESMagicEffectApplyEvent, magicEffect) == 0x10);
static_assert(sizeof(TESMagicEffectApplyEvent) == 0x18);

struct TESMagicWardHitEvent
{
};

struct TESMoveAttachDetachEvent
{
};

struct TESObjectLoadedEvent
{
};

struct TESObjectREFRTranslationEvent
{
};

struct TESOpenCloseEvent
{
};

struct TESPackageEvent
{
};

struct TESInitScriptEvent
{
};

struct TESPerkEntryRunEvent
{
};

struct TESQuestInitEvent
{
    uint32_t formId;
};

struct TESQuestStageEvent
{
    void* callback;
    uint32_t formId;
    uint16_t stageId;
    bool bUnk;
};

struct TESQuestStartStopEvent
{
    uint32_t formId;
};

struct TESQuestStageItemDoneEvent
{
    uint32_t formId;
    uint16_t stageId;
    bool unk;
};

struct TESResetEvent
{
};

struct TESResolveNPCTemplatesEvent
{
};

struct TESSceneEvent
{
};

struct TESSceneActionEvent
{
};

struct TESScenePhaseEvent
{
};

struct TESSellEvent
{
};

struct TESSleepStartEvent
{
};

struct TESSleepStopEvent
{
};

struct TESSpellCastEvent
{
    [[nodiscard]] TESObjectREFR* GetObjectData() const noexcept { return object.object; }
    [[nodiscard]] uint32_t GetSpellData() const noexcept { return spell; }

    NiPointer<TESObjectREFR> object;
    uint32_t spell;
};

static_assert(offsetof(TESSpellCastEvent, object) == 0x0);
static_assert(offsetof(TESSpellCastEvent, spell) == 0x8);
static_assert(sizeof(TESSpellCastEvent) == 0x10);

struct TESPlayerBowShotEvent
{
    [[nodiscard]] uint32_t GetWeaponData() const noexcept { return weapon; }
    [[nodiscard]] uint32_t GetAmmoData() const noexcept { return ammo; }
    [[nodiscard]] float GetShotPowerData() const noexcept { return shotPower; }
    [[nodiscard]] bool IsSunGazingData() const noexcept { return isSunGazing; }

    uint32_t weapon;
    uint32_t ammo;
    float shotPower;
    bool isSunGazing;
};

static_assert(offsetof(TESPlayerBowShotEvent, weapon) == 0x0);
static_assert(offsetof(TESPlayerBowShotEvent, ammo) == 0x4);
static_assert(offsetof(TESPlayerBowShotEvent, shotPower) == 0x8);
static_assert(offsetof(TESPlayerBowShotEvent, isSunGazing) == 0xC);
static_assert(sizeof(TESPlayerBowShotEvent) == 0x10);

struct TESTopicInfoEvent
{
    TESObjectREFR* hSpeakerRef;
    void* pCallback;
    uint32_t uiTopicInfoFormID;
    int32_t eType;
    uint16_t usStage;
};

struct TESTrackedStatsEvent
{
};

struct TESTrapHitEvent
{
};

struct TESTriggerEvent
{
    TESObjectREFR* pTrigger;
    TESObjectREFR* pActionRef;
};

struct TESTriggerEnterEvent
{
    TESObjectREFR* pTrigger;
    TESObjectREFR* pActionRef;
};

struct TESTriggerLeaveEvent
{
    TESObjectREFR* pTrigger;
    TESObjectREFR* pActionRef;
};

struct TESUniqueIDChangeEvent
{
};

struct TESSwitchRaceCompleteEvent
{
};

struct TESFastTravelEndEvent
{
};

struct EventDispatcherManager
{
    using CommonLibEventSourceOffsets = Skyrim::RuntimeLayout::ScriptEventSourceHolderCommonLibNgOffsets;
    using LocalEventSourceOffsets = Skyrim::RuntimeLayout::EventDispatcherManagerLocalShimOffsets;

    static EventDispatcherManager* Get() noexcept;

    [[nodiscard]] EventDispatcher<TESActivateEvent>& GetActivateEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESActivateEvent>>(this, CommonLibEventSourceOffsets::Activate);
#else
        return activateEvent;
#endif
    }

    [[nodiscard]] EventDispatcher<TESHitEvent>& GetHitEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESHitEvent>>(this, CommonLibEventSourceOffsets::Hit);
#else
        return hitEvent;
#endif
    }

    [[nodiscard]] EventDispatcher<TESGrabReleaseEvent>& GetGrabReleaseEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESGrabReleaseEvent>>(this, CommonLibEventSourceOffsets::GrabRelease);
#else
        return grabReleaseEvent;
#endif
    }

    [[nodiscard]] EventDispatcher<TESLoadGameEvent>& GetLoadGameEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESLoadGameEvent>>(this, CommonLibEventSourceOffsets::LoadGame);
#else
        return loadGameEvent;
#endif
    }

    [[nodiscard]] EventDispatcher<TESMagicEffectApplyEvent>& GetMagicEffectApplyEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESMagicEffectApplyEvent>>(this, CommonLibEventSourceOffsets::MagicEffectApply);
#else
        return magicEffectApplyEvent;
#endif
    }

    [[nodiscard]] EventDispatcher<TESSpellCastEvent>& GetSpellCastEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESSpellCastEvent>>(this, CommonLibEventSourceOffsets::SpellCast);
#else
        return spellCastEvent;
#endif
    }

    [[nodiscard]] EventDispatcher<TESPlayerBowShotEvent>& GetPlayerBowShotEventData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EventDispatcher<TESPlayerBowShotEvent>>(this, CommonLibEventSourceOffsets::PlayerBowShot);
#else
        return playerBowShotEvent;
#endif
    }

    EventDispatcher<BGSEventProcessedEvent> eventProcessedEvent;
    EventDispatcher<TESActivateEvent> activateEvent;
    EventDispatcher<TESActiveEffectApplyRemove> activeEffectApplyRemove;
    EventDispatcher<TESActorLocationChangeEvent> actorLocationChangeEvent;
    EventDispatcher<TESBookReadEvent> bookReadEvent;
    EventDispatcher<TESCellAttachDetachEvent> cellAttachDetachEvent;
    EventDispatcher<TESCellFullyLoadedEvent> cellFullyLoadedEvent;
    EventDispatcher<UnknownEvent> unknownDispatcher8; // apply decals event
    EventDispatcher<TESCombatEvent> combatEvent;
    EventDispatcher<TESContainerChangedEvent> containerChangedEvent;
    EventDispatcher<TESDeathEvent> deathEvent;
    EventDispatcher<TESDestructionStageChangedEvent> destructionStageChangedEventDispatcher;
    EventDispatcher<TESEnterBleedoutEvent> enterBleedoutEvent;
    EventDispatcher<TESEquipEvent> equipEvent;
    EventDispatcher<TESFormDeleteEvent> formDeleteEvent;
    EventDispatcher<TESFurnitureEvent> furnitureEvent;
    EventDispatcher<TESGrabReleaseEvent> grabReleaseEvent;
    EventDispatcher<TESHitEvent> hitEvent; // validated
    EventDispatcher<TESInitScriptEvent> initScriptEvent;
    EventDispatcher<TESLoadGameEvent> loadGameEvent;
    EventDispatcher<TESLockChangedEvent> lockChangedEvent;
    EventDispatcher<TESMagicEffectApplyEvent> magicEffectApplyEvent;
    EventDispatcher<TESMagicWardHitEvent> magicWardHitEvent;
    EventDispatcher<TESMoveAttachDetachEvent> moveAttachDetachEvent;
    EventDispatcher<TESObjectLoadedEvent> objectLoadedEvent;
    EventDispatcher<TESObjectREFRTranslationEvent> objectREFRTranslationEvent;
    EventDispatcher<TESOpenCloseEvent> openCloseEvent;
    EventDispatcher<TESPackageEvent> packageEvent;
    EventDispatcher<TESPerkEntryRunEvent> perkEntryRunEvent;
    EventDispatcher<TESQuestInitEvent> questInitEvent; // 9F8
    EventDispatcher<TESQuestStageEvent> questStageEvent;
    EventDispatcher<TESQuestStageItemDoneEvent> questStageItemDoneEvent; // AA8, validated StageItemFinishedCallback::TriggerItemDoneEvent
    EventDispatcher<TESQuestStartStopEvent> questStartStopEvent;         // TESResolveNPCTemplatesEvent
    EventDispatcher<TESResetEvent> resetEvent;                           // validated 0xB58
    EventDispatcher<TESResolveNPCTemplatesEvent> resolveNPCTemplatesEvent;
    EventDispatcher<TESSceneEvent> sceneEvent;
    EventDispatcher<TESSceneActionEvent> sceneActionEvent;
    EventDispatcher<TESScenePhaseEvent> scenePhaseEvent;
    EventDispatcher<TESSellEvent> sellEvent;
    EventDispatcher<TESSleepStartEvent> unknownDispatcher39;
    EventDispatcher<TESSleepStopEvent> sleepStopEvent;
    EventDispatcher<TESSpellCastEvent> spellCastEvent;
    EventDispatcher<TESPlayerBowShotEvent> playerBowShotEvent;
    EventDispatcher<TESTopicInfoEvent> topicInfoEvent;
    EventDispatcher<TESTrackedStatsEvent> trackedStatsEvent;
    EventDispatcher<TESTrapHitEvent> trapHitEvent;
    EventDispatcher<TESTriggerEvent> triggerEvent;
    EventDispatcher<TESTriggerEnterEvent> triggerEnterEvent;
    EventDispatcher<TESTriggerLeaveEvent> triggerLeaveEvent;
    EventDispatcher<TESUniqueIDChangeEvent> uniqueIDChangeEvent;
    EventDispatcher<UnknownEvent> unknownDispatcher50; // waitevent
    EventDispatcher<UnknownEvent> unknownDispatcher51; // TESWaitStopEvent
    EventDispatcher<TESSwitchRaceCompleteEvent> switchRaceCompleteEvent;
#if !TP_SKYRIM_VR
    EventDispatcher<TESFastTravelEndEvent> fastTravelEndEvent;
#endif
};

// constexpr auto x = offsetof(EventDispatcherManager, unkx);

static_assert(offsetof(EventDispatcherManager, activateEvent) == EventDispatcherManager::LocalEventSourceOffsets::Activate);
static_assert(offsetof(EventDispatcherManager, grabReleaseEvent) == EventDispatcherManager::LocalEventSourceOffsets::GrabRelease);
static_assert(offsetof(EventDispatcherManager, hitEvent) == EventDispatcherManager::LocalEventSourceOffsets::Hit);
static_assert(offsetof(EventDispatcherManager, loadGameEvent) == EventDispatcherManager::LocalEventSourceOffsets::LoadGame);
static_assert(offsetof(EventDispatcherManager, magicEffectApplyEvent) == EventDispatcherManager::LocalEventSourceOffsets::MagicEffectApply);
static_assert(offsetof(EventDispatcherManager, spellCastEvent) == EventDispatcherManager::LocalEventSourceOffsets::SpellCast);
static_assert(offsetof(EventDispatcherManager, playerBowShotEvent) == EventDispatcherManager::LocalEventSourceOffsets::PlayerBowShot);
static_assert(offsetof(EventDispatcherManager, questInitEvent) == EventDispatcherManager::LocalEventSourceOffsets::QuestInit);
static_assert(offsetof(EventDispatcherManager, questStageEvent) == EventDispatcherManager::LocalEventSourceOffsets::QuestStage);
static_assert(offsetof(EventDispatcherManager, questStartStopEvent) == EventDispatcherManager::LocalEventSourceOffsets::QuestStartStop);
static_assert(offsetof(EventDispatcherManager, switchRaceCompleteEvent) == EventDispatcherManager::LocalEventSourceOffsets::SwitchRaceComplete);
#if TP_SKYRIM_VR
static_assert(sizeof(EventDispatcherManager) == EventDispatcherManager::LocalEventSourceOffsets::VrSize);
#else
static_assert(offsetof(EventDispatcherManager, fastTravelEndEvent) == EventDispatcherManager::LocalEventSourceOffsets::SeFastTravelEnd);
static_assert(sizeof(EventDispatcherManager) == EventDispatcherManager::LocalEventSourceOffsets::SeSize);
#endif
