#pragma once

#include "ExtraData.h"

#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct ExtraHealth : BSExtraData
{
    using CommonLibHealthOffsets = Skyrim::RuntimeLayout::ExtraHealthCommonLibNgOffsets;
    using LocalHealthOffsets = Skyrim::RuntimeLayout::ExtraHealthLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Health;

    [[nodiscard]] float GetHealthData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibHealthOffsets::Health);
#else
        return fHealth;
#endif
    }

    float fHealth{};
};

static_assert(offsetof(ExtraHealth, fHealth) == ExtraHealth::LocalHealthOffsets::Health);
static_assert(sizeof(ExtraHealth) == ExtraHealth::LocalHealthOffsets::Size);
