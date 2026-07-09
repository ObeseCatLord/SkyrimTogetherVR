#pragma once

#include <AI/AITimer.h>
#include <Games/Misc/BGSWorldLocation.h>
#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct CombatTarget
{
    using CommonLibCombatTargetOffsets = Skyrim::RuntimeLayout::CombatTargetCommonLibNgOffsets;
    using LocalCombatTargetOffsets = Skyrim::RuntimeLayout::CombatTargetLocalShimOffsets;

    enum FLAGS : int16_t
    {
        NONE = 0x0,
        KNOWN = 0x1,
        LOST = 0x2,
    };

    [[nodiscard]] uint32_t GetTargetHandleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibCombatTargetOffsets::TargetHandle);
#else
        return targetHandle;
#endif
    }

    [[nodiscard]] uint16_t GetAttackerCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint16_t>(this, CommonLibCombatTargetOffsets::AttackerCount);
#else
        return attackerCount;
#endif
    }

    [[nodiscard]] FLAGS GetFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<FLAGS>(this, CommonLibCombatTargetOffsets::Flags);
#else
        return flags;
#endif
    }

    uint32_t targetHandle;
    int32_t detectLevel;
    float stealthPoints;
    float unk0C;
    BGSWorldLocation lastKnownLoc;
    BGSWorldLocation unk28;
    BGSWorldLocation unk40;
    BGSWorldLocation searchLoc;
    BGSWorldLocation unk70;
    float unk88;
    float unk8C;
    float lastKnownTimeStamp;
    float unk94;
    float unk98;
    float unk9C;
    uint32_t attackedMember;
    uint16_t attackerCount;
    FLAGS flags;
};
static_assert(offsetof(CombatTarget, targetHandle) == CombatTarget::LocalCombatTargetOffsets::TargetHandle);
static_assert(offsetof(CombatTarget, attackerCount) == CombatTarget::LocalCombatTargetOffsets::AttackerCount);
static_assert(offsetof(CombatTarget, flags) == CombatTarget::LocalCombatTargetOffsets::Flags);
static_assert(sizeof(CombatTarget) == CombatTarget::LocalCombatTargetOffsets::Size);

struct CombatMember
{
    BSPointerHandle<Actor> hMember;
    float groupStrengthUpdateTimer;
    float threatValue;
};
static_assert(sizeof(CombatMember) == 0x0C);

struct CombatSearchLocation
{
    BGSWorldLocation loc;
    float timestamp;
    float unk20;
};
static_assert(sizeof(CombatSearchLocation) == 0x20);

struct CombatSearchDoor
{
    BSPointerHandle<TESObjectREFR> hDoor;
    BSPointerHandle<TESObjectREFR> hLinkedDoor;
    uint8_t unk08;
    uint8_t unk09;
    uint8_t unk0A;
};
static_assert(sizeof(CombatSearchDoor) == 0xC);

struct CombatGroup
{
    using CommonLibCombatGroupOffsets = Skyrim::RuntimeLayout::CombatGroupCommonLibNgOffsets;
    using LocalCombatGroupOffsets = Skyrim::RuntimeLayout::CombatGroupLocalShimOffsets;

    [[nodiscard]] GameArray<CombatTarget>& GetTargetsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<CombatTarget>>(this, CommonLibCombatGroupOffsets::Targets);
#else
        return targets;
#endif
    }

    [[nodiscard]] const GameArray<CombatTarget>& GetTargetsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<CombatTarget>>(this, CommonLibCombatGroupOffsets::Targets);
#else
        return targets;
#endif
    }

    uint32_t groupID; 
    uint32_t groupIndex;
    GameArray<CombatTarget> targets; 
    GameArray<CombatMember> members; 
    CombatGroupDetectionListener* detectionListener;
    AITimer allyKilledTimer;
    AITimer avoidThreathTimer;
    AITimer unk50;
    AITimer detectionDialogueTimers[11];
    AITimer updateTimer;
    AITimer musicThreatRatioTimer;
    AITimer unkC0;
    float unkC8;
    float unkCC;
    float unkD0;
    float unkD4;
    uint32_t searchState;
    uint32_t padDC;
    BSPathingLOSGridMap*  gridMap; 
    AITimer searchUpdateTimer;
    AITimer searchAreaUpdateTimer;
    float unkF8;
    uint32_t hTargetToSearchFor;
    BGSWorldLocation searchTargetLoc;
    float searchRadius;
    uint32_t unk11C;
    GameArray<CombatSearchLocation> searchLocations;
    GameArray<CombatSearchDoor>  searchDoors;
    uint32_t unk150;
    uint32_t fleeCount;
    uint32_t fightCount;
    uint8_t musicState;
    uint8_t unk15D;
    uint8_t unk15E;
    uint8_t unk15F;
    BSReadWriteLock lock;
};
static_assert(offsetof(CombatGroup, targets) == CombatGroup::LocalCombatGroupOffsets::Targets);
static_assert(sizeof(CombatGroup) == CombatGroup::LocalCombatGroupOffsets::Size);
