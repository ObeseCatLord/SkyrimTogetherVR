#pragma once

#include <Forms/MagicItem.h>

#include <Components/BGSEquipType.h>
#include <Components/BGSMenuDisplayObject.h>
#include <Components/TESDescription.h>

struct SpellItem : MagicItem
{
    using CommonLibSpellItemOffsets = Skyrim::RuntimeLayout::SpellItemCommonLibNgOffsets;
    using LocalSpellItemOffsets = Skyrim::RuntimeLayout::SpellItemLocalShimOffsets;

    enum class SpellFlag
    {
        kNone = 0,
        kCostOverride = 1 << 0,
        kFoodItem = 1 << 1,
        kExtendDuration = 1 << 3,
        kPCStartSpell = 1 << 17,
        kInstantCast = 1 << 18,
        kIgnoreLOSCheck = 1 << 19,
        kIgnoreResistance = 1 << 20,
        kNoAbsorb = 1 << 21,
        kNoDualCastMods = 1 << 23
    };

    [[nodiscard]] MagicSystem::CastingType GetCastingTypeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::CastingType>(this, CommonLibSpellItemOffsets::CastingType);
#else
        return eCastingType;
#endif
    }

    [[nodiscard]] MagicSystem::Delivery GetDeliveryData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::Delivery>(this, CommonLibSpellItemOffsets::Delivery);
#else
        return eDelivery;
#endif
    }

    BGSEquipType equipType;
    BGSMenuDisplayObject menuDisplayObject;
    TESDescription description;
    uint32_t unk6C[3];
    float castTime;
    MagicSystem::CastingType eCastingType;
    MagicSystem::Delivery eDelivery;
    float castDuration;
    float range;
    void* pCastingPerk;
};

static_assert(SpellItem::CommonLibSpellItemOffsets::Data == 0xC0);
static_assert(SpellItem::CommonLibSpellItemOffsets::CastingType == 0xD0);
static_assert(SpellItem::CommonLibSpellItemOffsets::Delivery == 0xD4);
static_assert(SpellItem::CommonLibSpellItemOffsets::Size == 0xE8);
static_assert(offsetof(SpellItem, equipType) == SpellItem::LocalSpellItemOffsets::EquipType);
static_assert(offsetof(SpellItem, menuDisplayObject) == SpellItem::LocalSpellItemOffsets::MenuDisplayObject);
static_assert(offsetof(SpellItem, description) == SpellItem::LocalSpellItemOffsets::Description);
static_assert(offsetof(SpellItem, castTime) == SpellItem::LocalSpellItemOffsets::ChargeTime);
static_assert(offsetof(SpellItem, eCastingType) == SpellItem::LocalSpellItemOffsets::CastingType);
static_assert(offsetof(SpellItem, eDelivery) == SpellItem::LocalSpellItemOffsets::Delivery);
static_assert(offsetof(SpellItem, castDuration) == SpellItem::LocalSpellItemOffsets::CastDuration);
static_assert(offsetof(SpellItem, range) == SpellItem::LocalSpellItemOffsets::Range);
static_assert(offsetof(SpellItem, pCastingPerk) == SpellItem::LocalSpellItemOffsets::CastingPerk);
static_assert(sizeof(SpellItem) == SpellItem::LocalSpellItemOffsets::Size);
