#pragma once

#include <Components/BGSBipedObjectForm.h>
#include <Forms/TESForm.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESObjectARMO : TESForm
{
    using CommonLibArmorOffsets = Skyrim::RuntimeLayout::TESObjectARMOCommonLibNgOffsets;
    using LocalArmorOffsets = Skyrim::RuntimeLayout::TESObjectARMOLocalShimOffsets;

    [[nodiscard]] uint32_t GetSlotMaskData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibArmorOffsets::SlotMask);
#else
        return slotType;
#endif
    }

    [[nodiscard]] bool IsBodyPiece() const noexcept
    {
        return (GetSlotMaskData() & static_cast<uint32_t>(BGSBipedObjectForm::Part::Body)) != 0;
    }

    uint8_t unk20[0x1B8 - sizeof(TESForm)];
    uint32_t slotType;
};

static_assert(TESObjectARMO::CommonLibArmorOffsets::BipedObjectForm == 0x1B0);
static_assert(TESObjectARMO::CommonLibArmorOffsets::SlotMask == 0x1B8);
static_assert(TESObjectARMO::CommonLibArmorOffsets::Size == 0x228);
static_assert(offsetof(TESObjectARMO, slotType) == TESObjectARMO::LocalArmorOffsets::SlotMask);
