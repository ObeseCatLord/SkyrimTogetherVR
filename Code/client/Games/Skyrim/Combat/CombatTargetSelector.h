#pragma once

#include <AI/AITimer.h>
#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

struct CombatController;

struct CombatTargetSelector
{
    using LocalCombatTargetSelectorOffsets = Skyrim::RuntimeLayout::CombatTargetSelectorLocalShimOffsets;

    virtual ~CombatTargetSelector();
    virtual void Unk1();
    virtual void Unk2();
    virtual void Unk3();
    virtual void Unk4();
    virtual void Update();
    virtual BSPointerHandle<Actor> SelectTarget();

    [[nodiscard]] CombatController* GetCombatControllerData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<CombatController*>(this, LocalCombatTargetSelectorOffsets::Controller);
#else
        return pCombatController;
#endif
    }

    [[nodiscard]] BSPointerHandle<Actor> GetTargetHandleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BSPointerHandle<Actor>>(this, LocalCombatTargetSelectorOffsets::TargetHandle);
#else
        return hTarget;
#endif
    }

    [[nodiscard]] uint32_t GetPriorityData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, LocalCombatTargetSelectorOffsets::Priority);
#else
        return ePriority;
#endif
    }

    [[nodiscard]] uint32_t GetFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, LocalCombatTargetSelectorOffsets::Flags);
#else
        return flags;
#endif
    }

    [[nodiscard]] bool HasFlagsData(uint32_t aMask) const noexcept { return (GetFlagsData() & aMask) == aMask; }

    void *vftable_NiRefObject_8;
    CombatController* pCombatController;
    BSPointerHandle<Actor> hTarget;
    uint32_t ePriority;
    uint32_t flags;
    uint8_t pad24[4];
};
static_assert(offsetof(CombatTargetSelector, pCombatController) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Controller);
static_assert(offsetof(CombatTargetSelector, hTarget) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::TargetHandle);
static_assert(offsetof(CombatTargetSelector, ePriority) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Priority);
static_assert(offsetof(CombatTargetSelector, flags) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Flags);
static_assert(sizeof(CombatTargetSelector) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::Size);

struct CombatTargetSelectorStandard : public CombatTargetSelector
{
    AITimer updateTimer;
};
static_assert(sizeof(CombatTargetSelectorStandard) == CombatTargetSelector::LocalCombatTargetSelectorOffsets::StandardSize);
