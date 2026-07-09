#include "Projectile.h"
#include <Games/Skyrim/Forms/TESObjectWEAP.h>
#include <Games/Skyrim/Forms/MagicItem.h>
#include <Games/Skyrim/Forms/TESAmmo.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <World.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Games/Skyrim/Forms/TESObjectCELL.h>
#include <Forms/SpellItem.h>
#include <Games/Skyrim/VR/VRHookPolicy.h>

TP_THIS_FUNCTION(TLaunch, BSPointerHandle<Projectile>*, BSPointerHandle<Projectile>, Projectile::LaunchData& arData);
static TLaunch* RealLaunch = nullptr;

BSPointerHandle<Projectile>* Projectile::Launch(BSPointerHandle<Projectile>* apResult, LaunchData& apLaunchData) noexcept
{
    BSPointerHandle<Projectile>* result = TiltedPhoques::ThisCall(RealLaunch, apResult, apLaunchData);

    TP_ASSERT(result, "No projectile handle returned.");
    if (!result)
    {
        spdlog::error("No projectile handle returned.");
        return nullptr;
    }

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    TP_ASSERT(pProjectile, "No projectile found.");
    if (!pProjectile)
    {
        spdlog::error("No projectile found.");
        return nullptr;
    }

    pProjectile->SetPowerData(apLaunchData.GetPowerData());

    return result;
}

BSPointerHandle<Projectile>* TP_MAKE_THISCALL(HookLaunch, BSPointerHandle<Projectile>, Projectile::LaunchData& arData)
{
    // sync concentration spells through spell cast sync, the rest through projectile sync
    if (auto* pSpellData = arData.GetSpellData())
    {
        if (auto* pSpell = Cast<SpellItem>(pSpellData))
        {
            if (pSpell->GetCastingTypeData() == MagicSystem::CastingType::CONCENTRATION)
            {
                return TiltedPhoques::ThisCall(RealLaunch, apThis, arData);
            }
        }
    }

    if (auto* pShooter = arData.GetShooterData())
    {
        Actor* pActor = Cast<Actor>(pShooter);
        if (pActor)
        {
            ActorExtension* pExtendedActor = pActor->GetExtension();
            if (pExtendedActor->IsRemote())
            {
                apThis->handle.iBits = 0;
                return apThis;
            }
        }
    }

    ProjectileLaunchedEvent Event{};
    Event.Origin = arData.GetOriginData();
    if (auto* pProjectileBase = arData.GetProjectileBaseData())
        Event.ProjectileBaseID = pProjectileBase->GetFormIdData();
    if (auto* pShooter = arData.GetShooterData())
        Event.ShooterID = pShooter->GetFormIdData();
    if (auto* pWeaponSource = arData.GetWeaponSourceData())
        Event.WeaponID = pWeaponSource->GetFormIdData();
    if (auto* pAmmoSource = arData.GetAmmoSourceData())
        Event.AmmoID = pAmmoSource->GetFormIdData();
    Event.ZAngle = arData.GetAngleZData();
    Event.XAngle = arData.GetAngleXData();
    Event.YAngle = 0.0f;
    if (auto* pParentCell = arData.GetParentCellData())
        Event.ParentCellID = pParentCell->GetFormIdData();
    if (auto* pSpell = arData.GetSpellData())
        Event.SpellID = pSpell->GetFormIdData();
    Event.CastingSource = arData.GetCastingSourceData();
    Event.UnkBool1 = false;
    Event.Area = arData.GetAreaData();
    Event.Power = arData.GetPowerData();
    Event.Scale = arData.GetScaleData();
    Event.AlwaysHit = arData.IsAlwaysHitData();
    Event.NoDamageOutsideCombat = arData.IsNoDamageOutsideCombatData();
    Event.AutoAim = arData.IsAutoAimData();
    Event.UnkBool2 = arData.IsChainShatterData();
    Event.DeferInitialization = arData.DefersInitializationData();
    Event.ForceConeOfFire = arData.ForcesConeOfFireData();

    auto result = TiltedPhoques::ThisCall(RealLaunch, apThis, arData);

    TP_ASSERT(result, "No projectile handle returned.");

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    TP_ASSERT(pProjectile, "No projectile found.");

    Event.Power = pProjectile->GetPowerData();

    World::Get().GetRunner().Trigger(Event);

    return result;
}

static TiltedPhoques::Initializer s_projectileHooks(
    []()
    {
        POINTER_SKYRIMSE(TLaunch, s_launch, 44108);

        RealLaunch = s_launch.Get();

        TP_HOOK(&RealLaunch, HookLaunch);

#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER, TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER_VR_RESOLVED)
        VersionDbPtr<uint8_t> hookLoc(34452);

        struct C : TiltedPhoques::CodeGenerator
        {
            C(uint8_t* apLoc)
            {
                // replicate
                mov(rbx, ptr[rsp + 0x50]);

                // nullptr check
                cmp(rbx, 0);
                jz("exit");
                // jump back
                jmp_S(apLoc + 0x379);

                L("exit");
                // return false; scratch space from the registers
                mov(al, 0);
                add(rsp, 0x138);
                pop(r15);
                pop(r14);
                pop(r13);
                pop(r12);
                pop(rdi);
                pop(rsi);
                pop(rbx);
                pop(rbp);
                ret();
            }
        } gen(hookLoc.Get());
        TiltedPhoques::Jump(hookLoc.Get() + 0x374, gen.getCode());
#endif
    });
