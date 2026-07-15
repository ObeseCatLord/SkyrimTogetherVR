
#include <TiltedOnlineApp.h>
#include <TiltedOnlinePCH.h>
#include <VRCompatibilityStatus.h>

#include "VRTickBridge.h"

#include <Commctrl.h>
#include <Windows.h>

#include <base/dialogues/win/TaskDialog.h>

#include "immersive_launcher/stubs/DllBlocklist.h"

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
#define TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS
#define TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES
#define TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES 0
#endif

std::unique_ptr<TiltedOnlineApp> g_appInstance{nullptr};
static std::atomic_bool s_appStartAttempted{false};
static std::atomic_bool s_appStarted{false};
static std::atomic_bool s_appEndAttempted{false};

void RunTiltedApp();
void RunTiltedEnd() noexcept;

#if TP_SKYRIM_VR
using TVrWinMain = int(__stdcall*)(HINSTANCE, HINSTANCE, LPSTR, int);
static TVrWinMain s_vrWinMain = nullptr;

static int __stdcall HookVrWinMain(HINSTANCE aInstance, HINSTANCE aPreviousInstance, LPSTR apCommandLine, int aShowCommand)
{
    struct ShutdownGuard
    {
        ~ShutdownGuard() { RunTiltedEnd(); }
    } shutdownGuard;

    return s_vrWinMain(aInstance, aPreviousInstance, apCommandLine, aShowCommand);
}

static bool InstallVrWinMainLifecycleHook() noexcept
{
    s_vrWinMain = reinterpret_cast<TVrWinMain>(g_appInstance->GetMainAddress());
    if (!s_vrWinMain)
    {
        spdlog::critical("SkyrimTogetherVR could not resolve the required VR WinMain lifecycle target");
        return false;
    }

    spdlog::info("Installing SkyrimTogetherVR WinMain lifecycle hook: winMain={}", fmt::ptr(s_vrWinMain));
    TP_HOOK(&s_vrWinMain, HookVrWinMain);
    return true;
}
#endif

extern HICON g_SharedWindowIcon;
extern void InstallVrMainLoopBringupHooks();

#if TP_SKYRIM_VR
static void InstallVrBringupHooks()
{
#if TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS
    spdlog::info("Installing SkyrimTogetherVR deferred startup/update-owner hook");
    InstallVrMainLoopBringupHooks();
#else
    spdlog::warn("SkyrimTogetherVR bring-up hooks are disabled at compile time");
#endif
}
#endif

static void ShowAddressLibraryError(const wchar_t* apGamePath)
{
#if TP_SKYRIM_VR
    auto errorDetail = fmt::format(
        L"Looking for VR Address Library and SkyrimTogetherVR helper CSVs here: {}\\Data\\SKSE\\Plugins",
        apGamePath);

    Base::TaskDialog dia(
        g_SharedWindowIcon,
        L"Error",
        L"Failed to load Skyrim VR Address Library",
        L"Make sure version-1-4-15-0.csv is installed, along with SkyrimTogetherVR address helper CSVs.",
        errorDetail.c_str());

    dia.AppendButton(0xBEEF, L"Visit VR Address Library releases");
    const int result = dia.Show();
    if (result == 0xBEEF)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://github.com/alandtse/skyrim_vr_address_library/releases)", nullptr, nullptr, SW_SHOWNORMAL);
    }
#else
    auto errorDetail = fmt::format(L"Looking for it here: {}\\Data\\SKSE\\Plugins", apGamePath);

    Base::TaskDialog dia(g_SharedWindowIcon, L"Error", L"Failed to load Skyrim Address Library", L"Make sure to use \"All in one (1.6.X)\"", errorDetail.c_str());

    dia.AppendButton(0xBEED, L"Visit troubleshooting page on wiki.tiltedphoques.com");
    dia.AppendButton(0xBEEF, L"Visit Address Library modpage on nexusmods.com");
    const int result = dia.Show();
    if (result == 0xBEEF)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (result == 0xBEED)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://wiki.tiltedphoques.com/tilted-online/guides/troubleshooting/address-library-error)", nullptr, nullptr, SW_SHOWNORMAL);
    }
#endif

    exit(4);
}

bool RunTiltedInit(const std::filesystem::path& acGamePath, const String& aExeVersion)
{
    if (!VersionDb::Get().Load(acGamePath, aExeVersion))
    {
        ShowAddressLibraryError(acGamePath.c_str());
    }

    // VersionDb::Get().DumpToTextFile(R"(S:\Work\Tilted\fallout\_addresslib.txt)");

    g_appInstance = std::make_unique<TiltedOnlineApp>();

    spdlog::info("Loaded Skyrim address data for runtime {} with {} entries", VersionDb::Get().GetLoadedVersionString(), VersionDb::Get().GetOffsetMap().size());

#if TP_SKYRIM_VR
    spdlog::info(
        "SkyrimTogetherVR runtime flags: connectionOnly={}, bringupHooks={}, unvalidatedHooks={}, validatedInlinePatches={}", TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY,
        TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS, TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS, TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES);
#endif

#if TP_SKYRIM_VR
    if (!InstallVrWinMainLifecycleHook())
        return false;

    if (!SkyrimTogetherVR::TickBridge::Initialize())
    {
        spdlog::critical("SkyrimTogetherVR could not initialize the required SKSEVR task endpoint");
        return false;
    }

    const auto vrCompatibilityStatus = BuildVRCompatibilityStatus(acGamePath, stubs::g_IsHiggsActive, stubs::g_IsPlanckActive);
    WriteVRCompatibilityStatusFile(acGamePath, vrCompatibilityStatus);

    if (vrCompatibilityStatus.HiggsInstalled)
        spdlog::info("HIGGS detected; keeping SkyrimTogetherVR in HIGGS-compatible hook mode");

    if (vrCompatibilityStatus.PlanckInstalled)
        spdlog::info("PLANCK detected; keeping SkyrimTogetherVR in PLANCK/HIGGS-compatible hook mode");

#if TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
    if (vrCompatibilityStatus.VRPhysicsCompatibilityModInstalled)
    {
        spdlog::warn("HIGGS or PLANCK is installed; refusing to install unvalidated SkyrimTogetherVR gameplay hooks");
        InstallVrBringupHooks();
    }
    else
    {
        TiltedOnlineApp::InstallHooks2();
    }
#else
    spdlog::warn("SkyrimTogetherVR bring-up mode: skipping unvalidated Skyrim gameplay hooks");
    InstallVrBringupHooks();
#endif
#else
    TiltedOnlineApp::InstallHooks2();
#endif

    const auto hookSummary = TiltedPhoques::FunctionHookManager::GetInstance().InstallDelayedHooks();
#if TP_SKYRIM_VR
    if (hookSummary.Failures != 0)
    {
        spdlog::critical(
            "SkyrimTogetherVR refused to enter Skyrim VR after {} MinHook failure(s): delayed {}/{}, immediate {}/{}.", hookSummary.Failures, hookSummary.DelayedInstalled,
            hookSummary.DelayedAttempted, hookSummary.ImmediateInstalled, hookSummary.ImmediateAttempted);
        return false;
    }

    spdlog::info(
        "SkyrimTogetherVR pre-entry hook install verified: delayed {}/{}, immediate {}/{}.", hookSummary.DelayedInstalled, hookSummary.DelayedAttempted, hookSummary.ImmediateInstalled,
        hookSummary.ImmediateAttempted);
#endif
    return true;
}

void RunTiltedApp()
{
    if (!g_appInstance)
        return;
    if (s_appStartAttempted.exchange(true, std::memory_order_acq_rel))
        return;

#if TP_SKYRIM_VR
    spdlog::info("SkyrimTogetherVR client startup hook reached");
#endif

    bool started = false;
    try
    {
        started = g_appInstance->BeginMain();
    }
    catch (const std::exception& error)
    {
        spdlog::critical("SkyrimTogetherVR deferred client startup threw: {}", error.what());
    }
    catch (...)
    {
        spdlog::critical("SkyrimTogetherVR deferred client startup threw an unknown exception");
    }

    s_appStarted.store(started, std::memory_order_release);
    if (!started)
    {
#if TP_SKYRIM_VR
        SkyrimTogetherVR::TickBridge::Retire();
#endif
        spdlog::critical("SkyrimTogetherVR client startup failed after the first verified game callback; continuing without the client");
    }
}

void RunTiltedEnd() noexcept
{
    if (s_appEndAttempted.exchange(true, std::memory_order_acq_rel))
        return;

#if TP_SKYRIM_VR
    const auto currentThreadId = GetCurrentThreadId();
    const auto ownerThreadId = SkyrimTogetherVR::TickBridge::GetActivationThreadId();
    SkyrimTogetherVR::TickBridge::Retire();
    if (ownerThreadId != 0 && ownerThreadId != currentThreadId)
        spdlog::critical("SkyrimTogetherVR WinMain lifecycle shutdown ran on thread {}; owner is {}", currentThreadId, ownerThreadId);
    spdlog::info("SkyrimTogetherVR WinMain lifecycle shutdown hook reached on thread {}", currentThreadId);
#endif

    if (!s_appStarted.load(std::memory_order_acquire))
        return;

    try
    {
        if (g_appInstance)
            g_appInstance->EndMain();
    }
    catch (const std::exception& error)
    {
        spdlog::critical("SkyrimTogetherVR client teardown threw: {}", error.what());
    }
    catch (...)
    {
        spdlog::critical("SkyrimTogetherVR client teardown threw an unknown exception");
    }
}
