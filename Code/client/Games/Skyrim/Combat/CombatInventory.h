#pragma once

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct CombatInventory
{
    using CommonLibCombatInventoryOffsets = Skyrim::RuntimeLayout::CombatInventoryCommonLibNgOffsets;
    using LocalCombatInventoryOffsets = Skyrim::RuntimeLayout::CombatInventoryLocalShimOffsets;

    [[nodiscard]] float GetMaximumRangeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibCombatInventoryOffsets::MaximumRange);
#else
        return maximumRange;
#endif
    }

    uint8_t unk0[0x1B8];
    float maximumRange;
    uint8_t unk1BC[0x1C8 - 0x1BC];
};
static_assert(offsetof(CombatInventory, maximumRange) == CombatInventory::LocalCombatInventoryOffsets::MaximumRange);
static_assert(sizeof(CombatInventory) == CombatInventory::LocalCombatInventoryOffsets::Size);
