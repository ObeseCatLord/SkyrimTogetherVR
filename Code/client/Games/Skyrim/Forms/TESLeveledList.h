#pragma once

#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESLeveledList : BaseFormComponent
{
    using CommonLibLeveledListOffsets = Skyrim::RuntimeLayout::TESLeveledListCommonLibNgOffsets;
    using LocalLeveledListOffsets = Skyrim::RuntimeLayout::TESLeveledListLocalShimOffsets;

    struct LEVELED_OBJECT
    {
        using CommonLibLeveledListOffsets = Skyrim::RuntimeLayout::TESLeveledListCommonLibNgOffsets;
        using LocalLeveledListOffsets = Skyrim::RuntimeLayout::TESLeveledListLocalShimOffsets;

        [[nodiscard]] TESForm* GetFormData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESForm*>(this, CommonLibLeveledListOffsets::EntryForm);
#else
            return pForm;
#endif
        }

        TESForm* pForm;
        uint16_t count;
        uint16_t level;
        uint32_t padC;
        void* pItemExtra;
    };

    [[nodiscard]] LEVELED_OBJECT* GetEntriesData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<LEVELED_OBJECT*>(this, CommonLibLeveledListOffsets::Entries);
#else
        return pLeveledListA;
#endif
    }

    [[nodiscard]] const LEVELED_OBJECT* GetEntriesData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<LEVELED_OBJECT*>(this, CommonLibLeveledListOffsets::Entries);
#else
        return pLeveledListA;
#endif
    }

    [[nodiscard]] uint8_t GetEntryCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint8_t>(this, CommonLibLeveledListOffsets::NumEntries);
#else
        return numEntries;
#endif
    }

    LEVELED_OBJECT* pLeveledListA; // this array has count at offset -8
    uint8_t chanceNone;
    uint8_t flags;
    uint8_t numEntries;
    uint8_t unk13;
    uint8_t unk14[0x28 - 0x14];
};

static_assert(sizeof(TESLeveledList::LEVELED_OBJECT) == 0x18);
static_assert(TESLeveledList::CommonLibLeveledListOffsets::EntryForm == 0x0);
static_assert(TESLeveledList::CommonLibLeveledListOffsets::EntrySize == 0x18);
static_assert(TESLeveledList::CommonLibLeveledListOffsets::Entries == 0x8);
static_assert(TESLeveledList::CommonLibLeveledListOffsets::NumEntries == 0x12);
static_assert(TESLeveledList::CommonLibLeveledListOffsets::Size == 0x28);
static_assert(offsetof(TESLeveledList::LEVELED_OBJECT, pForm) == TESLeveledList::LocalLeveledListOffsets::EntryForm);
static_assert(offsetof(TESLeveledList::LEVELED_OBJECT, count) == TESLeveledList::LocalLeveledListOffsets::EntryCount);
static_assert(offsetof(TESLeveledList::LEVELED_OBJECT, level) == TESLeveledList::LocalLeveledListOffsets::EntryLevel);
static_assert(offsetof(TESLeveledList::LEVELED_OBJECT, pItemExtra) == TESLeveledList::LocalLeveledListOffsets::EntryExtra);
static_assert(offsetof(TESLeveledList, pLeveledListA) == TESLeveledList::LocalLeveledListOffsets::Entries);
static_assert(offsetof(TESLeveledList, chanceNone) == TESLeveledList::LocalLeveledListOffsets::ChanceNone);
static_assert(offsetof(TESLeveledList, flags) == TESLeveledList::LocalLeveledListOffsets::Flags);
static_assert(offsetof(TESLeveledList, numEntries) == TESLeveledList::LocalLeveledListOffsets::NumEntries);
static_assert(sizeof(TESLeveledList) == TESLeveledList::LocalLeveledListOffsets::Size);
