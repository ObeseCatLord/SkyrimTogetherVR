#include "CommandExecutor.h"
#include "EventCapture.h"

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

        SKSE::Init(a_skse);
        InitializeLogging();
        if (a_skse->RuntimeVersion() != SKSE::RUNTIME_VR_1_4_15) {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: unsupported runtime");
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

        SkyrimTogetherVR::GameplayAdapter::PublishPluginLoaded();
        SKSE::log::info("SkyrimTogetherVRGameplayBridge loaded");
        return true;
    } catch (...) {
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
