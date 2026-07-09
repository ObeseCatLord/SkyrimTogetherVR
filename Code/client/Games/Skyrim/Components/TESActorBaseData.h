#pragma once

#include <Components/BaseFormComponent.h>
#include <Games/Primitives.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BGSVoiceType;
struct TESFaction;

struct TESActorBaseData : BaseFormComponent
{
    using CommonLibActorBaseDataOffsets = Skyrim::RuntimeLayout::TESActorBaseDataCommonLibNgOffsets;
    using LocalActorBaseDataOffsets = Skyrim::RuntimeLayout::TESActorBaseDataLocalShimOffsets;

    enum BaseFlags
    {
        IS_ESSENTIAL = 1 << 1,
    };

    uint32_t flags;
    uint16_t unk08;
    uint16_t unk0A;
    uint16_t level;
    uint16_t minLevel;
    uint16_t maxLevel;
    uint16_t unk12;
    uint16_t unk14;
    uint16_t unk16;
    uint16_t unk18;
    uint16_t unk1A;
    void* unk1C;
    BGSVoiceType* voiceType;
    TESForm* owner;
    uint32_t unk28;

    struct alignas(sizeof(void*)) FactionInfo
    {
        TESFaction* faction;
        int8_t rank;
    };

    [[nodiscard]] uint32_t GetFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibActorBaseDataOffsets::Flags);
#else
        return flags;
#endif
    }

    void SetFlagsData(uint32_t aFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibActorBaseDataOffsets::Flags, aFlags);
#else
        flags = aFlags;
#endif
    }

    [[nodiscard]] GameArray<FactionInfo>& GetFactionsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<FactionInfo>>(this, CommonLibActorBaseDataOffsets::Factions);
#else
        return factions;
#endif
    }

    [[nodiscard]] const GameArray<FactionInfo>& GetFactionsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<FactionInfo>>(this, CommonLibActorBaseDataOffsets::Factions);
#else
        return factions;
#endif
    }

    bool IsEssential() const noexcept { return (GetFlagsData() & BaseFlags::IS_ESSENTIAL) != 0; }
    void SetEssential(bool aSet) noexcept
    {
        auto baseFlags = GetFlagsData();
        if (aSet)
            baseFlags |= BaseFlags::IS_ESSENTIAL;
        else
            baseFlags &= ~BaseFlags::IS_ESSENTIAL;

        SetFlagsData(baseFlags);
    }

    GameArray<FactionInfo> factions;
};

static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Flags == 0x8);
static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Level == 0x10);
static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::BaseTemplateForm == 0x30);
static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Factions == 0x40);
static_assert(TESActorBaseData::CommonLibActorBaseDataOffsets::Size == 0x58);
static_assert(offsetof(TESActorBaseData, flags) == TESActorBaseData::LocalActorBaseDataOffsets::Flags);
static_assert(offsetof(TESActorBaseData, level) == TESActorBaseData::LocalActorBaseDataOffsets::Level);
static_assert(offsetof(TESActorBaseData, owner) == TESActorBaseData::LocalActorBaseDataOffsets::Owner);
static_assert(offsetof(TESActorBaseData, factions) == TESActorBaseData::LocalActorBaseDataOffsets::Factions);
static_assert(sizeof(TESActorBaseData) == TESActorBaseData::LocalActorBaseDataOffsets::Size);
