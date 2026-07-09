#include <TiltedOnlinePCH.h>

#include <Forms/TESNPC.h>

TP_THIS_FUNCTION(TSetLeveledNpc, TESNPC*, TESNPC, TESNPC*);
static TSetLeveledNpc* RealSetLeveledNpc = nullptr;

TESNPC* TP_MAKE_THISCALL(HookSetLeveledNpc, TESNPC, TESNPC* apSelectedNpc)
{
    spdlog::info("For TESNPC: {}, spawning: {}", apThis->GetFullNameData().GetFullNameStringData(),
                 apSelectedNpc->GetFullNameData().GetFullNameStringData());

    return TiltedPhoques::ThisCall(RealSetLeveledNpc, apThis, Cast<TESNPC>(TESForm::GetById(0x3B547)));
}

static TiltedPhoques::Initializer s_npcInitHooks(
    []()
    {
        POINTER_SKYRIMSE(TSetLeveledNpc, s_SetLeveledNpc, 14375);

        RealSetLeveledNpc = s_SetLeveledNpc.Get();

        // TP_HOOK(&RealSetLeveledNpc, HookSetLeveledNpc);
    });
