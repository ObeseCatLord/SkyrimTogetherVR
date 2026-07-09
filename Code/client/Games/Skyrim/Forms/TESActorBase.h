#pragma once

#include <Forms/TESBoundAnimObject.h>

#include <Components/BGSDestructibleObjectForm.h>
#include <Components/BGSSkinForm.h>
#include <Components/BGSKeywordForm.h>
#include <Components/BGSAttackDataForm.h>
#include <Components/BGSPerkRankArray.h>
#include <Components/TESActorBaseData.h>
#include <Components/TESContainer.h>
#include <Components/TESSpellList.h>
#include <Components/TESAIForm.h>
#include <Components/TESFullName.h>
#include <Misc/ActorValueOwner.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESActorBase : TESBoundAnimObject
{
    using CommonLibActorBaseOffsets = Skyrim::RuntimeLayout::TESActorBaseCommonLibNgOffsets;
    using LocalActorBaseOffsets = Skyrim::RuntimeLayout::TESActorBaseLocalShimOffsets;

    [[nodiscard]] TESActorBaseData& GetActorBaseData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESActorBaseData>(this, CommonLibActorBaseOffsets::ActorData);
#else
        return actorData;
#endif
    }

    [[nodiscard]] const TESActorBaseData& GetActorBaseData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESActorBaseData>(this, CommonLibActorBaseOffsets::ActorData);
#else
        return actorData;
#endif
    }

    [[nodiscard]] TESFullName& GetFullNameData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibActorBaseOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] const TESFullName& GetFullNameData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESFullName>(this, CommonLibActorBaseOffsets::FullName);
#else
        return fullName;
#endif
    }

    [[nodiscard]] BGSKeywordForm& GetKeywordFormData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BGSKeywordForm>(this, CommonLibActorBaseOffsets::KeywordForm);
#else
        return keywordForm;
#endif
    }

    [[nodiscard]] const BGSKeywordForm& GetKeywordFormData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BGSKeywordForm>(this, CommonLibActorBaseOffsets::KeywordForm);
#else
        return keywordForm;
#endif
    }

    TESActorBaseData actorData;
    TESContainer container;
    TESSpellList spellList;
    TESAIForm aiForm;
    TESFullName fullName;
    ActorValueOwner actorValueOwner;
    BGSDestructibleObjectForm destructibleObjectForm;
    BGSSkinForm skinForm;
    BGSKeywordForm keywordForm;
    BGSAttackDataForm attackDataForm;
    BGSPerkRankArray perkRanks;
};

static_assert(TESActorBase::CommonLibActorBaseOffsets::ActorData == 0x30);
static_assert(TESActorBase::CommonLibActorBaseOffsets::FullName == 0xD8);
static_assert(TESActorBase::CommonLibActorBaseOffsets::KeywordForm == 0x110);
static_assert(TESActorBase::CommonLibActorBaseOffsets::Size == 0x150);
static_assert(offsetof(TESActorBase, actorData) == TESActorBase::LocalActorBaseOffsets::ActorData);
static_assert(offsetof(TESActorBase, fullName) == TESActorBase::LocalActorBaseOffsets::FullName);
static_assert(offsetof(TESActorBase, keywordForm) == TESActorBase::LocalActorBaseOffsets::KeywordForm);
static_assert(sizeof(TESActorBase) == TESActorBase::LocalActorBaseOffsets::Size);
