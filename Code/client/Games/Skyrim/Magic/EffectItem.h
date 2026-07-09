#pragma once

#include <Components/TESCondition.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct EffectSetting;

struct EffectItemData
{
    float fMagnitude;
    int32_t iArea;
    int32_t iDuration;
};

struct EffectItem
{
    using CommonLibEffectItemOffsets = Skyrim::RuntimeLayout::EffectItemCommonLibNgOffsets;
    using LocalEffectItemOffsets = Skyrim::RuntimeLayout::EffectItemLocalShimOffsets;

    bool IsHealingEffect() const noexcept;
    bool IsSummonEffect() const noexcept;
    bool IsSlowEffect() const noexcept;
    bool IsInivisibilityEffect() const noexcept;
    bool IsWerewolfEffect() const noexcept;
    bool IsVampireLordEffect() const noexcept;
    bool IsNightVisionEffect() const noexcept;

    [[nodiscard]] const EffectItemData& GetEffectItemData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<EffectItemData>(this, CommonLibEffectItemOffsets::Magnitude);
#else
        return data;
#endif
    }

    void SetEffectItemData(const EffectItemData& acData) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<EffectItemData>(this, CommonLibEffectItemOffsets::Magnitude, acData);
#else
        data = acData;
#endif
    }

    [[nodiscard]] EffectSetting* GetEffectSettingData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<EffectSetting*>(this, CommonLibEffectItemOffsets::EffectSetting);
#else
        return pEffectSetting;
#endif
    }

    void SetEffectSettingData(EffectSetting* apEffectSetting) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<EffectSetting*>(this, CommonLibEffectItemOffsets::EffectSetting, apEffectSetting);
#else
        pEffectSetting = apEffectSetting;
#endif
    }

    [[nodiscard]] float GetRawCostData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibEffectItemOffsets::RawCost);
#else
        return fRawCost;
#endif
    }

    void SetRawCostData(float aRawCost) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibEffectItemOffsets::RawCost, aRawCost);
#else
        fRawCost = aRawCost;
#endif
    }

    EffectItemData data;
    uint32_t padC;
    EffectSetting* pEffectSetting;
    float fRawCost;
    TESCondition Condition{};
};

static_assert(sizeof(EffectItemData) == 0xC);
static_assert(EffectItem::CommonLibEffectItemOffsets::Magnitude == 0x0);
static_assert(EffectItem::CommonLibEffectItemOffsets::EffectSetting == 0x10);
static_assert(EffectItem::CommonLibEffectItemOffsets::RawCost == 0x18);
static_assert(offsetof(EffectItem, data) == EffectItem::LocalEffectItemOffsets::Magnitude);
static_assert(offsetof(EffectItem, pEffectSetting) == EffectItem::LocalEffectItemOffsets::EffectSetting);
static_assert(offsetof(EffectItem, fRawCost) == EffectItem::LocalEffectItemOffsets::RawCost);
static_assert(sizeof(EffectItem) == EffectItem::LocalEffectItemOffsets::Size);
