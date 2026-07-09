#pragma once

#include "ExtraDataList.h"

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BGSLoadFormBuffer;
struct BGSSaveFormBuffer;
struct TESObjectREFR;
struct TESForm;
struct TESObjectARMO;

struct ExtraContainerChanges : BSExtraData
{
    using CommonLibContainerChangesOffsets = Skyrim::RuntimeLayout::ExtraContainerChangesCommonLibNgOffsets;
    using LocalContainerChangesOffsets = Skyrim::RuntimeLayout::ExtraContainerChangesLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::ContainerChanges;

    struct Entry
    {
        bool IsQuestObject() noexcept;
        TESForm* GetObject() noexcept;
        const TESForm* GetObject() const noexcept;
        GameList<ExtraDataList>* GetExtraLists() noexcept;
        const GameList<ExtraDataList>* GetExtraLists() const noexcept;
        int32_t GetCountDelta() const noexcept;

        // CommonLibSSE-NG names this InventoryEntryData:
        // object @ 0x00, extraLists @ 0x08, countDelta @ 0x10, size 0x18.
        TESForm* form;
        GameList<ExtraDataList>* dataList;
        int32_t count;
    };

    struct Data
    {
        void Save(BGSSaveFormBuffer* apBuffer);
        void Load(BGSLoadFormBuffer* apBuffer);
        TESObjectARMO* GetArmor(uint32_t aSlotId) noexcept;
        GameList<Entry>* GetEntryList() noexcept;
        const GameList<Entry>* GetEntryList() const noexcept;
        Entry* FindEntryByFormId(uint32_t aFormId) noexcept;
        const Entry* FindEntryByFormId(uint32_t aFormId) const noexcept;

        template <class TVisitor>
        void VisitInventory(TVisitor&& aVisitor) noexcept
        {
            auto* pEntries = GetEntryList();
            if (!pEntries)
                return;

            for (Entry* pEntry : *pEntries)
            {
                if (!pEntry)
                    continue;

                aVisitor(*pEntry);
            }
        }

        template <class TVisitor>
        void VisitInventory(TVisitor&& aVisitor) const noexcept
        {
            const auto* pEntries = GetEntryList();
            if (!pEntries)
                return;

            for (const Entry* pEntry : *pEntries)
            {
                if (!pEntry)
                    continue;

                aVisitor(*pEntry);
            }
        }

        // CommonLibSSE-NG names this pointed-to object InventoryChanges.
        GameList<Entry>* entries;
        TESObjectREFR* parent;
        float totalWeight;
        float armorWeight; // init to -1.0, maybe weight
        uint16_t unk18;    // Maybe count ? init to 0
    };

    Data* data;
};

static_assert(sizeof(ExtraContainerChanges) == ExtraContainerChanges::LocalContainerChangesOffsets::Size);
static_assert(offsetof(ExtraContainerChanges, data) == ExtraContainerChanges::LocalContainerChangesOffsets::Data);
static_assert(sizeof(ExtraContainerChanges::Entry) == ExtraContainerChanges::LocalContainerChangesOffsets::EntrySize);
static_assert(offsetof(ExtraContainerChanges::Entry, form) == ExtraContainerChanges::LocalContainerChangesOffsets::EntryObject);
static_assert(offsetof(ExtraContainerChanges::Entry, dataList) == ExtraContainerChanges::LocalContainerChangesOffsets::EntryExtraLists);
static_assert(offsetof(ExtraContainerChanges::Entry, count) == ExtraContainerChanges::LocalContainerChangesOffsets::EntryCountDelta);
static_assert(sizeof(ExtraContainerChanges::Data) == ExtraContainerChanges::LocalContainerChangesOffsets::DataSize);
static_assert(offsetof(ExtraContainerChanges::Data, entries) == ExtraContainerChanges::LocalContainerChangesOffsets::DataEntries);
static_assert(offsetof(ExtraContainerChanges::Data, parent) == ExtraContainerChanges::LocalContainerChangesOffsets::DataParent);
static_assert(offsetof(ExtraContainerChanges::Data, totalWeight) == ExtraContainerChanges::LocalContainerChangesOffsets::DataTotalWeight);
static_assert(offsetof(ExtraContainerChanges::Data, armorWeight) == ExtraContainerChanges::LocalContainerChangesOffsets::DataArmorWeight);
static_assert(offsetof(ExtraContainerChanges::Data, unk18) == ExtraContainerChanges::LocalContainerChangesOffsets::DataUnk18);
