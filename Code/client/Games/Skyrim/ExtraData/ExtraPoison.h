#pragma once

#include "ExtraData.h"

#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct AlchemyItem;

struct ExtraPoison : BSExtraData
{
    using CommonLibPoisonOffsets = Skyrim::RuntimeLayout::ExtraPoisonCommonLibNgOffsets;
    using LocalPoisonOffsets = Skyrim::RuntimeLayout::ExtraPoisonLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Poison;

    [[nodiscard]] AlchemyItem* GetPoisonData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<AlchemyItem*>(this, CommonLibPoisonOffsets::Poison);
#else
        return pPoison;
#endif
    }

    [[nodiscard]] uint32_t GetCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibPoisonOffsets::Count);
#else
        return uiCount;
#endif
    }

    AlchemyItem* pPoison{};
    uint32_t uiCount{};
};

static_assert(offsetof(ExtraPoison, pPoison) == ExtraPoison::LocalPoisonOffsets::Poison);
static_assert(offsetof(ExtraPoison, uiCount) == ExtraPoison::LocalPoisonOffsets::Count);
static_assert(sizeof(ExtraPoison) == ExtraPoison::LocalPoisonOffsets::Size);
