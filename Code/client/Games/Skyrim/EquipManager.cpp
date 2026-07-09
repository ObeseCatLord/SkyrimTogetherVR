#include <TiltedOnlinePCH.h>

#include <EquipManager.h>
#include <Games/References.h>
#include <Games/Overrides.h>

#include <Events/EquipmentChangeEvent.h>

#include <World.h>

#include <Games/Skyrim/Forms/TESAmmo.h>
#include <AI/AIProcess.h>
#include <Games/Skyrim/Misc/MiddleProcess.h>
#include <DefaultObjectManager.h>
#include <RuntimeLayout.h>

struct BGSEquipSlot : TESForm
{
};

struct EquipData
{
    using LocalEquipDataOffsets = Skyrim::RuntimeLayout::EquipDataLocalShimOffsets;

    [[nodiscard]] ExtraDataList* GetExtraDataListData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<ExtraDataList*>(this, LocalEquipDataOffsets::ExtraDataList);
#else
        return pExtraDataList;
#endif
    }

    [[nodiscard]] int32_t GetCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, LocalEquipDataOffsets::Count);
#else
        return count;
#endif
    }

    [[nodiscard]] BGSEquipSlot* GetSlotData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSEquipSlot*>(this, LocalEquipDataOffsets::Slot);
#else
        return pSlot;
#endif
    }

    [[nodiscard]] BGSEquipSlot* GetSlotToReplaceData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSEquipSlot*>(this, LocalEquipDataOffsets::SlotToReplace);
#else
        return pSlotToReplace;
#endif
    }

    [[nodiscard]] bool IsQueueEquipData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalEquipDataOffsets::QueueEquip);
#else
        return bQueueEquip;
#endif
    }

    [[nodiscard]] bool IsForceEquipData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalEquipDataOffsets::ForceEquip);
#else
        return bForceEquip;
#endif
    }

    [[nodiscard]] bool PlaysSoundData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalEquipDataOffsets::PlaySound);
#else
        return bPlaySound;
#endif
    }

    [[nodiscard]] bool AppliesNowData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalEquipDataOffsets::ApplyNow);
#else
        return bApplyNow;
#endif
    }

    ExtraDataList* pExtraDataList; // 0
    int32_t count;                 // 8
    BGSEquipSlot* pSlot;           // 10
    BGSEquipSlot* pSlotToReplace;  // 18
    bool bQueueEquip;
    bool bForceEquip;
    bool bPlaySound;
    bool bApplyNow;
    bool bUnk1;
};
static_assert(EquipData::LocalEquipDataOffsets::Count == 0x8);
static_assert(EquipData::LocalEquipDataOffsets::Slot == 0x10);
static_assert(EquipData::LocalEquipDataOffsets::QueueEquip == 0x20);
static_assert(EquipData::LocalEquipDataOffsets::Size == 0x28);
static_assert(offsetof(EquipData, pExtraDataList) == EquipData::LocalEquipDataOffsets::ExtraDataList);
static_assert(offsetof(EquipData, count) == EquipData::LocalEquipDataOffsets::Count);
static_assert(offsetof(EquipData, pSlot) == EquipData::LocalEquipDataOffsets::Slot);
static_assert(offsetof(EquipData, pSlotToReplace) == EquipData::LocalEquipDataOffsets::SlotToReplace);
static_assert(offsetof(EquipData, bQueueEquip) == EquipData::LocalEquipDataOffsets::QueueEquip);
static_assert(offsetof(EquipData, bForceEquip) == EquipData::LocalEquipDataOffsets::ForceEquip);
static_assert(offsetof(EquipData, bPlaySound) == EquipData::LocalEquipDataOffsets::PlaySound);
static_assert(offsetof(EquipData, bApplyNow) == EquipData::LocalEquipDataOffsets::ApplyNow);
static_assert(sizeof(EquipData) == EquipData::LocalEquipDataOffsets::Size);

struct MagicEquipData
{
    using LocalMagicEquipDataOffsets = Skyrim::RuntimeLayout::MagicEquipDataLocalShimOffsets;

    [[nodiscard]] BGSEquipSlot* GetEquipSlotData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSEquipSlot*>(this, LocalMagicEquipDataOffsets::EquipSlot);
#else
        return pEquipSlot;
#endif
    }

    [[nodiscard]] bool IsQueueEquipData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalMagicEquipDataOffsets::QueueEquip);
#else
        return bQueueEquip;
#endif
    }

    [[nodiscard]] bool IsForceEquipData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalMagicEquipDataOffsets::ForceEquip);
#else
        return bForceEquip;
#endif
    }

    BGSEquipSlot* pEquipSlot;
    bool bQueueEquip;
    bool bForceEquip;
};
static_assert(MagicEquipData::LocalMagicEquipDataOffsets::EquipSlot == 0x0);
static_assert(MagicEquipData::LocalMagicEquipDataOffsets::QueueEquip == 0x8);
static_assert(MagicEquipData::LocalMagicEquipDataOffsets::Size == 0x10);
static_assert(offsetof(MagicEquipData, pEquipSlot) == MagicEquipData::LocalMagicEquipDataOffsets::EquipSlot);
static_assert(offsetof(MagicEquipData, bQueueEquip) == MagicEquipData::LocalMagicEquipDataOffsets::QueueEquip);
static_assert(offsetof(MagicEquipData, bForceEquip) == MagicEquipData::LocalMagicEquipDataOffsets::ForceEquip);
static_assert(sizeof(MagicEquipData) == MagicEquipData::LocalMagicEquipDataOffsets::Size);

struct ShoutEquipData
{
    using LocalShoutEquipDataOffsets = Skyrim::RuntimeLayout::ShoutEquipDataLocalShimOffsets;

    [[nodiscard]] void* GetUnk1Data() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<void*>(this, LocalShoutEquipDataOffsets::Unk1);
#else
        return pUnk1;
#endif
    }

    [[nodiscard]] bool GetUnk2Data() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<bool>(this, LocalShoutEquipDataOffsets::Unk2);
#else
        return bUnk2;
#endif
    }

    void* pUnk1;
    bool bUnk2;
};
static_assert(ShoutEquipData::LocalShoutEquipDataOffsets::Unk1 == 0x0);
static_assert(ShoutEquipData::LocalShoutEquipDataOffsets::Unk2 == 0x8);
static_assert(ShoutEquipData::LocalShoutEquipDataOffsets::Size == 0x10);
static_assert(offsetof(ShoutEquipData, pUnk1) == ShoutEquipData::LocalShoutEquipDataOffsets::Unk1);
static_assert(offsetof(ShoutEquipData, bUnk2) == ShoutEquipData::LocalShoutEquipDataOffsets::Unk2);
static_assert(sizeof(ShoutEquipData) == ShoutEquipData::LocalShoutEquipDataOffsets::Size);

TP_THIS_FUNCTION(TEquip, void*, EquipManager, Actor* apActor, TESForm* apItem, EquipData* apData);
TP_THIS_FUNCTION(TUnEquip, void*, EquipManager, Actor* apActor, TESForm* apItem, EquipData* apData);
TP_THIS_FUNCTION(TEquipSpell, void*, EquipManager, Actor* apActor, TESForm* apSpell, MagicEquipData* apData);
TP_THIS_FUNCTION(TUnEquipSpell, void*, EquipManager, Actor* apActor, TESForm* apSpell, MagicEquipData* apData);
TP_THIS_FUNCTION(TEquipShout, void*, EquipManager, Actor* apActor, TESForm* apShout, ShoutEquipData* apData);
TP_THIS_FUNCTION(TUnEquipShout, void*, EquipManager, Actor* apActor, TESForm* apShout, ShoutEquipData* apData);

TUnEquip* RealUnEquip = nullptr;
TEquip* RealEquip = nullptr;
TEquipSpell* RealEquipSpell = nullptr;
TUnEquipSpell* RealUnEquipSpell = nullptr;
TEquipShout* RealEquipShout = nullptr;
TUnEquipShout* RealUnEquipShout = nullptr;

EquipManager* EquipManager::Get() noexcept
{
    POINTER_SKYRIMSE(EquipManager*, s_singleton, 400636);

    return *s_singleton.Get();
}

void* EquipManager::Equip(Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow)
{
    TP_THIS_FUNCTION(TEquipInternal, void*, EquipManager, Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow);
    POINTER_SKYRIMSE(TEquipInternal, s_equipFunc, 38894);

    ScopedEquipOverride equipOverride;

    spdlog::debug("Call Actor[{:X}]::Equip(), item id: {:X}, extra data? {}, count: {}", apActor->GetFormIdData(), apItem->GetFormIdData(), (bool)apExtraDataList, aCount);

    return TiltedPhoques::ThisCall(s_equipFunc, this, apActor, apItem, apExtraDataList, aCount, apSlot, abQueueEquip, abForceEquip, abPlaySound, abApplyNow);
}

void* EquipManager::UnEquip(Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow, TESForm* apSlotToReplace)
{
    TP_THIS_FUNCTION(TUnEquipInternal, void*, EquipManager, Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow, TESForm* apSlotToReplace);
    POINTER_SKYRIMSE(TUnEquipInternal, s_unequipFunc, 38901);

    ScopedEquipOverride equipOverride;

    spdlog::debug("Call Actor[{:X}]::UnEquip(), item id: {:X}, extra data? {}, count: {}", apActor->GetFormIdData(), apItem->GetFormIdData(), (bool)apExtraDataList, aCount);

    return TiltedPhoques::ThisCall(s_unequipFunc, this, apActor, apItem, apExtraDataList, aCount, apSlot, abQueueEquip, abForceEquip, abPlaySound, abApplyNow, apSlotToReplace);
}

void* EquipManager::EquipSpell(Actor* apActor, TESForm* apSpell, uint32_t aSlotId)
{
    TP_THIS_FUNCTION(TEquipSpellInternal, void*, EquipManager, Actor*, TESForm*, uint32_t);
    POINTER_SKYRIMSE(TEquipSpellInternal, s_equipFunc, 38896);

    ScopedEquipOverride equipOverride;

    return TiltedPhoques::ThisCall(s_equipFunc, this, apActor, apSpell, aSlotId);
}

void* EquipManager::UnEquipSpell(Actor* apActor, TESForm* apSpell, uint32_t aSlotId)
{
    TP_THIS_FUNCTION(TUnEquipSpellInternal, void*, EquipManager, Actor*, TESForm*, uint32_t);
    POINTER_SKYRIMSE(TUnEquipSpellInternal, s_unequipFunc, 38903);

    ScopedEquipOverride equipOverride;

    return TiltedPhoques::ThisCall(s_unequipFunc, this, apActor, apSpell, aSlotId);
}

void* EquipManager::EquipShout(Actor* apActor, TESForm* apShout)
{
    TP_THIS_FUNCTION(TEquipShoutInternal, void*, EquipManager, Actor*, TESForm*);
    POINTER_SKYRIMSE(TEquipShoutInternal, s_equipFunc, 38897);

    ScopedEquipOverride equipOverride;

    return TiltedPhoques::ThisCall(s_equipFunc, this, apActor, apShout);
}

void* EquipManager::UnEquipShout(Actor* apActor, TESForm* apShout)
{
    TP_THIS_FUNCTION(TUnEquipShoutInternal, void*, EquipManager, Actor*, TESForm*);
    POINTER_SKYRIMSE(TUnEquipShoutInternal, s_unequipFunc, 38903);

    ScopedEquipOverride equipOverride;

    return TiltedPhoques::ThisCall(s_unequipFunc, this, apActor, apShout);
}

void EquipManager::UnequipAll(Actor* apActor)
{
    TP_THIS_FUNCTION(TUnEquipAll, void, EquipManager, Actor*);
    POINTER_SKYRIMSE(TUnEquipAll, s_unequipAll, 38899);

    ScopedEquipOverride equipOverride;

    TiltedPhoques::ThisCall(s_unequipAll, this, apActor);
}

void* TP_MAKE_THISCALL(EquipHook, EquipManager, Actor* apActor, TESForm* apItem, EquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    const auto actorId = apActor->GetFormIdData();
    const auto itemId = apItem->GetFormIdData();
    if (pExtension->IsRemote())
    {
        spdlog::debug("Actor[{:X}]::Equip(), item form id: {:X}", actorId, itemId);
        if (!ScopedEquipOverride::IsOverriden())
            return nullptr;
    }

    // Consumables are "equipped" as well. We don't want this to sync, for several reasons.
    // The right hand item on the server would be overridden by the consumable.
    // Furthermore, the equip action on the other clients would doubly subtract the consumables.
    if (pExtension->IsLocal() && !apItem->IsConsumable() && !apData->IsQueueEquipData())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = actorId;
        evt.Count = apData->GetCountData();
        evt.ItemId = itemId;
        auto* pSlot = apData->GetSlotData();
        evt.EquipSlotId = pSlot ? pSlot->GetFormIdData() : 0;
        evt.IsAmmo = apItem->GetFormTypeData() == FormType::Ammo;

        World::Get().GetRunner().Trigger(evt);
    }

    ScopedUnequipOverride _;

    return TiltedPhoques::ThisCall(RealEquip, apThis, apActor, apItem, apData);
}

void* TP_MAKE_THISCALL(UnEquipHook, EquipManager, Actor* apActor, TESForm* apItem, EquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    const auto actorId = apActor->GetFormIdData();
    const auto itemId = apItem->GetFormIdData();
    if (pExtension->IsRemote())
    {
        spdlog::debug("Actor[{:X}]::Unequip(), item form id: {:X}, IsOverridden, equip: {}, inventory: {}", actorId, itemId, ScopedEquipOverride::IsOverriden(), ScopedInventoryOverride::IsOverriden());
        // The ScopedInventoryOverride check is here to allow the item to be unequipped if it is removed
        // Without this check, the game will not accept null as a return, and it'll keep trying to unequip infinitely
        if (!ScopedEquipOverride::IsOverriden() && !ScopedInventoryOverride::IsOverriden())
            return nullptr;
    }

    if (pExtension->IsLocal() && !ScopedUnequipOverride::IsOverriden() && !apData->IsQueueEquipData())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = actorId;
        evt.Count = apData->GetCountData();
        evt.ItemId = itemId;
        auto* pSlot = apData->GetSlotData();
        evt.EquipSlotId = pSlot ? pSlot->GetFormIdData() : 0;
        evt.Unequip = true;
        evt.IsAmmo = apItem->GetFormTypeData() == FormType::Ammo;

        World::Get().GetRunner().Trigger(evt);
    }

    spdlog::debug("UnEquipHook, actor: {:X}", actorId);

    return TiltedPhoques::ThisCall(RealUnEquip, apThis, apActor, apItem, apData);
}

void* TP_MAKE_THISCALL(EquipSpellHook, EquipManager, Actor* apActor, TESForm* apSpell, MagicEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    const auto actorId = apActor->GetFormIdData();
    const auto itemId = apSpell->GetFormIdData();
    if (pExtension->IsRemote() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    if (pExtension->IsLocal() && !apData->IsQueueEquipData())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = actorId;
        evt.ItemId = itemId;
        auto* pEquipSlot = apData->GetEquipSlotData();
        evt.EquipSlotId = pEquipSlot ? pEquipSlot->GetFormIdData() : 0;
        evt.IsSpell = true;

        World::Get().GetRunner().Trigger(evt);
    }

    ScopedUnequipOverride _;

    return TiltedPhoques::ThisCall(RealEquipSpell, apThis, apActor, apSpell, apData);
}

void* TP_MAKE_THISCALL(UnEquipSpellHook, EquipManager, Actor* apActor, TESForm* apSpell, MagicEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    const auto actorId = apActor->GetFormIdData();
    const auto itemId = apSpell->GetFormIdData();
    if (pExtension->IsRemote() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    if (pExtension->IsLocal() && !ScopedUnequipOverride::IsOverriden() && !apData->IsQueueEquipData())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = actorId;
        evt.ItemId = itemId;
        auto* pEquipSlot = apData->GetEquipSlotData();
        evt.EquipSlotId = pEquipSlot ? pEquipSlot->GetFormIdData() : 0;
        evt.Unequip = true;
        evt.IsSpell = true;

        World::Get().GetRunner().Trigger(evt);
    }

    return TiltedPhoques::ThisCall(RealUnEquipSpell, apThis, apActor, apSpell, apData);
}

void* TP_MAKE_THISCALL(EquipShoutHook, EquipManager, Actor* apActor, TESForm* apShout, ShoutEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    const auto actorId = apActor->GetFormIdData();
    const auto itemId = apShout->GetFormIdData();
    if (pExtension->IsRemote() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    // TODO: queue check?
    if (pExtension->IsLocal())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = actorId;
        evt.ItemId = itemId;
        evt.IsShout = true;

        World::Get().GetRunner().Trigger(evt);
    }

    ScopedUnequipOverride _;

    return TiltedPhoques::ThisCall(RealEquipShout, apThis, apActor, apShout, apData);
}

void* TP_MAKE_THISCALL(UnEquipShoutHook, EquipManager, Actor* apActor, TESForm* apShout, ShoutEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    const auto actorId = apActor->GetFormIdData();
    const auto itemId = apShout->GetFormIdData();
    if (pExtension->IsRemote() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    // TODO: queue check?
    if (pExtension->IsLocal() && !ScopedUnequipOverride::IsOverriden())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = actorId;
        evt.ItemId = itemId;
        evt.Unequip = true;
        evt.IsShout = true;

        World::Get().GetRunner().Trigger(evt);
    }

    return TiltedPhoques::ThisCall(RealUnEquipShout, apThis, apActor, apShout, apData);
}

static TiltedPhoques::Initializer s_equipmentHooks(
    []()
    {
        POINTER_SKYRIMSE(TEquip, s_equipFunc, 38929);
        POINTER_SKYRIMSE(TUnEquip, s_unequipFunc, 38934);
        POINTER_SKYRIMSE(TEquipSpell, s_equipSpellFunc, 38928);
        POINTER_SKYRIMSE(TUnEquipSpell, s_unequipSpellFunc, 38933);
        POINTER_SKYRIMSE(TEquipShout, s_equipShoutFunc, 38930);
        POINTER_SKYRIMSE(TUnEquipShout, s_unequipShoutFunc, 38935);

        RealEquip = s_equipFunc.Get();
        RealUnEquip = s_unequipFunc.Get();
        RealEquipSpell = s_equipSpellFunc.Get();
        RealUnEquipSpell = s_unequipSpellFunc.Get();
        RealEquipShout = s_equipShoutFunc.Get();
        RealUnEquipShout = s_unequipShoutFunc.Get();

        TP_HOOK(&RealEquip, EquipHook);
        TP_HOOK(&RealUnEquip, UnEquipHook);
        TP_HOOK(&RealEquipSpell, EquipSpellHook);
        TP_HOOK(&RealUnEquipSpell, UnEquipSpellHook);
        TP_HOOK(&RealEquipShout, EquipShoutHook);
        TP_HOOK(&RealUnEquipShout, UnEquipShoutHook);
    });
