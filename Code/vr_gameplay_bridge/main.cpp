#include "CommandExecutor.h"
#include "DialogueHooks.h"
#include "EventCapture.h"
#include "MagicHooks.h"
#include "PapyrusBindings.h"
#include "ProjectileHooks.h"

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
} // namespace

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
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

        if (!SkyrimTogetherVR::GameplayAdapter::RegisterPapyrusBindings()) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: Papyrus registration failed");
            return false;
        }

        auto& endpoint = SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint::Get();
        if (!endpoint.Attach())
            return false;

        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(SkyrimTogetherVR::GameplayAdapter::HandleSkseMessage)) {
            endpoint.Fault("SKSE messaging listener registration failed");
            return false;
        }
        if (!SkyrimTogetherVR::GameplayAdapter::DialogueHooks::Install()) {
            endpoint.Fault("exact Actor::SpeakSound hook installation failed");
            return false;
        }
        if (!SkyrimTogetherVR::GameplayAdapter::ProjectileHooks::Install()) {
            SkyrimTogetherVR::GameplayAdapter::DialogueHooks::Uninstall();
            endpoint.Fault("exact Projectile::Launch hook installation failed");
            return false;
        }
        if (!SkyrimTogetherVR::GameplayAdapter::MagicHooks::Install()) {
            SkyrimTogetherVR::GameplayAdapter::ProjectileHooks::Uninstall();
            SkyrimTogetherVR::GameplayAdapter::DialogueHooks::Uninstall();
            endpoint.Fault("exact magic hook installation failed");
            return false;
        }
        endpoint.SetOptionalCapability(
            SkyrimTogetherVR::GameplayBridge::Capability::ExactAnimationActions,
            false);
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: exact ActorMediator action lane is disabled pending GameId translation and verified TESActionData lifetime");

        SkyrimTogetherVR::GameplayAdapter::PublishPluginLoaded();
        SKSE::log::info("SkyrimTogetherVRGameplayBridge loaded");
        return true;
    } catch (...) {
        SkyrimTogetherVR::GameplayAdapter::MagicHooks::Uninstall();
        SkyrimTogetherVR::GameplayAdapter::ProjectileHooks::Uninstall();
        SkyrimTogetherVR::GameplayAdapter::DialogueHooks::Uninstall();
        SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint::Get().Fault("exception during SKSEPluginLoad");
        return false;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl STVRGameplayBridge_ProcessCommands(
    const std::uint32_t a_callerProcessId,
    const std::uint32_t a_callerThreadId,
    const std::uint64_t a_lifecycleEpoch,
    const std::uint32_t a_maxCommands) noexcept
{
    try {
        return static_cast<std::uint32_t>(SkyrimTogetherVR::GameplayAdapter::ProcessCommands(
            a_callerProcessId,
            a_callerThreadId,
            a_lifecycleEpoch,
            a_maxCommands));
    } catch (...) {
        SkyrimTogetherVR::GameplayAdapter::BridgeEndpoint::Get().Fault("exception during command pump");
        return static_cast<std::uint32_t>(SkyrimTogetherVR::GameplayBridge::CommandPumpResult::Faulted);
    }
}
