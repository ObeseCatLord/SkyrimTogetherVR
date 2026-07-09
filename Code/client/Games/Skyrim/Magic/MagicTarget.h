#pragma once

#include <Games/Magic/MagicSystem.h>
#include <RuntimeLayout.h>
#include <cstddef>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct Actor;
struct MagicItem;
struct EffectItem;
struct TESBoundObject;
struct ActiveEffect;

struct MagicTarget
{
    struct IPostCreationModification;
    struct ResultsCollector;

    struct ForEachActiveEffectVisitor
    {
        virtual ~ForEachActiveEffectVisitor();

        virtual bool Visit(ActiveEffect* apEffect) = 0;
    };
    static_assert(sizeof(ForEachActiveEffectVisitor) == 0x8);

    // this struct is a lot simpler in Fallout 4, it just passes a ref to AddTargetData
    struct ResetElapsedTimeMatchingEffects : ForEachActiveEffectVisitor
    {
        MagicItem* pSpell;
        Actor* pCaster;
        EffectItem* pEffectItem;
        TESBoundObject* pSource;
        MagicSystem::CastingSource eCastingSource;
        bool bResetOne;
    };
    static_assert(sizeof(ResetElapsedTimeMatchingEffects) == 0x30);

    struct HasSameUsageEffect : ForEachActiveEffectVisitor
    {
        EffectItem* pUsage;
        ActiveEffect* spFoundEffect;
    };
    static_assert(sizeof(HasSameUsageEffect) == 0x18);

    struct AddTargetData
    {
        using CommonLibAddTargetDataOffsets = Skyrim::RuntimeLayout::MagicTargetAddTargetDataCommonLibNgOffsets;
        using LocalAddTargetDataOffsets = Skyrim::RuntimeLayout::MagicTargetAddTargetDataLocalShimOffsets;

        bool CheckAddEffect(void* arArgs, float afResistance);

        bool ShouldSync();
        bool IsForbiddenEffect(Actor* apTarget);

        [[nodiscard]] Actor* GetCasterData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<Actor*>(this, CommonLibAddTargetDataOffsets::Caster);
#else
            return pCaster;
#endif
        }

        void SetCasterData(Actor* apCaster) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<Actor*>(this, CommonLibAddTargetDataOffsets::Caster, apCaster);
#else
            pCaster = apCaster;
#endif
        }

        [[nodiscard]] MagicItem* GetSpellData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<MagicItem*>(this, CommonLibAddTargetDataOffsets::MagicItem);
#else
            return pSpell;
#endif
        }

        void SetSpellData(MagicItem* apSpell) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<MagicItem*>(this, CommonLibAddTargetDataOffsets::MagicItem, apSpell);
#else
            pSpell = apSpell;
#endif
        }

        [[nodiscard]] EffectItem* GetEffectItemData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<EffectItem*>(this, CommonLibAddTargetDataOffsets::Effect);
#else
            return pEffectItem;
#endif
        }

        void SetEffectItemData(EffectItem* apEffectItem) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<EffectItem*>(this, CommonLibAddTargetDataOffsets::Effect, apEffectItem);
#else
            pEffectItem = apEffectItem;
#endif
        }

        [[nodiscard]] float GetMagnitudeData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibAddTargetDataOffsets::Magnitude);
#else
            return fMagnitude;
#endif
        }

        void SetMagnitudeData(float aMagnitude) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibAddTargetDataOffsets::Magnitude, aMagnitude);
#else
            fMagnitude = aMagnitude;
#endif
        }

        [[nodiscard]] float GetUnk40Data() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibAddTargetDataOffsets::Unk40);
#else
            return fUnkFloat1;
#endif
        }

        void SetUnk40Data(float aValue) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibAddTargetDataOffsets::Unk40, aValue);
#else
            fUnkFloat1 = aValue;
#endif
        }

        [[nodiscard]] MagicSystem::CastingSource GetCastingSourceData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<MagicSystem::CastingSource>(this, CommonLibAddTargetDataOffsets::CastingSource);
#else
            return eCastingSource;
#endif
        }

        void SetCastingSourceData(MagicSystem::CastingSource aeCastingSource) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<MagicSystem::CastingSource>(this, CommonLibAddTargetDataOffsets::CastingSource, aeCastingSource);
#else
            eCastingSource = aeCastingSource;
#endif
        }

        [[nodiscard]] bool IsDualCastData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibAddTargetDataOffsets::DualCast);
#else
            return bDualCast;
#endif
        }

        void SetDualCastData(bool abDualCast) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibAddTargetDataOffsets::DualCast, abDualCast);
#else
            bDualCast = abDualCast;
#endif
        }

        Actor* pCaster;
        MagicItem* pSpell;
        EffectItem* pEffectItem;
        TESBoundObject* pSource;
        IPostCreationModification* pCallback;
        ResultsCollector* pResultsCollector;
        NiPoint3 ExplosionLocation;
        float fMagnitude;
        float fUnkFloat1; // seems to always be 1.0?
        MagicSystem::CastingSource eCastingSource;
        bool bAreaTarget;
        bool bDualCast;
    };
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Caster == 0x0);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::MagicItem == 0x8);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Effect == 0x10);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Magnitude == 0x3C);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Unk40 == 0x40);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::CastingSource == 0x44);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::DualCast == 0x49);
    static_assert(AddTargetData::CommonLibAddTargetDataOffsets::Size == 0x50);
    static_assert(offsetof(AddTargetData, pCaster) == AddTargetData::LocalAddTargetDataOffsets::Caster);
    static_assert(offsetof(AddTargetData, pSpell) == AddTargetData::LocalAddTargetDataOffsets::MagicItem);
    static_assert(offsetof(AddTargetData, pEffectItem) == AddTargetData::LocalAddTargetDataOffsets::Effect);
    static_assert(offsetof(AddTargetData, fMagnitude) == AddTargetData::LocalAddTargetDataOffsets::Magnitude);
    static_assert(offsetof(AddTargetData, fUnkFloat1) == AddTargetData::LocalAddTargetDataOffsets::Unk40);
    static_assert(offsetof(AddTargetData, eCastingSource) == AddTargetData::LocalAddTargetDataOffsets::CastingSource);
    static_assert(offsetof(AddTargetData, bDualCast) == AddTargetData::LocalAddTargetDataOffsets::DualCast);
    static_assert(sizeof(AddTargetData) == AddTargetData::LocalAddTargetDataOffsets::Size);

    virtual ~MagicTarget();

    Actor* GetTargetAsActor();

    bool AddTarget(AddTargetData& arData, bool aHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept;
    // this function actually adds the effect
    bool CheckAddEffect(AddTargetData& arData) noexcept;
    void DispelAllSpells(bool aNow) noexcept;

    void* unk04;
    void* unk08;
};
