#pragma once

#include <ExtraData.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESFaction;

struct ExtraFactionChanges : BSExtraData
{
    using CommonLibFactionChangesOffsets = Skyrim::RuntimeLayout::ExtraFactionChangesCommonLibNgOffsets;
    using LocalFactionChangesOffsets = Skyrim::RuntimeLayout::ExtraFactionChangesLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Faction;

    virtual ~ExtraFactionChanges();

    struct Entry
    {
        TESFaction* faction;
        int8_t rank;
    };

    [[nodiscard]] GameArray<Entry>& GetEntriesData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<Entry>>(this, CommonLibFactionChangesOffsets::Entries);
#else
        return entries;
#endif
    }

    [[nodiscard]] const GameArray<Entry>& GetEntriesData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<Entry>>(this, CommonLibFactionChangesOffsets::Entries);
#else
        return entries;
#endif
    }

    GameArray<Entry> entries;
};

static_assert(sizeof(ExtraFactionChanges::Entry) == 0x10);
static_assert(ExtraFactionChanges::CommonLibFactionChangesOffsets::Entries == 0x10);
static_assert(ExtraFactionChanges::CommonLibFactionChangesOffsets::Size == 0x38);
static_assert(offsetof(ExtraFactionChanges, entries) == ExtraFactionChanges::LocalFactionChangesOffsets::Entries);
static_assert(sizeof(ExtraFactionChanges) == ExtraFactionChanges::LocalFactionChangesOffsets::Size);
