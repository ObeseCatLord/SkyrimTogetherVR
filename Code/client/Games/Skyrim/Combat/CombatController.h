#pragma once

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct CombatTargetSelector;
struct CombatGroup;
struct CombatState;
struct CombatInventory;
struct CombatAimController;
struct CombatTargetSelector;

struct CombatController
{
    using CommonLibCombatControllerOffsets = Skyrim::RuntimeLayout::CombatControllerCommonLibNgOffsets;
    using LocalCombatControllerOffsets = Skyrim::RuntimeLayout::CombatControllerLocalShimOffsets;

    void SetTarget(Actor* apTarget);
    void UpdateTarget();

    [[nodiscard]] CombatGroup* GetCombatGroupData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<CombatGroup*>(this, CommonLibCombatControllerOffsets::CombatGroup);
#else
        return pCombatGroup;
#endif
    }

    [[nodiscard]] CombatInventory* GetInventoryData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<CombatInventory*>(this, CommonLibCombatControllerOffsets::Inventory);
#else
        return pInventory;
#endif
    }

    [[nodiscard]] uint32_t GetAttackerHandleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibCombatControllerOffsets::AttackerHandle);
#else
        return attackerHandle;
#endif
    }

    [[nodiscard]] uint32_t GetTargetHandleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibCombatControllerOffsets::TargetHandle);
#else
        return targetHandle;
#endif
    }

    [[nodiscard]] bool GetStartedCombatData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibCombatControllerOffsets::StartedCombat);
#else
        return startedCombat;
#endif
    }

    [[nodiscard]] GameArray<CombatTargetSelector*>& GetTargetSelectorsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<CombatTargetSelector*>>(this, CommonLibCombatControllerOffsets::TargetSelectors);
#else
        return targetSelectors;
#endif
    }

    [[nodiscard]] const GameArray<CombatTargetSelector*>& GetTargetSelectorsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<CombatTargetSelector*>>(this, CommonLibCombatControllerOffsets::TargetSelectors);
#else
        return targetSelectors;
#endif
    }

    [[nodiscard]] CombatTargetSelector* GetActiveTargetSelectorData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<CombatTargetSelector*>(this, CommonLibCombatControllerOffsets::ActiveTargetSelector);
#else
        return pActiveTargetSelector;
#endif
    }

    void SetActiveTargetSelectorData(CombatTargetSelector* apSelector) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<CombatTargetSelector*>(this, CommonLibCombatControllerOffsets::ActiveTargetSelector,
                                                           apSelector);
#else
        pActiveTargetSelector = apSelector;
#endif
    }

    [[nodiscard]] Actor* GetCachedAttackerData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<NiPointer<Actor>>(this, CommonLibCombatControllerOffsets::CachedAttacker)
            .object;
#else
        return pCachedAttacker.object;
#endif
    }

    CombatGroup *pCombatGroup;
    CombatState *pState;
    CombatInventory *pInventory;
    void *pCombatBlackboard;
    void *pBehaviorController;
    uint32_t attackerHandle;
    uint32_t targetHandle;
    uint32_t previousTargetHandle;
    uint8_t unk34;
    bool startedCombat;
    uint8_t unk36;
    uint8_t unk37;
    TESCombatStyle *pCombatStyle;
    bool stoppedCombat;
    bool unk41;
    bool ignoringCombat;
    bool inactive;
    float unk44;
    float unk4C;
    GameArray<void*> aimControllers;
    uint64_t aimControllerLock;
    CombatAimController *pCurrentAimController;
    CombatAimController *pPreviousAimController;
    GameArray<void*> areas;
    CombatAreaStandard *pCurrentArea;
    GameArray<CombatTargetSelector*> targetSelectors;
    CombatTargetSelector *pActiveTargetSelector;
    CombatTargetSelector *pPreviousTargetSelector;
    uint32_t handleCount;
    int32_t unkCC;
    NiPointer<Actor> pCachedAttacker;
    NiPointer<Actor> pCachedTarget;
};

static_assert(CombatController::CommonLibCombatControllerOffsets::TargetSelectors == 0x98);
static_assert(CombatController::CommonLibCombatControllerOffsets::ActiveTargetSelector == 0xB0);
static_assert(CombatController::CommonLibCombatControllerOffsets::CachedAttacker == 0xC8);
static_assert(CombatController::CommonLibCombatControllerOffsets::Size == 0xD8);
static_assert(offsetof(CombatController, pCombatGroup) == CombatController::LocalCombatControllerOffsets::CombatGroup);
static_assert(offsetof(CombatController, pInventory) == CombatController::LocalCombatControllerOffsets::Inventory);
static_assert(offsetof(CombatController, attackerHandle) == CombatController::LocalCombatControllerOffsets::AttackerHandle);
static_assert(offsetof(CombatController, targetHandle) == CombatController::LocalCombatControllerOffsets::TargetHandle);
static_assert(offsetof(CombatController, startedCombat) == CombatController::LocalCombatControllerOffsets::StartedCombat);
static_assert(offsetof(CombatController, targetSelectors) == CombatController::LocalCombatControllerOffsets::TargetSelectors);
static_assert(offsetof(CombatController, pActiveTargetSelector) == CombatController::LocalCombatControllerOffsets::ActiveTargetSelector);
static_assert(offsetof(CombatController, pCachedAttacker) == CombatController::LocalCombatControllerOffsets::CachedAttacker);
static_assert(sizeof(CombatController) == CombatController::LocalCombatControllerOffsets::Size);
