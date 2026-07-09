#pragma once

#include <Forms/TESForm.h>
#include <Components/TESFullName.h>
#include <Components/BGSKeywordForm.h>
#include <Components/BGSMenuDisplayObject.h>
#include <Games/Magic/MagicSystem.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct EffectSetting : TESForm
{
    using CommonLibEffectSettingOffsets = Skyrim::RuntimeLayout::EffectSettingCommonLibNgOffsets;
    using LocalEffectSettingOffsets = Skyrim::RuntimeLayout::EffectSettingLocalShimOffsets;

    [[nodiscard]] TESFullName& GetFullNameData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibEffectSettingOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] const TESFullName& GetFullNameData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibEffectSettingOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] BGSKeywordForm& GetKeywordFormData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BGSKeywordForm>(this, CommonLibEffectSettingOffsets::KeywordForm);
#else
        return keywordForm;
#endif
    }

    [[nodiscard]] const BGSKeywordForm& GetKeywordFormData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BGSKeywordForm>(this, CommonLibEffectSettingOffsets::KeywordForm);
#else
        return keywordForm;
#endif
    }

    [[nodiscard]] EffectArchetypes::ArchetypeID GetArchetypeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<EffectArchetypes::ArchetypeID>(this, CommonLibEffectSettingOffsets::Archetype);
#else
        return eArchetype;
#endif
    }

    TESFullName fullName;
    BGSMenuDisplayObject menuDisplayObject;
    BGSKeywordForm keywordForm;
    uintptr_t unk30;
    uintptr_t unk34;
    uint32_t flags;
    uint32_t unk3C;
    void* unk40;
    uint32_t unk44;
    uint32_t unk48;
    uint16_t unk4C;
    uint8_t pad4E[2];
    void* unk50;
    uint32_t unk54;
    void* unk58;
    void* unk5C;
    uint8_t unk60[0x18];
    EffectArchetypes::ArchetypeID eArchetype;
    void* unk80[2];
    uint32_t castType;
    uint32_t deliveryType;
    // more stuff
};

static_assert(EffectSetting::CommonLibEffectSettingOffsets::FullName == 0x20);
static_assert(EffectSetting::CommonLibEffectSettingOffsets::KeywordForm == 0x40);
static_assert(EffectSetting::CommonLibEffectSettingOffsets::Archetype == 0xC0);
static_assert(offsetof(EffectSetting, fullName) == EffectSetting::LocalEffectSettingOffsets::FullName);
static_assert(offsetof(EffectSetting, keywordForm) == EffectSetting::LocalEffectSettingOffsets::KeywordForm);
static_assert(offsetof(EffectSetting, eArchetype) == EffectSetting::LocalEffectSettingOffsets::Archetype);
