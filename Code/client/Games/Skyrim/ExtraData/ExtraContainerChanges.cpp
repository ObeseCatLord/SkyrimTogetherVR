#include <TiltedOnlinePCH.h>

#include <ExtraData/ExtraContainerChanges.h>
#include <Forms/TESForm.h>

void ExtraContainerChanges::Data::Save(BGSSaveFormBuffer* apBuffer)
{
    TP_THIS_FUNCTION(TSaveFunc, void*, ExtraContainerChanges::Data, BGSSaveFormBuffer*);

    POINTER_SKYRIMSE(TSaveFunc, s_save, 16142);

    TiltedPhoques::ThisCall(s_save, this, apBuffer);
}

void ExtraContainerChanges::Data::Load(BGSLoadFormBuffer* apBuffer)
{
    TP_THIS_FUNCTION(TLoadFunc, void*, ExtraContainerChanges::Data, BGSLoadFormBuffer*);

    POINTER_SKYRIMSE(TLoadFunc, s_load, 16143);

    TiltedPhoques::ThisCall(s_load, this, apBuffer);
}

bool ExtraContainerChanges::Entry::IsQuestObject() noexcept
{
    TP_THIS_FUNCTION(TIsQuestObject, bool, ExtraContainerChanges::Entry);

    POINTER_SKYRIMSE(TIsQuestObject, s_isQuestObject, 16005);

    return TiltedPhoques::ThisCall(s_isQuestObject, this);
}

TESForm* ExtraContainerChanges::Entry::GetObject() noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<TESForm*>(this, ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryObject);
#else
    return form;
#endif
}

const TESForm* ExtraContainerChanges::Entry::GetObject() const noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<TESForm*>(this, ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryObject);
#else
    return form;
#endif
}

GameList<ExtraDataList>* ExtraContainerChanges::Entry::GetExtraLists() noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<GameList<ExtraDataList>*>(
        this, ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryExtraLists);
#else
    return dataList;
#endif
}

const GameList<ExtraDataList>* ExtraContainerChanges::Entry::GetExtraLists() const noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<GameList<ExtraDataList>*>(
        this, ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryExtraLists);
#else
    return dataList;
#endif
}

int32_t ExtraContainerChanges::Entry::GetCountDelta() const noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<int32_t>(
        this, ExtraContainerChanges::CommonLibContainerChangesOffsets::EntryCountDelta);
#else
    return count;
#endif
}

TESObjectARMO* ExtraContainerChanges::Data::GetArmor(uint32_t aSlotId) noexcept
{
    TP_THIS_FUNCTION(TGetArmor, TESObjectARMO*, ExtraContainerChanges::Data, uint32_t);

    POINTER_SKYRIMSE(TGetArmor, s_getArmor, 16113);

    return TiltedPhoques::ThisCall(s_getArmor, this, aSlotId);
}

GameList<ExtraContainerChanges::Entry>* ExtraContainerChanges::Data::GetEntryList() noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<GameList<ExtraContainerChanges::Entry>*>(
        this, ExtraContainerChanges::CommonLibContainerChangesOffsets::DataEntries);
#else
    return entries;
#endif
}

const GameList<ExtraContainerChanges::Entry>* ExtraContainerChanges::Data::GetEntryList() const noexcept
{
#if TP_SKYRIM_VR
    return Skyrim::RuntimeLayout::Value<GameList<ExtraContainerChanges::Entry>*>(
        this, ExtraContainerChanges::CommonLibContainerChangesOffsets::DataEntries);
#else
    return entries;
#endif
}

ExtraContainerChanges::Entry* ExtraContainerChanges::Data::FindEntryByFormId(uint32_t aFormId) noexcept
{
    Entry* pResult = nullptr;
    VisitInventory([&](Entry& arEntry) {
        if (!pResult && arEntry.GetObject() && arEntry.GetObject()->GetFormIdData() == aFormId)
            pResult = &arEntry;
    });

    return pResult;
}

const ExtraContainerChanges::Entry* ExtraContainerChanges::Data::FindEntryByFormId(uint32_t aFormId) const noexcept
{
    const Entry* pResult = nullptr;
    VisitInventory([&](const Entry& arEntry) {
        if (!pResult && arEntry.GetObject() && arEntry.GetObject()->GetFormIdData() == aFormId)
            pResult = &arEntry;
    });

    return pResult;
}
