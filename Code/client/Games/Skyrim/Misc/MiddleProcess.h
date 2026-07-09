#pragma once

#include <RuntimeLayout.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct ActiveEffect;
struct BGSLoadFormBuffer;

struct InventoryEntry;

struct MiddleProcess
{
    using CommonLibMiddleProcessOffsets = Skyrim::RuntimeLayout::MiddleProcessCommonLibNgOffsets;
    using LocalMiddleProcessOffsets = Skyrim::RuntimeLayout::MiddleProcessLocalShimOffsets;

    // void SaveActiveEffects()
    void LoadActiveEffects(BGSLoadFormBuffer* apLoadGameBuffer);

    [[nodiscard]] float GetDirectionData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibMiddleProcessOffsets::Direction);
#else
        return direction;
#endif
    }

    void SetDirectionData(float aDirection) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibMiddleProcessOffsets::Direction, aDirection);
#else
        direction = aDirection;
#endif
    }

    [[nodiscard]] GameList<ActiveEffect>* GetActiveEffectsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<GameList<ActiveEffect>*>(this, CommonLibMiddleProcessOffsets::ActiveEffects);
#else
        return ActiveEffects;
#endif
    }

    [[nodiscard]] BSPointerHandle<TESObjectREFR> GetCommandingActorData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BSPointerHandle<TESObjectREFR>>(this, CommonLibMiddleProcessOffsets::CommandingActor);
#else
        return commandingActor;
#endif
    }

    void SetCommandingActorData(BSPointerHandle<TESObjectREFR> aCommandingActor) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store(this, CommonLibMiddleProcessOffsets::CommandingActor, aCommandingActor);
#else
        commandingActor = aCommandingActor;
#endif
    }

    [[nodiscard]] InventoryEntry* GetLeftEquippedObjectData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<InventoryEntry*>(this, CommonLibMiddleProcessOffsets::LeftEquippedObject);
#else
        return leftEquippedObject;
#endif
    }

    [[nodiscard]] InventoryEntry* GetRightEquippedObjectData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<InventoryEntry*>(this, CommonLibMiddleProcessOffsets::RightEquippedObject);
#else
        return rightEquippedObject;
#endif
    }

    [[nodiscard]] InventoryEntry* GetBothHandsEquippedObjectData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<InventoryEntry*>(this, CommonLibMiddleProcessOffsets::BothHandsEquippedObject);
#else
        return bothHandsEquippedObject;
#endif
    }

    // 0xB0 - pitch
    uint8_t pad0[0xB8];
    float direction; // B8
    uint8_t padBC[0x1A0 - 0xBC];
    GameList<ActiveEffect>* ActiveEffects;
    uint8_t pad1A8[0x218 - 0x1A8];
    BSPointerHandle<TESObjectREFR> commandingActor;
    uint8_t pad21C[0x220 - 0x21C];
    InventoryEntry* leftEquippedObject;
    uint8_t pad228[0x260 - 0x228];
    InventoryEntry* rightEquippedObject;
    InventoryEntry* bothHandsEquippedObject;
    // 0xB8 - direction
    //
    // 0x326 - bool lookat
};

static_assert(offsetof(MiddleProcess, direction) == MiddleProcess::LocalMiddleProcessOffsets::Direction);
static_assert(offsetof(MiddleProcess, ActiveEffects) == MiddleProcess::LocalMiddleProcessOffsets::ActiveEffects);
static_assert(offsetof(MiddleProcess, commandingActor) == MiddleProcess::LocalMiddleProcessOffsets::CommandingActor);
static_assert(offsetof(MiddleProcess, leftEquippedObject) == MiddleProcess::LocalMiddleProcessOffsets::LeftEquippedObject);
static_assert(offsetof(MiddleProcess, rightEquippedObject) == MiddleProcess::LocalMiddleProcessOffsets::RightEquippedObject);
static_assert(offsetof(MiddleProcess, bothHandsEquippedObject) == MiddleProcess::LocalMiddleProcessOffsets::BothHandsEquippedObject);
