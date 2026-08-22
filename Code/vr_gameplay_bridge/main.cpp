#include "CommandExecutor.h"
#include "ActivationHooks.h"
#include "ActorActionHooks.h"
#include "ActorAuthorityHooks.h"
#include "CalendarHooks.h"
#include "DialogueProcessResponseHook.h"
#include "DialogueHooks.h"
#include "DropHooks.h"
#include "EquipmentAuthorityHooks.h"
#include "EventCapture.h"
#include "InvisibilityHooks.h"
#include "MagicHooks.h"
#include "PapyrusBindings.h"
#include "ProgressionHooks.h"
#include "ProjectileHooks.h"
#include "QuestNativeAccess.h"
#include "RemoteSaveExclusion.h"
#include "RemoteSolvedPosePresentation.h"
#include "SummonAuthorityHooks.h"
#include "WaypointHooks.h"
#include "WeatherNativeAccess.h"
#include "VRTextInput.h"
#include "VrNoThrow.h"

#include <cstdio>
#include <memory>

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
void InitializeLogging() noexcept
{
    try {
        auto path = SKSE::log::log_directory();
        if (!path)
            return;
        *path /= "SkyrimTogetherVRGameplayBridge.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("SkyrimTogetherVRGameplayBridge", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
    } catch (...) {
    }
}

[[nodiscard]] bool DetachAllGameplayHooks() noexcept
{
    bool detached = true;
    detached = SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::ActorActionHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::InvisibilityHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::ProgressionHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::WaypointHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::DropHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::ActivationHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::CalendarHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::MagicHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::ProjectileHooks::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHook::Uninstall() && detached;
    detached = SkyrimTogetherVR::GameplayAdapter::DialogueHooks::Uninstall() && detached;
    return detached;
}

[[nodiscard]] bool FinishFaultedLoad(
    SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint& aEndpoint,
    const char* aReason,
    const bool a_mustRemainResident = false) noexcept
{
    SkyrimTogetherVR::GameplayAdapter::NoThrow::BestEffort([&] { aEndpoint.Fault(aReason); });
    const bool detached = DetachAllGameplayHooks();
    if (!a_mustRemainResident && detached)
        return false;

    if (!detached)
    {
        SkyrimTogetherVR::GameplayAdapter::NoThrow::BestEffort([&] { SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: load failed but at least one detour could not be proven detached; retaining the faulted plugin so its trampoline remains callable");
        });
    }
    else
    {
        SkyrimTogetherVR::GameplayAdapter::NoThrow::BestEffort([&] { SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: load faulted after an irreversible SKSE callback registration; retaining the inert plugin to keep callback code resident");
        });
    }
    return true;
}
} // namespace

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    bool mustRemainResident = false;
    try {
        if (!a_skse)
            return false;

        const auto interfaceRuntime = a_skse->RuntimeVersion();
        const auto skseVersion = a_skse->SKSEVersion();
        const auto releaseIndex = skseVersion >= SkyrimTogetherVR::GameplayBridge::kMinimumSkseVrVersion ?
                                      a_skse->GetReleaseIndex() :
                                      0;
        if (interfaceRuntime.pack() != SkyrimTogetherVR::GameplayBridge::kSkseVrInterfaceRuntimeVersion ||
            skseVersion < SkyrimTogetherVR::GameplayBridge::kMinimumSkseVrVersion ||
            releaseIndex < SkyrimTogetherVR::GameplayBridge::kMinimumSkseVrReleaseIndex) {
            char message[256]{};
            _snprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                "SkyrimTogetherVRGameplayBridge: unsupported loader contract runtime=0x%08X skse=0x%08X release=%u\n",
                interfaceRuntime.pack(),
                skseVersion,
                releaseIndex);
            OutputDebugStringA(message);
            return false;
        }

        SKSE::Init(a_skse);
        InitializeLogging();
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: validated loader runtime=0x{:08X} skse=0x{:08X} release={}",
            interfaceRuntime.pack(),
            skseVersion,
            releaseIndex);

        auto& endpoint = SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint::Get();
        if (!endpoint.Attach())
            return false;

        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging)
            return FinishFaultedLoad(endpoint, "SKSE messaging interface is unavailable");
        if (!SkyrimTogetherVR::GameplayAdapter::RemoteSaveExclusion::ValidateTarget()) {
            return FinishFaultedLoad(endpoint, "exact remote actor save-exclusion target validation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::WeatherNativeAccess::ValidateTargets()) {
            return FinishFaultedLoad(endpoint, "exact Sky weather target validation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::DialogueHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact Actor::SpeakSound hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHook::Install()) {
            return FinishFaultedLoad(endpoint, "exact AIProcess::ProcessResponse hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::ProjectileHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact Projectile::Launch hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::MagicHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact magic hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::CalendarHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact Calendar::Update hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::ActivationHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact TESObjectREFR::Activate hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::DropHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact PlayerCharacter::DropObject hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::WaypointHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact local waypoint hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess::ValidateTarget()) {
            return FinishFaultedLoad(endpoint, "exact TESQuest::SetStage target validation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::ProgressionHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact progression hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact actor authority hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::SummonAuthorityHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact SummonCreatureEffect authority hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact equipment authority hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::InvisibilityHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact InvisibilityEffect::Finish hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::ActorActionHooks::Install()) {
            return FinishFaultedLoad(endpoint, "exact ActorMediator action hook installation failed");
        }
        if (!SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation::Install()) {
            return FinishFaultedLoad(endpoint, "exact Character::UpdateAnimation solved-pose hook installation failed");
        }
        // SKSE exposes no callback unregister operation. Do all reversible
        // target validation and hook installation first. From the first
        // registration attempt onward, any failure keeps a faulted inert DLL
        // resident so SKSE can never call into unloaded code.
        mustRemainResident = true;
        if (!SkyrimTogetherVR::GameplayAdapter::RegisterPapyrusBindings()) {
            return FinishFaultedLoad(endpoint, "Papyrus registration failed", true);
        }
        if (!messaging->RegisterListener(SkyrimTogetherVR::GameplayAdapter::HandleSkseMessage)) {
            return FinishFaultedLoad(endpoint, "SKSE messaging listener registration failed", true);
        }

        SkyrimTogetherVR::GameplayAdapter::PublishPluginLoaded();
        SKSE::log::info("SkyrimTogetherVRGameplayBridge loaded");
        return true;
    } catch (...) {
        return FinishFaultedLoad(
            SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint::Get(),
            "exception during SKSEPluginLoad",
            mustRemainResident);
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl STVRGameplayBridge_ProcessCommands(
    const std::uint32_t a_callerProcessId,
    const std::uint32_t a_callerThreadId,
    const std::uint64_t a_lifecycleEpoch,
    const std::uint32_t a_maxCommands) noexcept
{
    try {
        const auto result = SkyrimTogetherVR::GameplayAdapter::ProcessCommands(
            a_callerProcessId,
            a_callerThreadId,
            a_lifecycleEpoch,
            a_maxCommands);
        if (result != SkyrimTogetherVR::GameplayBridge::CommandPumpResult::Success)
            return static_cast<std::uint32_t>(result);

        auto& textInput = SkyrimTogetherVR::GameplayAdapter::VRTextInput::Get();
        static std::uint64_t lastTextInputLifecycleEpoch{};
        if (lastTextInputLifecycleEpoch != 0 && lastTextInputLifecycleEpoch != a_lifecycleEpoch)
            textInput.Shutdown();
        // VR text entry is optional UI.  Its availability must never turn a
        // successful gameplay command pump into a networking failure.
        if (textInput.Initialize())
            textInput.Pump();
        lastTextInputLifecycleEpoch = a_lifecycleEpoch;
        return static_cast<std::uint32_t>(result);
    } catch (...) {
        SkyrimTogetherVR::GameplayAdapter::NoThrow::BestEffort([] {
            SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint::Get().Fault("exception during command pump");
        });
        return static_cast<std::uint32_t>(SkyrimTogetherVR::GameplayBridge::CommandPumpResult::Faulted);
    }
}
