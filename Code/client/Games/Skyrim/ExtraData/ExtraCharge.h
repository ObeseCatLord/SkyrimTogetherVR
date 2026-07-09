#pragma once

#include "ExtraData.h"

#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct ExtraCharge : BSExtraData
{
    using CommonLibChargeOffsets = Skyrim::RuntimeLayout::ExtraChargeCommonLibNgOffsets;
    using LocalChargeOffsets = Skyrim::RuntimeLayout::ExtraChargeLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Charge;

    [[nodiscard]] float GetChargeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibChargeOffsets::Charge);
#else
        return fCharge;
#endif
    }

    float fCharge{};
};

static_assert(offsetof(ExtraCharge, fCharge) == ExtraCharge::LocalChargeOffsets::Charge);
static_assert(sizeof(ExtraCharge) == ExtraCharge::LocalChargeOffsets::Size);
