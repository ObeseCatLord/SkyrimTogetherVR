#pragma once

#include <Components/BaseFormComponent.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BGSBipedObjectForm : BaseFormComponent
{
    using CommonLibBipedObjectOffsets = Skyrim::RuntimeLayout::BGSBipedObjectFormCommonLibNgOffsets;
    using LocalBipedObjectOffsets = Skyrim::RuntimeLayout::BGSBipedObjectFormLocalShimOffsets;

    enum class Part
    {
        Head = 1 << 0,
        Hair = 1 << 1,
        Body = 1 << 2,
        Hands = 1 << 3,
        Forearms = 1 << 4,
        Amulet = 1 << 5,
        Ring = 1 << 6,
        Feet = 1 << 7,
        Calves = 1 << 8,
        Shield = 1 << 9,
        LongHair = 1 << 11,
        Circlet = 1 << 12,
        Ears = 1 << 13
    };

    [[nodiscard]] uint32_t GetSlotMaskData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibBipedObjectOffsets::SlotMask);
#else
        return bitfield;
#endif
    }

    [[nodiscard]] bool HasPartData(Part aPart) const noexcept
    {
        return (GetSlotMaskData() & static_cast<uint32_t>(aPart)) != 0;
    }

    uint32_t bitfield;
    uint32_t unk8;
};

static_assert(BGSBipedObjectForm::CommonLibBipedObjectOffsets::SlotMask == 0x8);
static_assert(BGSBipedObjectForm::CommonLibBipedObjectOffsets::ArmorType == 0xC);
static_assert(BGSBipedObjectForm::CommonLibBipedObjectOffsets::Size == 0x10);
static_assert(offsetof(BGSBipedObjectForm, bitfield) == BGSBipedObjectForm::LocalBipedObjectOffsets::SlotMask);
static_assert(offsetof(BGSBipedObjectForm, unk8) == BGSBipedObjectForm::LocalBipedObjectOffsets::ArmorType);
static_assert(sizeof(BGSBipedObjectForm) == BGSBipedObjectForm::LocalBipedObjectOffsets::Size);
