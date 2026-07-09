#pragma once

#include <Forms/TESBoundObject.h>
#include <Forms/EffectSetting.h>
#include <Forms/BGSKeyword.h>
#include <Magic/EffectItem.h>
#include <Components/BGSKeywordForm.h>
#include <Components/TESFullName.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct MagicItem : TESBoundObject
{
    using CommonLibMagicItemOffsets = Skyrim::RuntimeLayout::MagicItemCommonLibNgOffsets;
    using LocalMagicItemOffsets = Skyrim::RuntimeLayout::MagicItemLocalShimOffsets;

    bool IsWardSpell() const noexcept;
    bool IsInvisibilitySpell() const noexcept;
    bool IsHealingSpell() const noexcept;
    bool IsBuffSpell() const noexcept;
    bool IsBoundWeaponSpell() noexcept;

    EffectItem* GetEffect(const uint32_t aEffectId) noexcept;

    [[nodiscard]] TESFullName& GetFullNameData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibMagicItemOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] const TESFullName& GetFullNameData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibMagicItemOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] BGSKeywordForm& GetKeywordFormData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BGSKeywordForm>(this, CommonLibMagicItemOffsets::KeywordForm);
#else
        return keyword;
#endif
    }

    [[nodiscard]] const BGSKeywordForm& GetKeywordFormData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BGSKeywordForm>(this, CommonLibMagicItemOffsets::KeywordForm);
#else
        return keyword;
#endif
    }

    [[nodiscard]] GameArray<EffectItem*>& GetEffectsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<EffectItem*>>(this, CommonLibMagicItemOffsets::Effects);
#else
        return listOfEffects;
#endif
    }

    [[nodiscard]] const GameArray<EffectItem*>& GetEffectsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameArray<EffectItem*>>(this, CommonLibMagicItemOffsets::Effects);
#else
        return listOfEffects;
#endif
    }

    [[nodiscard]] int32_t GetHostileCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibMagicItemOffsets::HostileCount);
#else
        return iHostileCount;
#endif
    }

    [[nodiscard]] EffectSetting* GetAVEffectSettingData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<EffectSetting*>(this, CommonLibMagicItemOffsets::AVEffectSetting);
#else
        return pAVEffectSetting;
#endif
    }

    TESFullName fullName;
    BGSKeywordForm keyword;
    GameArray<EffectItem*> listOfEffects;
    int32_t iHostileCount;
    EffectSetting* pAVEffectSetting;
    uint32_t uiPreloadCount;
    void* pPreloadItem;
};

static_assert(MagicItem::CommonLibMagicItemOffsets::FullName == 0x30);
static_assert(MagicItem::CommonLibMagicItemOffsets::KeywordForm == 0x40);
static_assert(MagicItem::CommonLibMagicItemOffsets::Effects == 0x58);
static_assert(MagicItem::CommonLibMagicItemOffsets::HostileCount == 0x70);
static_assert(MagicItem::CommonLibMagicItemOffsets::AVEffectSetting == 0x78);
static_assert(MagicItem::CommonLibMagicItemOffsets::Size == 0x90);
static_assert(offsetof(MagicItem, fullName) == MagicItem::LocalMagicItemOffsets::FullName);
static_assert(offsetof(MagicItem, keyword) == MagicItem::LocalMagicItemOffsets::KeywordForm);
static_assert(offsetof(MagicItem, listOfEffects) == MagicItem::LocalMagicItemOffsets::Effects);
static_assert(offsetof(MagicItem, iHostileCount) == MagicItem::LocalMagicItemOffsets::HostileCount);
static_assert(offsetof(MagicItem, pAVEffectSetting) == MagicItem::LocalMagicItemOffsets::AVEffectSetting);
static_assert(sizeof(MagicItem) == MagicItem::LocalMagicItemOffsets::Size);
