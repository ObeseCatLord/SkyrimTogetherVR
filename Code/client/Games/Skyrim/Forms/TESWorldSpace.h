#pragma once

#include <Forms/TESForm.h>

#include <Components/TESFullName.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESWorldSpace : TESForm
{
    using CommonLibWorldSpaceOffsets = Skyrim::RuntimeLayout::TESWorldSpaceCommonLibNgOffsets;
    using LocalWorldSpaceOffsets = Skyrim::RuntimeLayout::TESWorldSpaceLocalShimOffsets;

    // aX and aY are coordinates, not positions
    TESObjectCELL* LoadCell(int32_t aXCoordinate, int32_t aYCoordinate) noexcept;

    [[nodiscard]] TESFullName& GetFullNameData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibWorldSpaceOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] const TESFullName& GetFullNameData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibWorldSpaceOffsets::FullName);
#else
        return fullName;
#endif
    }

    TESFullName fullName;
};

static_assert(TESWorldSpace::CommonLibWorldSpaceOffsets::FullName == 0x20);
static_assert(offsetof(TESWorldSpace, fullName) == TESWorldSpace::LocalWorldSpaceOffsets::FullName);
