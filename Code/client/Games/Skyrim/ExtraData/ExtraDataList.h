#pragma once

#include "ExtraData.h"

#include <ExtraData/ExtraSoul.h>
#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct AlchemyItem;
struct EnchantmentItem;

struct ExtraDataList
{
    using CommonLibExtraDataListOffsets = Skyrim::RuntimeLayout::ExtraDataListCommonLibNgOffsets;
    using LocalExtraDataListOffsets = Skyrim::RuntimeLayout::ExtraDataListLocalShimOffsets;

    static ExtraDataList* New() noexcept;

    bool Contains(ExtraDataType aType) const;
    void Set(ExtraDataType aType, bool aSet);

    bool Add(ExtraDataType aType, BSExtraData* apNewData);
    bool Remove(ExtraDataType aType, BSExtraData* apNewData);

    uint32_t GetCount() const;

    void SetType(ExtraDataType aType, bool aClear);
    BSExtraData* GetByType(ExtraDataType type) const;

    void SetSoulData(SOUL_LEVEL aSoulLevel) noexcept;
    void SetChargeData(float aCharge) noexcept;
    void SetWorn(bool aWornLeft) noexcept;
    void SetPoison(AlchemyItem* apItem, uint32_t aCount) noexcept;
    void SetHealth(float aHealth) noexcept;
    void SetEnchantmentData(EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip) noexcept;

    [[nodiscard]] bool HasQuestObjectAlias() noexcept;

    virtual ~ExtraDataList();

    [[nodiscard]] BSExtraData*& GetDataData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BSExtraData*>(this, CommonLibExtraDataListOffsets::Data);
#else
        return data;
#endif
    }

    [[nodiscard]] BSExtraData* GetDataData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BSExtraData*>(this, CommonLibExtraDataListOffsets::Data);
#else
        return data;
#endif
    }

    struct Bitfield
    {
        uint8_t data[0x18];
    };

    [[nodiscard]] Bitfield*& GetBitfieldData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<Bitfield*>(this, CommonLibExtraDataListOffsets::Bitfield);
#else
        return bitfield;
#endif
    }

    [[nodiscard]] const Bitfield* GetBitfieldData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<Bitfield*>(this, CommonLibExtraDataListOffsets::Bitfield);
#else
        return bitfield;
#endif
    }

    [[nodiscard]] BSRecursiveLock& GetLockData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<BSRecursiveLock>(
            const_cast<ExtraDataList*>(this), CommonLibExtraDataListOffsets::Lock);
#else
        return lock;
#endif
    }

    BSExtraData* data = nullptr;

    Bitfield* bitfield{};
    mutable BSRecursiveLock lock{};
};

static_assert(sizeof(ExtraDataList::Bitfield) == ExtraDataList::LocalExtraDataListOffsets::BitfieldSize);
static_assert(offsetof(ExtraDataList, data) == ExtraDataList::LocalExtraDataListOffsets::Data);
static_assert(offsetof(ExtraDataList, bitfield) == ExtraDataList::LocalExtraDataListOffsets::Bitfield);
static_assert(offsetof(ExtraDataList, lock) == ExtraDataList::LocalExtraDataListOffsets::Lock);
