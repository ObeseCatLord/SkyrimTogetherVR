#pragma once

#include "ExtraData.h"
#include <Components/TESFullName.h>
#include <Components/TESDescription.h>
#include <Forms/TESForm.h>
#include <Misc/BSFixedString.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESQuest;

struct BGSMessage : TESForm, TESFullName, TESDescription
{
    void* pIcon;
    void* pOwnerQuest;
    uint8_t menuButtons[0x10];
    uint32_t uiFlags;
    uint32_t uiDisplayTime;
};

static_assert(sizeof(BGSMessage) == 0x68);

struct ExtraTextDisplayData : BSExtraData
{
    using CommonLibTextDisplayOffsets = Skyrim::RuntimeLayout::ExtraTextDisplayDataCommonLibNgOffsets;
    using LocalTextDisplayOffsets = Skyrim::RuntimeLayout::ExtraTextDisplayDataLocalShimOffsets;

    inline static constexpr auto eExtraData = ExtraDataType::TextDisplayData;

    enum class DisplayDataType : int32_t
    {
        Uninitialized = -1,
        CustomName = -2,
    };

    [[nodiscard]] BSFixedString& GetDisplayNameData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BSFixedString>(this, CommonLibTextDisplayOffsets::DisplayName);
#else
        return DisplayName;
#endif
    }

    [[nodiscard]] const BSFixedString& GetDisplayNameData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BSFixedString>(this, CommonLibTextDisplayOffsets::DisplayName);
#else
        return DisplayName;
#endif
    }

    [[nodiscard]] const char* GetDisplayNameStringData() const noexcept { return GetDisplayNameData().AsAscii(); }

    void SetDisplayNameData(const char* apDisplayName) noexcept { GetDisplayNameData().Set(apDisplayName); }

    [[nodiscard]] BGSMessage* GetDisplayNameTextData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSMessage*>(this, CommonLibTextDisplayOffsets::DisplayNameText);
#else
        return pDisplayNameText;
#endif
    }

    [[nodiscard]] TESQuest* GetOwnerQuestData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESQuest*>(this, CommonLibTextDisplayOffsets::OwnerQuest);
#else
        return pOwnerQuest;
#endif
    }

    [[nodiscard]] DisplayDataType GetOwnerInstanceTypeData() const noexcept
    {
        return static_cast<DisplayDataType>(GetOwnerInstanceData());
    }

    [[nodiscard]] int32_t GetOwnerInstanceData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibTextDisplayOffsets::OwnerInstance);
#else
        return iOwnerInstance;
#endif
    }

    void SetOwnerInstanceData(int32_t aOwnerInstance) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<int32_t>(this, CommonLibTextDisplayOffsets::OwnerInstance, aOwnerInstance);
#else
        iOwnerInstance = aOwnerInstance;
#endif
    }

    void SetOwnerInstanceData(DisplayDataType aOwnerInstance) noexcept
    {
        SetOwnerInstanceData(static_cast<int32_t>(aOwnerInstance));
    }

    [[nodiscard]] float GetTemperFactorData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibTextDisplayOffsets::TemperFactor);
#else
        return fTemperFactor;
#endif
    }

    void SetTemperFactorData(float aTemperFactor) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibTextDisplayOffsets::TemperFactor, aTemperFactor);
#else
        fTemperFactor = aTemperFactor;
#endif
    }

    [[nodiscard]] uint16_t GetCustomNameLengthData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint16_t>(this, CommonLibTextDisplayOffsets::CustomNameLength);
#else
        return usCustomNameLength;
#endif
    }

    void SetCustomNameLengthData(uint16_t aCustomNameLength) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint16_t>(this, CommonLibTextDisplayOffsets::CustomNameLength, aCustomNameLength);
#else
        usCustomNameLength = aCustomNameLength;
#endif
    }

    BSFixedString DisplayName{};
    BGSMessage* pDisplayNameText{};
    TESQuest* pOwnerQuest{};
    int32_t iOwnerInstance{};
    float fTemperFactor{};
    uint16_t usCustomNameLength{};

    uint16_t pad32{};
    uint32_t pad34{};
};

static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::DisplayName == 0x10);
static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::DisplayNameText == 0x18);
static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::OwnerQuest == 0x20);
static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::OwnerInstance == 0x28);
static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::TemperFactor == 0x2C);
static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::CustomNameLength == 0x30);
static_assert(ExtraTextDisplayData::CommonLibTextDisplayOffsets::Size == 0x38);
static_assert(offsetof(ExtraTextDisplayData, DisplayName) == ExtraTextDisplayData::LocalTextDisplayOffsets::DisplayName);
static_assert(offsetof(ExtraTextDisplayData, pDisplayNameText) == ExtraTextDisplayData::LocalTextDisplayOffsets::DisplayNameText);
static_assert(offsetof(ExtraTextDisplayData, pOwnerQuest) == ExtraTextDisplayData::LocalTextDisplayOffsets::OwnerQuest);
static_assert(offsetof(ExtraTextDisplayData, iOwnerInstance) == ExtraTextDisplayData::LocalTextDisplayOffsets::OwnerInstance);
static_assert(offsetof(ExtraTextDisplayData, fTemperFactor) == ExtraTextDisplayData::LocalTextDisplayOffsets::TemperFactor);
static_assert(offsetof(ExtraTextDisplayData, usCustomNameLength) == ExtraTextDisplayData::LocalTextDisplayOffsets::CustomNameLength);
static_assert(sizeof(ExtraTextDisplayData) == 0x38);
static_assert(sizeof(ExtraTextDisplayData) == ExtraTextDisplayData::LocalTextDisplayOffsets::Size);
