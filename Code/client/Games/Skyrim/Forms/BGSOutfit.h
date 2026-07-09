#pragma once

#include <Forms/TESForm.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BGSOutfit : TESForm
{
    using CommonLibOutfitOffsets = Skyrim::RuntimeLayout::BGSOutfitCommonLibNgOffsets;
    using LocalOutfitOffsets = Skyrim::RuntimeLayout::BGSOutfitLocalShimOffsets;

    [[nodiscard]] GameArray<TESForm*>& GetOutfitItemsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<TESForm*>>(this, CommonLibOutfitOffsets::OutfitItems);
#else
        return outfitItems;
#endif
    }

    [[nodiscard]] const GameArray<TESForm*>& GetOutfitItemsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<TESForm*>>(this, CommonLibOutfitOffsets::OutfitItems);
#else
        return outfitItems;
#endif
    }

    GameArray<TESForm*> outfitItems;
};

static_assert(BGSOutfit::CommonLibOutfitOffsets::OutfitItems == 0x20);
static_assert(BGSOutfit::CommonLibOutfitOffsets::Size == 0x38);
static_assert(offsetof(BGSOutfit, outfitItems) == BGSOutfit::LocalOutfitOffsets::OutfitItems);
static_assert(sizeof(BGSOutfit) == BGSOutfit::LocalOutfitOffsets::Size);
