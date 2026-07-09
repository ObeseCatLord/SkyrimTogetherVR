#pragma once

#include "MagicCaster.h"

#include <Games/Skyrim/NetImmerse/NiPointer.h>
#include <cstddef>

struct Actor;
struct NiNode;
struct BGSArtObject;
struct WeaponEnchantmentController;

struct ActorMagicCaster : MagicCaster
{
    using CommonLibActorMagicCasterOffsets = Skyrim::RuntimeLayout::ActorMagicCasterCommonLibNgOffsets;
    using LocalActorMagicCasterOffsets = Skyrim::RuntimeLayout::ActorMagicCasterLocalShimOffsets;

    virtual uint64_t SpellCast(bool abSuccess, uint32_t auiTargetCount, MagicItem* apSpell) override;

    virtual void sub_1D(float aUnk1);

    [[nodiscard]] Actor* GetCasterActorData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<Actor*>(this, CommonLibActorMagicCasterOffsets::CasterActor);
#else
        return pCasterActor;
#endif
    }

    [[nodiscard]] NiNode* GetMagicNodeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<NiNode*>(this, CommonLibActorMagicCasterOffsets::MagicNode);
#else
        return pMagicNode;
#endif
    }

    [[nodiscard]] MagicSystem::CastingSource GetCastingSourceData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::CastingSource>(this, CommonLibActorMagicCasterOffsets::CastingSource);
#else
        return eCastingSource;
#endif
    }

    uint8_t unk48[0x70];
    Actor* pCasterActor;
    NiNode* pMagicNode;
    NiPointer<uint64_t> spLight; // NiPointer<BSLight> spLight;
    void(__fastcall* pfInterruptHandler)(Actor*);
    uint64_t LoadGameSubBuffer; // BGSLoadGameSubBuffer LoadGameSubBuffer;
    BGSArtObject* pWaitingForCastingArt;
    WeaponEnchantmentController* pWeaponEnchantmentController;
    float fCostCharged;
    MagicSystem::CastingSource eCastingSource;
    uint32_t uiFlags;
};

static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::CasterActor == 0xB8);
static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::MagicNode == 0xC0);
static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::CastingSource == 0xF4);
static_assert(ActorMagicCaster::CommonLibActorMagicCasterOffsets::Size == 0x100);
static_assert(offsetof(ActorMagicCaster, pCasterActor) == ActorMagicCaster::LocalActorMagicCasterOffsets::CasterActor);
static_assert(offsetof(ActorMagicCaster, pMagicNode) == ActorMagicCaster::LocalActorMagicCasterOffsets::MagicNode);
static_assert(offsetof(ActorMagicCaster, eCastingSource) == ActorMagicCaster::LocalActorMagicCasterOffsets::CastingSource);
static_assert(sizeof(ActorMagicCaster) == ActorMagicCaster::LocalActorMagicCasterOffsets::Size);
