#pragma once

#include <Forms/TESForm.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESRace;

struct TESRaceForm : BaseFormComponent
{
    using CommonLibRaceFormOffsets = Skyrim::RuntimeLayout::TESRaceFormCommonLibNgOffsets;
    using LocalRaceFormOffsets = Skyrim::RuntimeLayout::TESRaceFormLocalShimOffsets;

    [[nodiscard]] TESRace* GetRaceData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESRace*>(this, CommonLibRaceFormOffsets::Race);
#else
        return race;
#endif
    }

    void SetRaceData(TESRace* apRace) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<TESRace*>(this, CommonLibRaceFormOffsets::Race, apRace);
#else
        race = apRace;
#endif
    }

    TESRace* race;
};

static_assert(TESRaceForm::CommonLibRaceFormOffsets::Race == 0x8);
static_assert(TESRaceForm::CommonLibRaceFormOffsets::Size == 0x10);
static_assert(offsetof(TESRaceForm, race) == TESRaceForm::LocalRaceFormOffsets::Race);
static_assert(sizeof(TESRaceForm) == TESRaceForm::LocalRaceFormOffsets::Size);
