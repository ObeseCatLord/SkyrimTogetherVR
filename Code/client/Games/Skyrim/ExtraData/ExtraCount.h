#pragma once

#include "ExtraData.h"

#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct ExtraCount : BSExtraData
{
    using CommonLibCountOffsets = Skyrim::RuntimeLayout::ExtraCountCommonLibNgOffsets;
    using LocalCountOffsets = Skyrim::RuntimeLayout::ExtraCountLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Count;

    [[nodiscard]] int16_t GetCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int16_t>(this, CommonLibCountOffsets::Count);
#else
        return count;
#endif
    }

    int16_t count;
};

static_assert(offsetof(ExtraCount, count) == ExtraCount::LocalCountOffsets::Count);
static_assert(sizeof(ExtraCount) == ExtraCount::LocalCountOffsets::Size);
