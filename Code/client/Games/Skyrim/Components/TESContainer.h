#pragma once

#include <Components/BaseFormComponent.h>
#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESForm;

struct TESContainer : BaseFormComponent
{
    using CommonLibContainerOffsets = Skyrim::RuntimeLayout::TESContainerCommonLibNgOffsets;
    using LocalContainerOffsets = Skyrim::RuntimeLayout::TESContainerLocalShimOffsets;

    struct Entry
    {
        using CommonLibEntryOffsets = Skyrim::RuntimeLayout::TESContainerCommonLibNgOffsets;
        using LocalEntryOffsets = Skyrim::RuntimeLayout::TESContainerLocalShimOffsets;

        [[nodiscard]] uint32_t GetCountData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibEntryOffsets::EntryCount);
#else
            return count;
#endif
        }

        [[nodiscard]] TESForm* GetFormData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESForm*>(this, CommonLibEntryOffsets::EntryForm);
#else
            return form;
#endif
        }

        // CommonLibSSE-NG names this ContainerObject.
        uint32_t count;
        TESForm* form;
        void* data;
    };

    int64_t GetItemCount(TESForm* apItem) const noexcept;

    [[nodiscard]] Entry** GetEntriesData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<Entry**>(this, CommonLibContainerOffsets::Entries);
#else
        return entries;
#endif
    }

    [[nodiscard]] uint32_t GetEntryCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibContainerOffsets::Count);
#else
        return count;
#endif
    }

    Entry** entries;
    uint32_t count;
};

static_assert(sizeof(TESContainer::Entry) == TESContainer::Entry::LocalEntryOffsets::EntrySize);
static_assert(offsetof(TESContainer::Entry, count) == TESContainer::Entry::LocalEntryOffsets::EntryCount);
static_assert(offsetof(TESContainer::Entry, form) == TESContainer::Entry::LocalEntryOffsets::EntryForm);
static_assert(offsetof(TESContainer::Entry, data) == TESContainer::Entry::LocalEntryOffsets::EntryData);
static_assert(sizeof(TESContainer) == TESContainer::LocalContainerOffsets::Size);
static_assert(offsetof(TESContainer, entries) == TESContainer::LocalContainerOffsets::Entries);
static_assert(offsetof(TESContainer, count) == TESContainer::LocalContainerOffsets::Count);
