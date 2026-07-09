#include "ActorMagicCaster.h"

#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>

#include <Games/Skyrim/Actor.h>
#include <Games/ActorExtension.h>

#include <World.h>

TP_THIS_FUNCTION(TSpellCast, void, ActorMagicCaster, bool sbSuccess, int32_t auiTargetCount, MagicItem* apSpell);
TP_THIS_FUNCTION(TInterruptCast, void, ActorMagicCaster, bool);

static TSpellCast* RealSpellCast = nullptr;
static TInterruptCast* RealInterruptCast = nullptr;

void TP_MAKE_THISCALL(HookSpellCast, ActorMagicCaster, bool abSuccess, int32_t auiTargetCount, MagicItem* apSpell)
{
    spdlog::debug("HookSpellCast, abSuccess: {}, auiTargetCount: {}, apSpell: {:X}", abSuccess, auiTargetCount, (uint64_t)apSpell);

    // Note: these if guards is how the game does it too
    Actor* pCasterActor = apThis->GetCasterActorData();
    if (!pCasterActor)
        return;
    if (!abSuccess)
        return;
    if (!apSpell && !apThis->GetCurrentSpellData())
        return;

    ActorExtension* pCasterExtension = pCasterActor->GetExtension();
    if (pCasterExtension && pCasterExtension->IsRemote())
        return;

    uint32_t targetFormId = 0;

    const auto desiredTarget = apThis->GetDesiredTargetData();
    if (desiredTarget)
    {
        TESObjectREFR* pDesiredTarget = TESObjectREFR::GetByHandle(desiredTarget.handle.iBits);
        if (pDesiredTarget)
        {
            targetFormId = pDesiredTarget->GetFormIdData();
        }
    }

    if (apSpell)
        World::Get().GetRunner().Trigger(SpellCastEvent(apThis, apSpell->GetFormIdData(), targetFormId));

    TiltedPhoques::ThisCall(RealSpellCast, apThis, abSuccess, auiTargetCount, apSpell);
}

void TP_MAKE_THISCALL(HookInterruptCast, ActorMagicCaster, bool abRefund)
{
    Actor* pCasterActor = apThis->GetCasterActorData();
    if (!pCasterActor)
    {
        TiltedPhoques::ThisCall(RealInterruptCast, apThis, abRefund);
        return;
    }

    ActorExtension* pExtended = pCasterActor->GetExtension();

    if (pExtended && pExtended->IsLocal())
        World::Get().GetRunner().Trigger(InterruptCastEvent(pCasterActor->GetFormIdData(), apThis->GetCastingSourceData()));

    TiltedPhoques::ThisCall(RealInterruptCast, apThis, abRefund);
}

static TiltedPhoques::Initializer s_actorMagicCasterHooks(
    []()
    {
        POINTER_SKYRIMSE(TSpellCast, s_spellCast, 34144);
        POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 34140);

        RealSpellCast = s_spellCast.Get();
        RealInterruptCast = s_interruptCast.Get();

        TP_HOOK(&RealSpellCast, HookSpellCast);
        TP_HOOK(&RealInterruptCast, HookInterruptCast);
    });
