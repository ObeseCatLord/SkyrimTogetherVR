#pragma once

#include <Components/BaseFormComponent.h>
#include <Misc/BSFixedString.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESFullName : BaseFormComponent
{
    using CommonLibFullNameOffsets = Skyrim::RuntimeLayout::TESFullNameCommonLibNgOffsets;
    using LocalFullNameOffsets = Skyrim::RuntimeLayout::TESFullNameLocalShimOffsets;

    virtual ~TESFullName();

    [[nodiscard]] BSFixedString& GetFullNameData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BSFixedString>(this, CommonLibFullNameOffsets::Value);
#else
        return value;
#endif
    }

    [[nodiscard]] const BSFixedString& GetFullNameData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BSFixedString>(this, CommonLibFullNameOffsets::Value);
#else
        return value;
#endif
    }

    [[nodiscard]] const char* GetFullNameStringData() const noexcept { return GetFullNameData().AsAscii(); }

    BSFixedString value;
};

static_assert(TESFullName::CommonLibFullNameOffsets::Value == 0x8);
static_assert(TESFullName::CommonLibFullNameOffsets::Size == 0x10);
static_assert(offsetof(TESFullName, value) == TESFullName::LocalFullNameOffsets::Value);
static_assert(sizeof(TESFullName) == TESFullName::LocalFullNameOffsets::Size);
