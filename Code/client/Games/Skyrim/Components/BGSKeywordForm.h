#pragma once

#include <Components/BaseFormComponent.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BGSKeyword;

struct BGSKeywordForm : BaseFormComponent
{
    using CommonLibKeywordFormOffsets = Skyrim::RuntimeLayout::BGSKeywordFormCommonLibNgOffsets;
    using LocalKeywordFormOffsets = Skyrim::RuntimeLayout::BGSKeywordFormLocalShimOffsets;

    virtual bool Contains(BGSKeyword* apKeyword) const;
    virtual void sub_5();

    [[nodiscard]] BGSKeyword** GetKeywordsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSKeyword**>(this, CommonLibKeywordFormOffsets::Keywords);
#else
        return keywords;
#endif
    }

    [[nodiscard]] BGSKeyword* const* GetKeywordsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSKeyword**>(this, CommonLibKeywordFormOffsets::Keywords);
#else
        return keywords;
#endif
    }

    [[nodiscard]] uint32_t GetKeywordCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibKeywordFormOffsets::Count);
#else
        return count;
#endif
    }

    [[nodiscard]] bool ContainsKeywordData(BGSKeyword* apKeyword) const
    {
        return GetKeywordCountData() > 0 && Contains(apKeyword);
    }

    BGSKeyword** keywords;
    uint32_t count;
};

static_assert(BGSKeywordForm::CommonLibKeywordFormOffsets::Keywords == 0x8);
static_assert(BGSKeywordForm::CommonLibKeywordFormOffsets::Count == 0x10);
static_assert(BGSKeywordForm::CommonLibKeywordFormOffsets::Size == 0x18);
static_assert(offsetof(BGSKeywordForm, keywords) == BGSKeywordForm::LocalKeywordFormOffsets::Keywords);
static_assert(offsetof(BGSKeywordForm, count) == BGSKeywordForm::LocalKeywordFormOffsets::Count);
static_assert(sizeof(BGSKeywordForm) == BGSKeywordForm::LocalKeywordFormOffsets::Size);
