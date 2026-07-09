#include "ExtraDataList.h"

#include <Games/Overrides.h>

ExtraDataList* ExtraDataList::New() noexcept
{
    ExtraDataList* pExtraDataList = Memory::Allocate<ExtraDataList>();
    pExtraDataList->GetDataData() = nullptr;
    pExtraDataList->GetLockData().m_counter = pExtraDataList->GetLockData().m_tid = 0;
    pExtraDataList->GetBitfieldData() = Memory::Allocate<ExtraDataList::Bitfield>();
    memset(pExtraDataList->GetBitfieldData(), 0, ExtraDataList::LocalExtraDataListOffsets::BitfieldSize);
    return pExtraDataList;
}

bool ExtraDataList::Contains(ExtraDataType aType) const
{
    if (const auto* pBitfield = GetBitfieldData())
    {
        const auto value = static_cast<uint32_t>(aType);
        const auto index = value >> 3;

        const auto element = pBitfield->data[index];

        return (element >> (value % 8)) & 1;
    }

    return false;
}

BSExtraData* ExtraDataList::GetByType(ExtraDataType aType) const
{
    auto& extraLock = GetLockData();

    if (!ScopedExtraDataOverride::IsOverriden())
        extraLock.Lock();

    if (!Contains(aType))
    {
        if (!ScopedExtraDataOverride::IsOverriden())
            extraLock.Unlock();
        return nullptr;
    }

    auto pEntry = GetDataData();
    while (pEntry != nullptr && pEntry->GetType() != aType)
    {
        pEntry = pEntry->next;
    }

    if (!ScopedExtraDataOverride::IsOverriden())
        extraLock.Unlock();

    return pEntry;
}

bool ExtraDataList::Add(ExtraDataType aType, BSExtraData* apNewData)
{
    if (Contains(aType))
        return false;

    BSScopedLock<BSRecursiveLock> _(GetLockData());

    BSExtraData*& pData = GetDataData();
    BSExtraData* pNext = pData;
    pData = apNewData;
    apNewData->next = pNext;
    SetType(aType, false);

    return true;
}

uint32_t ExtraDataList::GetCount() const
{
    uint32_t count = 0;

    BSExtraData* pNext = GetDataData();
    while (pNext)
    {
        count++;
        pNext = pNext->next;
    }

    return count;
}

void ExtraDataList::SetType(ExtraDataType aType, bool aClear)
{
    uint32_t index = static_cast<uint8_t>(aType) >> 3;
    uint8_t bitmask = 1 << (static_cast<uint8_t>(aType) % 8);
    uint8_t& flag = GetBitfieldData()->data[index];
    if (aClear)
        flag &= ~bitmask;
    else
        flag |= bitmask;
}

void ExtraDataList::SetSoulData(SOUL_LEVEL aSoulLevel) noexcept
{
    TP_THIS_FUNCTION(TSetSoulData, void, ExtraDataList, SOUL_LEVEL aSoulLevel);
    POINTER_SKYRIMSE(TSetSoulData, setSoulData, 11620);
    TiltedPhoques::ThisCall(setSoulData, this, aSoulLevel);
}

void ExtraDataList::SetChargeData(float aCharge) noexcept
{
    TP_THIS_FUNCTION(TSetChargeData, void, ExtraDataList, float aCharge);
    POINTER_SKYRIMSE(TSetChargeData, setChargeData, 11619);
    TiltedPhoques::ThisCall(setChargeData, this, aCharge);
}

void ExtraDataList::SetWorn(bool aWornLeft) noexcept
{
    // TODO: what's this bool? seems to be true always except for one instance
    TP_THIS_FUNCTION(TSetWornData, void, ExtraDataList, bool aUnk1, bool aWornLeft);
    POINTER_SKYRIMSE(TSetWornData, setWornData, 11612);
    TiltedPhoques::ThisCall(setWornData, this, true, aWornLeft);
}

void ExtraDataList::SetPoison(AlchemyItem* apItem, uint32_t aCount) noexcept
{
    TP_THIS_FUNCTION(TSetPoison, void, ExtraDataList, AlchemyItem* apItem, uint32_t aCount);
    POINTER_SKYRIMSE(TSetPoison, setPoison, 11822);
    TiltedPhoques::ThisCall(setPoison, this, apItem, aCount);
}

void ExtraDataList::SetHealth(float aHealth) noexcept
{
    TP_THIS_FUNCTION(TSetHealth, void, ExtraDataList, float aHealth);
    POINTER_SKYRIMSE(TSetHealth, setHealth, 11616);
    TiltedPhoques::ThisCall(setHealth, this, aHealth);
}

void ExtraDataList::SetEnchantmentData(EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip) noexcept
{
    TP_THIS_FUNCTION(TSetEnchantmentData, void, ExtraDataList, EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip);
    POINTER_SKYRIMSE(TSetEnchantmentData, setEnchantmentData, 12060);
    TiltedPhoques::ThisCall(setEnchantmentData, this, apItem, aCharge, aRemoveOnUnequip);
}

bool ExtraDataList::HasQuestObjectAlias() noexcept
{
    TP_THIS_FUNCTION(THasQuestObjectAlias, bool, ExtraDataList);
    POINTER_SKYRIMSE(THasQuestObjectAlias, s_hasQuestObjectAlias, 12052);
    return TiltedPhoques::ThisCall(s_hasQuestObjectAlias, this);
}
