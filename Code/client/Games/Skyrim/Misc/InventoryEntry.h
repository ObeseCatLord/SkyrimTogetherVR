#pragma once

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESBoundObject;
struct ExtraDataList;

struct InventoryEntry
{
    using CommonLibInventoryEntryOffsets = Skyrim::RuntimeLayout::InventoryEntryCommonLibNgOffsets;
    using LocalInventoryEntryOffsets = Skyrim::RuntimeLayout::InventoryEntryLocalShimOffsets;

    [[nodiscard]] TESBoundObject* GetObjectData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESBoundObject*>(this, CommonLibInventoryEntryOffsets::Object);
#else
        return pObject;
#endif
    }

    [[nodiscard]] GameList<ExtraDataList>* GetExtraListsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<GameList<ExtraDataList>*>(this, CommonLibInventoryEntryOffsets::ExtraLists);
#else
        return pExtraLists;
#endif
    }

    [[nodiscard]] int32_t GetCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibInventoryEntryOffsets::Count);
#else
        return count;
#endif
    }

    // CommonLibSSE-NG names this InventoryEntryData.
    TESBoundObject* pObject;
    GameList<ExtraDataList>* pExtraLists;
    int32_t count;
};

static_assert(sizeof(InventoryEntry) == InventoryEntry::LocalInventoryEntryOffsets::Size);
static_assert(offsetof(InventoryEntry, pObject) == InventoryEntry::LocalInventoryEntryOffsets::Object);
static_assert(offsetof(InventoryEntry, pExtraLists) == InventoryEntry::LocalInventoryEntryOffsets::ExtraLists);
static_assert(offsetof(InventoryEntry, count) == InventoryEntry::LocalInventoryEntryOffsets::Count);
