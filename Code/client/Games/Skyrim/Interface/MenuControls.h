#pragma once

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct MenuControls
{
    using CommonLibMenuControlsOffsets = Skyrim::RuntimeLayout::MenuControlsCommonLibNgOffsets;
    using LocalMenuControlsOffsets = Skyrim::RuntimeLayout::MenuControlsLocalShimOffsets;

    static MenuControls* GetInstance();

    void SetToggle(bool);

    void SetToggleData(bool aValue) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint8_t>(this, CommonLibMenuControlsOffsets::Toggle, aValue ? 1 : 0);
#else
        unk83 = aValue ? 1 : 0;
#endif
    }

    uint8_t pad_0000[0x80]; // 0x0000
    bool isProcessing;      // 0x0080
    bool beastForm;         // 0x0081
    bool remapMode;         // 0x0082
    uint8_t unk83;          // 0x0083
    uint32_t unk84;         // 0x0084
};

static_assert(offsetof(MenuControls, isProcessing) == MenuControls::LocalMenuControlsOffsets::IsProcessing);
static_assert(offsetof(MenuControls, beastForm) == MenuControls::LocalMenuControlsOffsets::BeastForm);
static_assert(offsetof(MenuControls, remapMode) == MenuControls::LocalMenuControlsOffsets::RemapMode);
static_assert(offsetof(MenuControls, unk83) == MenuControls::LocalMenuControlsOffsets::Toggle);
static_assert(offsetof(MenuControls, unk84) == MenuControls::LocalMenuControlsOffsets::Unk84);
static_assert(sizeof(MenuControls) == MenuControls::LocalMenuControlsOffsets::Size);
