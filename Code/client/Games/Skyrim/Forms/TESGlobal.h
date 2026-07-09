#pragma once

#include <Forms/TESForm.h>
#include <Misc/BSString.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESGlobal : TESForm
{
    using CommonLibGlobalOffsets = Skyrim::RuntimeLayout::TESGlobalCommonLibNgOffsets;
    using LocalGlobalOffsets = Skyrim::RuntimeLayout::TESGlobalLocalShimOffsets;

    [[nodiscard]] float GetValueData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibGlobalOffsets::Value);
#else
        return f;
#endif
    }

    void SetValueData(float aValue) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibGlobalOffsets::Value, aValue);
#else
        f = aValue;
#endif
    }

    BSString unk14;
    uint8_t unk1C;
    uint8_t pad[3];
    union
    {
        uint32_t i;
        float f;
    };
};

static_assert(TESGlobal::CommonLibGlobalOffsets::Value == 0x34);
static_assert(TESGlobal::CommonLibGlobalOffsets::Size == 0x38);
static_assert(offsetof(TESGlobal, f) == TESGlobal::LocalGlobalOffsets::Value);
static_assert(sizeof(TESGlobal) == TESGlobal::LocalGlobalOffsets::Size);
