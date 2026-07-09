#pragma once

#include <cstddef>
#include <cstdint>

#include <Games/Skyrim/TESObjectREFR.h>
#include <Games/Magic/MagicSystem.h>
#include <RuntimeLayout.h>

struct BGSProjectile;
struct TESObjectREFR;
struct TESObjectWEAP;
struct TESAmmo;
struct TESObjectCELL;
struct MagicItem;
struct AlchemyItem;
struct EnchantmentItem;
struct CombatController;
struct TESForm;

struct Projectile : TESObjectREFR
{
    using CommonLibProjectileOffsets = Skyrim::RuntimeLayout::ProjectileCommonLibNgOffsets;
    using LocalProjectileOffsets = Skyrim::RuntimeLayout::ProjectileLocalShimOffsets;

    struct LaunchData
    {
        using CommonLibLaunchDataOffsets = Skyrim::RuntimeLayout::ProjectileLaunchDataCommonLibNgOffsets;
        using LocalLaunchDataOffsets = Skyrim::RuntimeLayout::ProjectileLaunchDataLocalShimOffsets;

        virtual ~LaunchData() = default;

        [[nodiscard]] const NiPoint3& GetOriginData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Ref<NiPoint3>(this, CommonLibLaunchDataOffsets::Origin);
#else
            return origin;
#endif
        }

        void SetOriginData(const NiPoint3& acOrigin) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<NiPoint3>(this, CommonLibLaunchDataOffsets::Origin, acOrigin);
#else
            origin = acOrigin;
#endif
        }

        [[nodiscard]] const NiPoint3& GetContactNormalData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Ref<NiPoint3>(this, CommonLibLaunchDataOffsets::ContactNormal);
#else
            return contactNormal;
#endif
        }

        void SetContactNormalData(const NiPoint3& acContactNormal) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<NiPoint3>(this, CommonLibLaunchDataOffsets::ContactNormal, acContactNormal);
#else
            contactNormal = acContactNormal;
#endif
        }

        [[nodiscard]] TESForm* GetProjectileBaseData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESForm*>(this, CommonLibLaunchDataOffsets::ProjectileBase);
#else
            return projectileBase;
#endif
        }

        void SetProjectileBaseData(TESForm* apProjectileBase) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<TESForm*>(this, CommonLibLaunchDataOffsets::ProjectileBase, apProjectileBase);
#else
            projectileBase = apProjectileBase;
#endif
        }

        [[nodiscard]] TESObjectREFR* GetShooterData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESObjectREFR*>(this, CommonLibLaunchDataOffsets::Shooter);
#else
            return shooter;
#endif
        }

        void SetShooterData(TESObjectREFR* apShooter) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<TESObjectREFR*>(this, CommonLibLaunchDataOffsets::Shooter, apShooter);
#else
            shooter = apShooter;
#endif
        }

        [[nodiscard]] CombatController* GetCombatControllerData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<CombatController*>(this, CommonLibLaunchDataOffsets::CombatController);
#else
            return combatController;
#endif
        }

        void SetCombatControllerData(CombatController* apCombatController) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<CombatController*>(this, CommonLibLaunchDataOffsets::CombatController, apCombatController);
#else
            combatController = apCombatController;
#endif
        }

        [[nodiscard]] TESObjectWEAP* GetWeaponSourceData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESObjectWEAP*>(this, CommonLibLaunchDataOffsets::WeaponSource);
#else
            return weaponSource;
#endif
        }

        void SetWeaponSourceData(TESObjectWEAP* apWeaponSource) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<TESObjectWEAP*>(this, CommonLibLaunchDataOffsets::WeaponSource, apWeaponSource);
#else
            weaponSource = apWeaponSource;
#endif
        }

        [[nodiscard]] TESAmmo* GetAmmoSourceData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESAmmo*>(this, CommonLibLaunchDataOffsets::AmmoSource);
#else
            return ammoSource;
#endif
        }

        void SetAmmoSourceData(TESAmmo* apAmmoSource) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<TESAmmo*>(this, CommonLibLaunchDataOffsets::AmmoSource, apAmmoSource);
#else
            ammoSource = apAmmoSource;
#endif
        }

        [[nodiscard]] float GetAngleZData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibLaunchDataOffsets::AngleZ);
#else
            return angleZ;
#endif
        }

        void SetAngleZData(float aAngleZ) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibLaunchDataOffsets::AngleZ, aAngleZ);
#else
            angleZ = aAngleZ;
#endif
        }

        [[nodiscard]] float GetAngleXData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibLaunchDataOffsets::AngleX);
#else
            return angleX;
#endif
        }

        void SetAngleXData(float aAngleX) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibLaunchDataOffsets::AngleX, aAngleX);
#else
            angleX = aAngleX;
#endif
        }

        [[nodiscard]] void* GetUnk50Data() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<void*>(this, CommonLibLaunchDataOffsets::Unk50);
#else
            return unk50;
#endif
        }

        void SetUnk50Data(void* apUnk50) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<void*>(this, CommonLibLaunchDataOffsets::Unk50, apUnk50);
#else
            unk50 = apUnk50;
#endif
        }

        [[nodiscard]] TESObjectREFR* GetDesiredTargetData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESObjectREFR*>(this, CommonLibLaunchDataOffsets::DesiredTarget);
#else
            return desiredTarget;
#endif
        }

        void SetDesiredTargetData(TESObjectREFR* apDesiredTarget) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<TESObjectREFR*>(this, CommonLibLaunchDataOffsets::DesiredTarget, apDesiredTarget);
#else
            desiredTarget = apDesiredTarget;
#endif
        }

        [[nodiscard]] float GetUnk60Data() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibLaunchDataOffsets::Unk60);
#else
            return unk60;
#endif
        }

        void SetUnk60Data(float aUnk60) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibLaunchDataOffsets::Unk60, aUnk60);
#else
            unk60 = aUnk60;
#endif
        }

        [[nodiscard]] float GetUnk64Data() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibLaunchDataOffsets::Unk64);
#else
            return unk64;
#endif
        }

        void SetUnk64Data(float aUnk64) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibLaunchDataOffsets::Unk64, aUnk64);
#else
            unk64 = aUnk64;
#endif
        }

        [[nodiscard]] TESObjectCELL* GetParentCellData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<TESObjectCELL*>(this, CommonLibLaunchDataOffsets::ParentCell);
#else
            return parentCell;
#endif
        }

        void SetParentCellData(TESObjectCELL* apParentCell) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<TESObjectCELL*>(this, CommonLibLaunchDataOffsets::ParentCell, apParentCell);
#else
            parentCell = apParentCell;
#endif
        }

        [[nodiscard]] MagicItem* GetSpellData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<MagicItem*>(this, CommonLibLaunchDataOffsets::Spell);
#else
            return spell;
#endif
        }

        void SetSpellData(MagicItem* apSpell) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<MagicItem*>(this, CommonLibLaunchDataOffsets::Spell, apSpell);
#else
            spell = apSpell;
#endif
        }

        [[nodiscard]] MagicSystem::CastingSource GetCastingSourceData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<MagicSystem::CastingSource>(this, CommonLibLaunchDataOffsets::CastingSource);
#else
            return castingSource;
#endif
        }

        void SetCastingSourceData(MagicSystem::CastingSource aeCastingSource) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<MagicSystem::CastingSource>(this, CommonLibLaunchDataOffsets::CastingSource, aeCastingSource);
#else
            castingSource = aeCastingSource;
#endif
        }

        [[nodiscard]] EnchantmentItem* GetEnchantmentData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<EnchantmentItem*>(this, CommonLibLaunchDataOffsets::Enchantment);
#else
            return enchantItem;
#endif
        }

        void SetEnchantmentData(EnchantmentItem* apEnchantment) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<EnchantmentItem*>(this, CommonLibLaunchDataOffsets::Enchantment, apEnchantment);
#else
            enchantItem = apEnchantment;
#endif
        }

        [[nodiscard]] AlchemyItem* GetPoisonData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<AlchemyItem*>(this, CommonLibLaunchDataOffsets::Poison);
#else
            return poison;
#endif
        }

        void SetPoisonData(AlchemyItem* apPoison) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<AlchemyItem*>(this, CommonLibLaunchDataOffsets::Poison, apPoison);
#else
            poison = apPoison;
#endif
        }

        [[nodiscard]] int32_t GetAreaData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibLaunchDataOffsets::Area);
#else
            return area;
#endif
        }

        void SetAreaData(int32_t aArea) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<int32_t>(this, CommonLibLaunchDataOffsets::Area, aArea);
#else
            area = aArea;
#endif
        }

        [[nodiscard]] float GetPowerData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibLaunchDataOffsets::Power);
#else
            return power;
#endif
        }

        void SetPowerData(float aPower) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibLaunchDataOffsets::Power, aPower);
#else
            power = aPower;
#endif
        }

        [[nodiscard]] float GetScaleData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<float>(this, CommonLibLaunchDataOffsets::Scale);
#else
            return scale;
#endif
        }

        void SetScaleData(float aScale) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<float>(this, CommonLibLaunchDataOffsets::Scale, aScale);
#else
            scale = aScale;
#endif
        }

        [[nodiscard]] bool IsAlwaysHitData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::AlwaysHit);
#else
            return alwaysHit;
#endif
        }

        void SetAlwaysHitData(bool abAlwaysHit) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::AlwaysHit, abAlwaysHit);
#else
            alwaysHit = abAlwaysHit;
#endif
        }

        [[nodiscard]] bool IsNoDamageOutsideCombatData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::NoDamageOutsideCombat);
#else
            return noDamageOutsideCombat;
#endif
        }

        void SetNoDamageOutsideCombatData(bool abNoDamageOutsideCombat) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::NoDamageOutsideCombat, abNoDamageOutsideCombat);
#else
            noDamageOutsideCombat = abNoDamageOutsideCombat;
#endif
        }

        [[nodiscard]] bool IsAutoAimData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::AutoAim);
#else
            return autoAim;
#endif
        }

        void SetAutoAimData(bool abAutoAim) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::AutoAim, abAutoAim);
#else
            autoAim = abAutoAim;
#endif
        }

        [[nodiscard]] bool IsChainShatterData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::ChainShatter);
#else
            return chainShatter;
#endif
        }

        void SetChainShatterData(bool abChainShatter) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::ChainShatter, abChainShatter);
#else
            chainShatter = abChainShatter;
#endif
        }

        [[nodiscard]] bool UsesOriginData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::UseOrigin);
#else
            return useOrigin;
#endif
        }

        void SetUseOriginData(bool abUseOrigin) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::UseOrigin, abUseOrigin);
#else
            useOrigin = abUseOrigin;
#endif
        }

        [[nodiscard]] bool DefersInitializationData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::DeferInitialization);
#else
            return deferInitialization;
#endif
        }

        void SetDeferInitializationData(bool abDeferInitialization) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::DeferInitialization, abDeferInitialization);
#else
            deferInitialization = abDeferInitialization;
#endif
        }

        [[nodiscard]] bool ForcesConeOfFireData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<bool>(this, CommonLibLaunchDataOffsets::ForceConeOfFire);
#else
            return forceConeOfFire;
#endif
        }

        void SetForceConeOfFireData(bool abForceConeOfFire) noexcept
        {
#if TP_SKYRIM_VR
            Skyrim::RuntimeLayout::Store<bool>(this, CommonLibLaunchDataOffsets::ForceConeOfFire, abForceConeOfFire);
#else
            forceConeOfFire = abForceConeOfFire;
#endif
        }

        NiPoint3 origin;
        NiPoint3 contactNormal;
        TESForm* projectileBase; // CommonLib names this BGSProjectile*
        TESObjectREFR* shooter;
        CombatController* combatController;
        TESObjectWEAP* weaponSource;
        TESAmmo* ammoSource;
        float angleZ;
        float angleX;
        void* unk50; // maps to Projectile runtime unk110
        TESObjectREFR* desiredTarget;
        float unk60; // maps to Projectile runtime unk1A8
        float unk64; // maps to Projectile runtime unk1AC
        TESObjectCELL* parentCell;
        MagicItem* spell;
        MagicSystem::CastingSource castingSource;
        uint32_t pad7C;
        EnchantmentItem* enchantItem;
        AlchemyItem* poison;
        int32_t area;
        float power; // init to 1.0 and set to 1.0 before launch in TESObjectWEAP::Fire()
        float scale; // init to 1.0 and set to 1.0 before launch in TESObjectWEAP::Fire()
        bool alwaysHit;
        bool noDamageOutsideCombat;
        bool autoAim;
        bool chainShatter;
        bool useOrigin;
        bool deferInitialization;
        bool forceConeOfFire;
    };

    uint8_t unkA0[0x120 - sizeof(TESObjectREFR)];
    void* pActorCause;
    uint32_t hShooter;
    uint8_t unk[0x190 - 0x12C];
    float fPower;
    float fSpeedMult;
    float fRange;
    float fAge;
    float fDamage;

    [[nodiscard]] float GetPowerData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibProjectileOffsets::Power);
#else
        return fPower;
#endif
    }

    void SetPowerData(float aPower) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibProjectileOffsets::Power, aPower);
#else
        fPower = aPower;
#endif
    }

    static BSPointerHandle<Projectile>* Launch(BSPointerHandle<Projectile>* apResult, LaunchData& arData) noexcept;
};

static_assert(sizeof(Projectile::LaunchData) == 0xA8);
static_assert(offsetof(Projectile::LaunchData, origin) == 0x8);
static_assert(offsetof(Projectile::LaunchData, contactNormal) == 0x14);
static_assert(offsetof(Projectile::LaunchData, projectileBase) == 0x20);
static_assert(offsetof(Projectile::LaunchData, shooter) == 0x28);
static_assert(offsetof(Projectile::LaunchData, combatController) == 0x30);
static_assert(offsetof(Projectile::LaunchData, weaponSource) == 0x38);
static_assert(offsetof(Projectile::LaunchData, ammoSource) == 0x40);
static_assert(offsetof(Projectile::LaunchData, angleZ) == 0x48);
static_assert(offsetof(Projectile::LaunchData, angleX) == 0x4C);
static_assert(offsetof(Projectile::LaunchData, unk50) == 0x50);
static_assert(offsetof(Projectile::LaunchData, desiredTarget) == 0x58);
static_assert(offsetof(Projectile::LaunchData, unk60) == 0x60);
static_assert(offsetof(Projectile::LaunchData, unk64) == 0x64);
static_assert(offsetof(Projectile::LaunchData, parentCell) == 0x68);
static_assert(offsetof(Projectile::LaunchData, spell) == 0x70);
static_assert(offsetof(Projectile::LaunchData, castingSource) == 0x78);
static_assert(offsetof(Projectile::LaunchData, pad7C) == 0x7C);
static_assert(offsetof(Projectile::LaunchData, enchantItem) == 0x80);
static_assert(offsetof(Projectile::LaunchData, poison) == 0x88);
static_assert(offsetof(Projectile::LaunchData, area) == 0x90);
static_assert(offsetof(Projectile::LaunchData, power) == 0x94);
static_assert(offsetof(Projectile::LaunchData, scale) == 0x98);
static_assert(offsetof(Projectile::LaunchData, alwaysHit) == 0x9C);
static_assert(offsetof(Projectile::LaunchData, noDamageOutsideCombat) == 0x9D);
static_assert(offsetof(Projectile::LaunchData, autoAim) == 0x9E);
static_assert(offsetof(Projectile::LaunchData, chainShatter) == 0x9F);
static_assert(offsetof(Projectile::LaunchData, useOrigin) == 0xA0);
static_assert(offsetof(Projectile::LaunchData, deferInitialization) == 0xA1);
static_assert(offsetof(Projectile::LaunchData, forceConeOfFire) == 0xA2);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Origin == 0x8);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::ProjectileBase == 0x20);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Shooter == 0x28);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Spell == 0x70);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::CastingSource == 0x78);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Power == 0x94);
static_assert(Projectile::LaunchData::CommonLibLaunchDataOffsets::Size == 0xA8);
static_assert(Projectile::CommonLibProjectileOffsets::Power == 0x188);
static_assert(offsetof(Projectile, fPower) == Projectile::LocalProjectileOffsets::Power);
