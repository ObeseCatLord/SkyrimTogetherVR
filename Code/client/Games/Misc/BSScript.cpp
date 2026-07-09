#include <Misc/BSScript.h>
#include <Misc/NativeFunction.h>

#include <World.h>
#include <Events/PapyrusFunctionRegisterEvent.h>
#include <Forms/TESForm.h>
#include <Misc/GameVM.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Games/PapyrusFunctions.h>

#include <spdlog/spdlog.h>

#include <mutex>

TP_THIS_FUNCTION(TRegisterPapyrusFunction, void, BSScript::IVirtualMachine, NativeFunction*);
TP_THIS_FUNCTION(TBindEverythingToScript, void, BSScript::IVirtualMachine*);
TP_THIS_FUNCTION(TSignaturesMatch, bool, BSScript::NativeFunction, BSScript::NativeFunction*);
TP_THIS_FUNCTION(TCompareVariables, int64_t, void, BSScript::Variable*, BSScript::Variable*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;
TBindEverythingToScript* RealBindEverythingToScript = nullptr;
TSignaturesMatch* RealSignaturesMatch = nullptr;
TCompareVariables* RealCompareVariables = nullptr;
static std::once_flag s_papyrusNativeHookOnce;

namespace
{
bool BindSkyrimTogetherNative(BSScript::IVirtualMachine* apVm, BSScript::IFunction* apFunction)
{
    if (!apVm || !apFunction)
        return false;

    auto* pFunctionName = apFunction->GetName().AsAscii();
    auto* pClassName = apFunction->GetObjectTypeName().AsAscii();
    const bool bound = apVm->BindNativeMethod(apFunction);

    if (!bound)
    {
        spdlog::warn("SkyrimTogetherVR Papyrus native bind failed: {}::{}", pClassName ? pClassName : "<unknown>", pFunctionName ? pFunctionName : "<unknown>");
    }

    return bound;
}
} // namespace

void TP_MAKE_THISCALL(HookRegisterPapyrusFunction, BSScript::IVirtualMachine, NativeFunction* apFunction)
{
    auto& runner = World::Get().GetRunner();

    PapyrusFunctionRegisterEvent event(apFunction->functionName.AsAscii(), apFunction->typeName.AsAscii(), apFunction->functionAddress);

    runner.Trigger(std::move(event));

    TiltedPhoques::ThisCall(RealRegisterPapyrusFunction, apThis, apFunction);
}

void TP_MAKE_THISCALL(HookBindEverythingToScript, BSScript::IVirtualMachine*)
{
    auto* pVm = apThis ? *apThis : nullptr;
    if (!pVm)
    {
        spdlog::warn("SkyrimTogetherVR Papyrus native bind hook received a null VM");
        TiltedPhoques::ThisCall(RealBindEverythingToScript, apThis);
        return;
    }

    BindSkyrimTogetherNative(pVm, new BSScript::IsRemotePlayerFunc("IsRemotePlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsRemotePlayer, BSScript::Variable::kBoolean));
    BindSkyrimTogetherNative(pVm, new BSScript::IsPlayerFunc("IsPlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsPlayer, BSScript::Variable::kBoolean));
    BindSkyrimTogetherNative(
        pVm, new BSScript::ConnectToSkyrimTogetherFunc("ConnectToSkyrimTogether", "SkyrimTogetherUtils", PapyrusFunctions::ConnectToSkyrimTogether, BSScript::Variable::kBoolean));
    BindSkyrimTogetherNative(
        pVm, new BSScript::DisconnectFromSkyrimTogetherFunc(
                 "DisconnectFromSkyrimTogether", "SkyrimTogetherUtils", PapyrusFunctions::DisconnectFromSkyrimTogether, BSScript::Variable::kBoolean));
    BindSkyrimTogetherNative(
        pVm,
        new BSScript::IsSkyrimTogetherConnectedFunc("IsSkyrimTogetherConnected", "SkyrimTogetherUtils", PapyrusFunctions::IsSkyrimTogetherConnected, BSScript::Variable::kBoolean));
    BindSkyrimTogetherNative(
        pVm,
        new BSScript::ConnectToSkyrimTogetherFunc(
            "SetSkyrimTogetherConnectionConfig", "SkyrimTogetherUtils", PapyrusFunctions::SetSkyrimTogetherConnectionConfig, BSScript::Variable::kBoolean));
    BindSkyrimTogetherNative(
        pVm, new BSScript::GetSkyrimTogetherConnectionStateFunc(
                 "GetSkyrimTogetherConnectionState", "SkyrimTogetherUtils", PapyrusFunctions::GetSkyrimTogetherConnectionState, BSScript::Variable::kString));
    BindSkyrimTogetherNative(
        pVm, new BSScript::GetSkyrimTogetherConnectionStateFunc(
                 "GetSkyrimTogetherConfiguredEndpoint", "SkyrimTogetherUtils", PapyrusFunctions::GetSkyrimTogetherConfiguredEndpoint, BSScript::Variable::kString));
    BindSkyrimTogetherNative(
        pVm, new BSScript::GetSkyrimTogetherConnectionStateFunc(
                 "GetSkyrimTogetherConfiguredPassword", "SkyrimTogetherUtils", PapyrusFunctions::GetSkyrimTogetherConfiguredPassword, BSScript::Variable::kString));
    BindSkyrimTogetherNative(
        pVm, new BSScript::GetSkyrimTogetherConnectionStateFunc(
                 "GetSkyrimTogetherStatusSummary", "SkyrimTogetherUtils", PapyrusFunctions::GetSkyrimTogetherStatusSummary, BSScript::Variable::kString));
    BindSkyrimTogetherNative(
        pVm, new BSScript::GetSkyrimTogetherConnectionStateFunc(
                 "GetSkyrimTogetherTelemetryReadout", "SkyrimTogetherUtils", PapyrusFunctions::GetSkyrimTogetherTelemetryReadout, BSScript::Variable::kString));
    BindSkyrimTogetherNative(
        pVm, new BSScript::DidLaunchSkyrimTogetherFunc(
                 "DidLaunchSkyrimTogether", "SkyrimTogetherVerifyLaunchScript", PapyrusFunctions::DidLaunchSkyrimTogether, BSScript::Variable::kBoolean));

    TiltedPhoques::ThisCall(RealBindEverythingToScript, apThis);
}

bool TP_MAKE_THISCALL(HookSignaturesMatch, BSScript::NativeFunction, BSScript::NativeFunction* apOther)
{
    /*
    if (!strcmp(apThis->GetName().AsAscii(), "IsRemotePlayer"))
        DebugBreak();
    */

    return TiltedPhoques::ThisCall(RealSignaturesMatch, apThis, apOther);
}

// This is a neat hack, but it has been disabled since it messes up other things like beastform.
// These kinds of issues should be solved with custom scripts now that we have SkyrimTogether.esp anyway.
int64_t TP_MAKE_THISCALL(HookCompareVariables, void, BSScript::Variable* apVar1, BSScript::Variable* apVar2)
{
    BSScript::Object* pObject1 = apVar1->GetObject();
    BSScript::Object* pObject2 = apVar2->GetObject();

    if (!pObject1 || !pObject2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    uint64_t handle1 = pObject1->GetHandle();
    uint64_t handle2 = pObject2->GetHandle();

    auto* pPolicy = GameVM::Get()->virtualMachine->GetObjectHandlePolicy();

    if (!pPolicy || !handle1 || !handle2 || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle1) || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle2) ||
        !pPolicy->IsHandleObjectAvailable(handle1) || !pPolicy->IsHandleObjectAvailable(handle2))
    {
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
    }

    Actor* pActor1 = pPolicy->GetObjectForHandle<Actor>(handle1);
    Actor* pActor2 = pPolicy->GetObjectForHandle<Actor>(handle2);

    if (!pActor1 || !pActor2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    if (pActor1 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor2->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }
    else if (pActor2 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor1->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }

    return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
}

void InstallSkyrimTogetherPapyrusNativeHooks()
{
    std::call_once(
        s_papyrusNativeHookOnce,
        []()
        {
            POINTER_SKYRIMSE(TBindEverythingToScript, s_bindEverythingToScript, 55739);

            RealBindEverythingToScript = s_bindEverythingToScript.Get();

            TP_HOOK(&RealBindEverythingToScript, HookBindEverythingToScript);
        });
}

static TiltedPhoques::Initializer s_vmHooks(
    []()
    {
        POINTER_SKYRIMSE(TRegisterPapyrusFunction, s_registerPapyrusFunction, 104788);
        POINTER_SKYRIMSE(TSignaturesMatch, s_signaturesMatch, 104359);

        // POINTER_SKYRIMSE(TCompareVariables, s_compareVariables, 105220);

        RealRegisterPapyrusFunction = s_registerPapyrusFunction.Get();
        RealSignaturesMatch = s_signaturesMatch.Get();
        // RealCompareVariables = s_compareVariables.Get();

        InstallSkyrimTogetherPapyrusNativeHooks();
        TP_HOOK(&RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
        TP_HOOK(&RealSignaturesMatch, HookSignaturesMatch);
        // TP_HOOK(&RealCompareVariables, HookCompareVariables);
    });
