#include "InvisibilityEffect.h"

#include <Games/ActorExtension.h>
#include <Forms/ActorValueInfo.h>
#include <Magic/MagicTarget.h>

TP_THIS_FUNCTION(TFinish, void, InvisibilityEffect);
static TFinish* RealFinish = nullptr;

// This needs to be done because the actor value does not update in time
void TP_MAKE_THISCALL(HookFinish, InvisibilityEffect)
{
    auto* pTarget = apThis ? apThis->GetTargetData() : nullptr;
    if (pTarget)
    {
        if (Actor* pActor = pTarget->GetTargetAsActor())
        {
            if (pActor->GetExtension()->IsRemote())
                pActor->SetActorValue(ActorValueInfo::kInvisibility, 0.f);
        }
    }

    TiltedPhoques::ThisCall(RealFinish, apThis);
}

static TiltedPhoques::Initializer s_invisibilityEffectsHooks(
    []()
    {
        POINTER_SKYRIMSE(TFinish, s_finish, 34370);

        RealFinish = s_finish.Get();

        TP_HOOK(&RealFinish, HookFinish);
    });
