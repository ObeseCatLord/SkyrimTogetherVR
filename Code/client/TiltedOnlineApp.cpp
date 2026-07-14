#include <TiltedOnlinePCH.h>

#include <TiltedOnlineApp.h>

#include "VRTickBridge.h"

#include <DInputHook.hpp>
#include <dinput.h>
#include <WindowsHook.hpp>

#include <World.h>
#include <VRCompatibilityStatus.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Systems/RenderSystemD3D11.h>

#include <Services/OverlayService.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>
#include <Services/VRLifecycleService.h>

#include <ScriptExtender.h>
#include <NvidiaUtil.h>

#include "immersive_launcher/stubs/DllBlocklist.h"

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY
#define TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY 0
#endif

using TiltedPhoques::Debug;

TiltedOnlineApp::TiltedOnlineApp()
{
    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    auto logPath = TiltedPhoques::GetPath() / "logs";

    std::error_code ec;
    create_directory(logPath, ec);

    auto rotatingLogger = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath / "tp_client.log", 1048576 * 5, 3);
    // rotatingLogger->set_level(spdlog::level::debug);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console, rotatingLogger});
    logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] %$ %v");
    spdlog::flush_every(std::chrono::seconds(1));
    set_default_logger(logger);
}

TiltedOnlineApp::~TiltedOnlineApp() = default;

void* TiltedOnlineApp::GetMainAddress() const
{
    POINTER_SKYRIMSE(void, winMain, 36544);

    return winMain.GetPtr();
}

bool TiltedOnlineApp::BeginMain()
{
    World::Create();
    World::Get().ctx().at<DiscordService>().Init();

#if TP_SKYRIM_VR && (TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY || !TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY)
    spdlog::warn("SkyrimTogetherVR flat D3D overlay/render startup is disabled for this VR target");
#else
    World::Get().ctx().emplace<RenderSystemD3D11>(World::Get().ctx().at<OverlayService>(), World::Get().ctx().at<ImguiService>());
#endif

    if (!WasScriptExtenderLoadAttempted())
    {
        spdlog::error("SkyrimTogetherVR pre-entry SKSEVR bootstrap was not attempted; the connection handshake will report SKSE inactive.");
    }
    else if (GetScriptExtenderLoadResult() != ScriptExtenderLoadResult::kModuleLoaded)
    {
        spdlog::error(
            "SkyrimTogetherVR could not load the required SKSEVR Steam bootstrap shim (result {}). "
            "The connection handshake will report SKSE inactive.",
            static_cast<unsigned int>(GetScriptExtenderLoadResult()));
    }
    else if (!IsScriptExtenderLoaded())
    {
        spdlog::warn("SkyrimTogetherVR SKSEVR Steam bootstrap shim is installed, but the game-startup core module is not visible yet.");
    }
    else
    {
        spdlog::info("SkyrimTogetherVR SKSEVR core module loaded through the Steam startup shim.");
    }

#if TP_SKYRIM_VR
    WriteVRCompatibilityStatusFile(
        TiltedPhoques::GetPath(),
        BuildVRCompatibilityStatus(TiltedPhoques::GetPath(), stubs::g_IsHiggsActive, stubs::g_IsPlanckActive));

    if (stubs::g_IsHiggsActive)
        spdlog::info("HIGGS SKSE plugin is loaded; hand physics compatibility guard is active");
    if (stubs::g_IsPlanckActive)
        spdlog::info("PLANCK SKSE plugin is loaded; active-ragdoll compatibility guard is active");
#endif

    // TODO: Figure out a way to un-blacklist NvCamera64.dll (see DllBlocklist.cpp). Then this hack can be removed
#if !(TP_SKYRIM_VR && (TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY || !TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY))
    if (IsNvidiaOverlayLoaded())
        ApplyNvidiaFix();
#endif

#if TP_SKYRIM_VR
    SkyrimTogetherVR::TickBridge::Activate();
#endif

    return true;
}

bool TiltedOnlineApp::EndMain()
{
#if TP_SKYRIM_VR
    World::Get().ctx().at<VRLifecycleService>().BeginTeardown();
    SkyrimTogetherVR::TickBridge::Retire();
#endif

    UninstallHooks();
    if (m_pDevice)
        m_pDevice->Release();

    return true;
}

void TiltedOnlineApp::Update()
{
#if TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
    World::Get().Update();
    return;
#endif

    // Reverting a change that used to be here to disable bUseFaceGenPreprocessedHeads==true (which is 
    // the default) handling. Extensive testing over months by multiple parties showed that enabling 
    // the flag introduces no issues WITH PROPERLY GENERATED CHARACTERS (in-game character generation 
    // or showracemenu). The shortcut of  "coc riverwood" from the main menu skips proper character generation.
    // 
    // Plus, having it on  has some benefits like helping with neck seams. Comment to avoid revisiting.
    // 
    // There are still some issues to track down, like hair color and maybe face tint not syncing correctly,
    // but they are unrelated and unchanged by this flag.
    // 
 
#if !TP_SKYRIM_VR
    // Make sure the window stays active
    POINTER_SKYRIMSE(uint32_t, bAlwaysActive, 380768);

    *bAlwaysActive = 1;
#endif

    World::Get().Update();
}

bool TiltedOnlineApp::Attach()
{
    TiltedPhoques::Debug::OnAttach();

    // TiltedPhoques::Nop(0x1405D3FA1, 6);
    return true;
}

bool TiltedOnlineApp::Detach()
{
    TiltedPhoques::Debug::OnDetach();
    return true;
}

void TiltedOnlineApp::InstallHooks2()
{
    TiltedPhoques::Initializer::RunAll();

#if TP_SKYRIM_VR && (TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY || !TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY)
    spdlog::warn("SkyrimTogetherVR DirectInput overlay hooks are disabled for this VR target");
#else
    TiltedPhoques::DInputHook::Install();
    TiltedPhoques::DInputHook::Get().SetToggleKeys({DIK_F2, DIK_RCONTROL});
#endif
}

void TiltedOnlineApp::UninstallHooks()
{
}

void TiltedOnlineApp::ApplyNvidiaFix() noexcept
{
    auto d3dFeatureLevelOut = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = CreateEarlyDxDevice(&m_pDevice, &d3dFeatureLevelOut);
    if (FAILED(hr))
        spdlog::error("D3D11CreateDevice failed. Detected an NVIDIA GPU, error code={0:x}", hr);

    if (d3dFeatureLevelOut < D3D_FEATURE_LEVEL_11_0)
        spdlog::warn("Unexpected D3D11 feature level detected (< 11.0), may cause issues");
}
