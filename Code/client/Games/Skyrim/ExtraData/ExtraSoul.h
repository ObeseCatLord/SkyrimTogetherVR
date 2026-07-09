#pragma once

#include "ExtraData.h"

#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

enum class SOUL_LEVEL
{
    SOUL_NONE = 0x0,
    SOUL_PETTY = 0x1,
    SOUL_LESSER = 0x2,
    SOUL_COMMON = 0x3,
    SOUL_GREATER = 0x4,
    SOUL_GRAND = 0x5,
    SOUL_LEVEL_COUNT = 0x6,
};

struct ExtraSoul : BSExtraData
{
    using CommonLibSoulOffsets = Skyrim::RuntimeLayout::ExtraSoulCommonLibNgOffsets;
    using LocalSoulOffsets = Skyrim::RuntimeLayout::ExtraSoulLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Soul;

    [[nodiscard]] SOUL_LEVEL GetSoulData() const noexcept
    {
#if TP_SKYRIM_VR
        return static_cast<SOUL_LEVEL>(Skyrim::RuntimeLayout::Value<uint8_t>(this, CommonLibSoulOffsets::Soul));
#else
        return cSoul;
#endif
    }

    SOUL_LEVEL cSoul{};
};

static_assert(offsetof(ExtraSoul, cSoul) == ExtraSoul::LocalSoulOffsets::Soul);
static_assert(sizeof(ExtraSoul) == ExtraSoul::LocalSoulOffsets::Size);
