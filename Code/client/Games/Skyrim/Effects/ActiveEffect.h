#pragma once

#include <Games/Magic/MagicSystem.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct Actor;
struct MagicItem;
struct MagicTarget;
struct EffectItem;
struct NiNode;
struct TESBoundObject;

struct ActiveEffect
{
    using CommonLibActiveEffectOffsets = Skyrim::RuntimeLayout::ActiveEffectCommonLibNgOffsets;
    using LocalActiveEffectOffsets = Skyrim::RuntimeLayout::ActiveEffectLocalShimOffsets;

    // this ctor is used by seemingly all ActiveEffect child classes' Instantiate() methods
    // can be used to find individual Instantiate() methods
    // address: 0x140C4E350
    ActiveEffect(Actor* apCaster, MagicItem* apSpell, EffectItem* apEffect);

    virtual void sub_0();
    virtual void sub_1();
    virtual void sub_2();
    virtual void sub_3();
    virtual void sub_4();
    virtual void sub_5();
    virtual void sub_6();
    virtual void sub_7();
    virtual void sub_8();
    virtual void sub_9();
    virtual void sub_A();
    virtual void sub_B();
    virtual void sub_C();
    virtual void sub_D();
    virtual void sub_E();
    virtual void sub_F();
    virtual void sub_10();
    virtual void sub_11();
    virtual void sub_12();
    virtual void sub_13();
    virtual void Start();
    virtual void sub_15();
    virtual void sub_16();
    virtual void sub_17();
    virtual void sub_18();

    static ActiveEffect* Instantiate(Actor* apCaster, MagicItem* apSpell, EffectItem* apEffect);

    [[nodiscard]] uint32_t GetCasterHandleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibActiveEffectOffsets::Caster);
#else
        return hCaster;
#endif
    }

    [[nodiscard]] MagicItem* GetSpellData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicItem*>(this, CommonLibActiveEffectOffsets::Spell);
#else
        return pSpell;
#endif
    }

    [[nodiscard]] void* GetEffectData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<void*>(this, CommonLibActiveEffectOffsets::Effect);
#else
        return pEffect;
#endif
    }

    [[nodiscard]] MagicTarget* GetTargetData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicTarget*>(this, CommonLibActiveEffectOffsets::Target);
#else
        return pTarget;
#endif
    }

    [[nodiscard]] float GetElapsedSecondsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibActiveEffectOffsets::ElapsedSeconds);
#else
        return fElapsedSeconds;
#endif
    }

    void SetElapsedSecondsData(float aElapsedSeconds) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibActiveEffectOffsets::ElapsedSeconds, aElapsedSeconds);
#else
        fElapsedSeconds = aElapsedSeconds;
#endif
    }

    [[nodiscard]] float GetDurationData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibActiveEffectOffsets::Duration);
#else
        return fDuration;
#endif
    }

    [[nodiscard]] float GetMagnitudeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibActiveEffectOffsets::Magnitude);
#else
        return fMagnitude;
#endif
    }

    void SetMagnitudeData(float aMagnitude) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibActiveEffectOffsets::Magnitude, aMagnitude);
#else
        fMagnitude = aMagnitude;
#endif
    }

    [[nodiscard]] uint32_t GetFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibActiveEffectOffsets::Flags);
#else
        return uiFlags;
#endif
    }

    [[nodiscard]] MagicSystem::CastingSource GetCastingSourceData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MagicSystem::CastingSource>(this, CommonLibActiveEffectOffsets::CastingSource);
#else
        return eCastingSource;
#endif
    }

    uint8_t pad8[0x34 - 0x8];
    uint32_t hCaster;
    NiNode* pSourceNode;
    MagicItem* pSpell;
    void* pEffect;
    MagicTarget* pTarget;
    TESBoundObject* pSource;
    void* pHitEffects;
    MagicItem* pDisplacementSpell;
    float fElapsedSeconds;
    float fDuration;
    float fMagnitude;
    uint32_t uiFlags;
    uint32_t eConditionStatus;
    uint16_t usUniqueID;
    MagicSystem::CastingSource eCastingSource;
};
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Spell == 0x40);
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Target == 0x50);
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::ElapsedSeconds == 0x70);
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Duration == 0x74);
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Magnitude == 0x78);
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::Flags == 0x7C);
static_assert(ActiveEffect::CommonLibActiveEffectOffsets::CastingSource == 0x88);
static_assert(offsetof(ActiveEffect, hCaster) == ActiveEffect::LocalActiveEffectOffsets::Caster);
static_assert(offsetof(ActiveEffect, pSpell) == ActiveEffect::LocalActiveEffectOffsets::Spell);
static_assert(offsetof(ActiveEffect, pTarget) == ActiveEffect::LocalActiveEffectOffsets::Target);
static_assert(offsetof(ActiveEffect, fElapsedSeconds) == ActiveEffect::LocalActiveEffectOffsets::ElapsedSeconds);
static_assert(offsetof(ActiveEffect, fDuration) == ActiveEffect::LocalActiveEffectOffsets::Duration);
static_assert(offsetof(ActiveEffect, fMagnitude) == ActiveEffect::LocalActiveEffectOffsets::Magnitude);
static_assert(offsetof(ActiveEffect, uiFlags) == ActiveEffect::LocalActiveEffectOffsets::Flags);
static_assert(offsetof(ActiveEffect, eCastingSource) == ActiveEffect::LocalActiveEffectOffsets::CastingSource);
static_assert(sizeof(ActiveEffect) == ActiveEffect::LocalActiveEffectOffsets::Size);

namespace ActiveEffectFactory
{
ActiveEffect* Activate(Actor* apCaster, MagicSystem::CastingSource aeCastingSource, MagicItem* apSpell, EffectItem* apEffectItem, TESBoundObject* apSource, bool abWornEnchantment);
};
