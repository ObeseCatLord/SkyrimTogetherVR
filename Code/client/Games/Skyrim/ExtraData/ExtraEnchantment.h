#pragma once

#include "ExtraData.h"

#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct EnchantmentItem;

struct ExtraEnchantment : BSExtraData
{
    using CommonLibEnchantmentOffsets = Skyrim::RuntimeLayout::ExtraEnchantmentCommonLibNgOffsets;
    using LocalEnchantmentOffsets = Skyrim::RuntimeLayout::ExtraEnchantmentLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::Enchantment;

    [[nodiscard]] EnchantmentItem* GetEnchantmentData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<EnchantmentItem*>(this, CommonLibEnchantmentOffsets::Enchantment);
#else
        return pEnchantment;
#endif
    }

    [[nodiscard]] uint16_t GetChargeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint16_t>(this, CommonLibEnchantmentOffsets::Charge);
#else
        return usCharge;
#endif
    }

    [[nodiscard]] bool GetRemoveOnUnequipData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibEnchantmentOffsets::RemoveOnUnequip);
#else
        return bRemoveOnUnequip;
#endif
    }

    EnchantmentItem* pEnchantment;
    uint16_t usCharge;
    bool bRemoveOnUnequip;
};

static_assert(offsetof(ExtraEnchantment, pEnchantment) == ExtraEnchantment::LocalEnchantmentOffsets::Enchantment);
static_assert(offsetof(ExtraEnchantment, usCharge) == ExtraEnchantment::LocalEnchantmentOffsets::Charge);
static_assert(offsetof(ExtraEnchantment, bRemoveOnUnequip) == ExtraEnchantment::LocalEnchantmentOffsets::RemoveOnUnequip);
static_assert(sizeof(ExtraEnchantment) == ExtraEnchantment::LocalEnchantmentOffsets::Size);
