#pragma once

#include "MagicItem.h"

#include <RuntimeLayout.h>
#include <Structs/Inventory.h>
#include <Games/Magic/MagicSystem.h>
#include "BGSListForm.h"

#include <cstddef>

struct EnchantmentItem : MagicItem
{
    using CommonLibEnchantmentItemOffsets = Skyrim::RuntimeLayout::EnchantmentItemCommonLibNgOffsets;
    using LocalEnchantmentItemOffsets = Skyrim::RuntimeLayout::EnchantmentItemLocalShimOffsets;

    static EnchantmentItem* Create(const Inventory::EnchantmentData& aData) noexcept;

    [[nodiscard]] int32_t GetCostOverrideData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibEnchantmentItemOffsets::CostOverride);
#else
        return iCostOverride;
#endif
    }

    [[nodiscard]] int32_t GetFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibEnchantmentItemOffsets::Flags);
#else
        return iFlags;
#endif
    }

    [[nodiscard]] MagicSystem::CastingType GetCastingTypeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::CastingType>(this, CommonLibEnchantmentItemOffsets::CastingType);
#else
        return eCastingType;
#endif
    }

    [[nodiscard]] int32_t GetChargeOverrideData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibEnchantmentItemOffsets::ChargeOverride);
#else
        return iChargeOverride;
#endif
    }

    [[nodiscard]] MagicSystem::Delivery GetDeliveryData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::Delivery>(this, CommonLibEnchantmentItemOffsets::Delivery);
#else
        return eDelivery;
#endif
    }

    [[nodiscard]] MagicSystem::SpellType GetSpellTypeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::SpellType>(this, CommonLibEnchantmentItemOffsets::SpellType);
#else
        return eSpellType;
#endif
    }

    [[nodiscard]] float GetChargeTimeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibEnchantmentItemOffsets::ChargeTime);
#else
        return fChargeTime;
#endif
    }

    [[nodiscard]] EnchantmentItem* GetBaseEnchantmentData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<EnchantmentItem*>(this, CommonLibEnchantmentItemOffsets::BaseEnchantment);
#else
        return pBaseEnchantment;
#endif
    }

    [[nodiscard]] BGSListForm* GetWornRestrictionsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSListForm*>(this, CommonLibEnchantmentItemOffsets::WornRestrictions);
#else
        return pWornRestrictions;
#endif
    }

    int32_t iCostOverride;
    int32_t iFlags;
    MagicSystem::CastingType eCastingType;
    int32_t iChargeOverride;
    MagicSystem::Delivery eDelivery;
    MagicSystem::SpellType eSpellType;
    float fChargeTime;
    EnchantmentItem* pBaseEnchantment;
    BGSListForm* pWornRestrictions;
};

static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::Data == 0x90);
static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::CastingType == 0x98);
static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::Delivery == 0xA0);
static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::BaseEnchantment == 0xB0);
static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::WornRestrictions == 0xB8);
static_assert(EnchantmentItem::CommonLibEnchantmentItemOffsets::Size == 0xC0);
static_assert(offsetof(EnchantmentItem, iCostOverride) == EnchantmentItem::LocalEnchantmentItemOffsets::CostOverride);
static_assert(offsetof(EnchantmentItem, iFlags) == EnchantmentItem::LocalEnchantmentItemOffsets::Flags);
static_assert(offsetof(EnchantmentItem, eCastingType) == EnchantmentItem::LocalEnchantmentItemOffsets::CastingType);
static_assert(offsetof(EnchantmentItem, iChargeOverride) == EnchantmentItem::LocalEnchantmentItemOffsets::ChargeOverride);
static_assert(offsetof(EnchantmentItem, eDelivery) == EnchantmentItem::LocalEnchantmentItemOffsets::Delivery);
static_assert(offsetof(EnchantmentItem, eSpellType) == EnchantmentItem::LocalEnchantmentItemOffsets::SpellType);
static_assert(offsetof(EnchantmentItem, fChargeTime) == EnchantmentItem::LocalEnchantmentItemOffsets::ChargeTime);
static_assert(offsetof(EnchantmentItem, pBaseEnchantment) == EnchantmentItem::LocalEnchantmentItemOffsets::BaseEnchantment);
static_assert(offsetof(EnchantmentItem, pWornRestrictions) == EnchantmentItem::LocalEnchantmentItemOffsets::WornRestrictions);
static_assert(sizeof(EnchantmentItem) == EnchantmentItem::LocalEnchantmentItemOffsets::Size);
