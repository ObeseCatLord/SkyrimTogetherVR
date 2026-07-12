
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

extern HICON g_SharedWindowIcon;
extern void InstallVrMainLoopBringupHooks();
namespace BSGraphics
{
void InstallVrRenderBringupHooks();
}

#if TP_SKYRIM_VR
static void InstallVrBringupHooks()
{
#if TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS
    spdlog::info("Installing SkyrimTogetherVR startup/main-loop/render bring-up hooks");
    InstallVrMainLoopBringupHooks();
    BSGraphics::InstallVrRenderBringupHooks();
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

    spdlog::info("Loaded Skyrim address data for runtime {} with {} entries", VersionDb::Get().GetLoadedVersionString(), VersionDb::Get().GetOffsetMap().size());

#if TP_SKYRIM_VR
    spdlog::info(
        "SkyrimTogetherVR runtime flags: connectionOnly={}, bringupHooks={}, unvalidatedHooks={}, validatedInlinePatches={}", TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY,
        TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS, TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS, TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES);
#endif

    // VersionDb::Get().DumpToTextFile(R"(S:\Work\Tilted\fallout\_addresslib.txt)");

    g_appInstance = std::make_unique<TiltedOnlineApp>();

#if TP_SKYRIM_VR
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
#if TP_SKYRIM_VR
    spdlog::info("SkyrimTogetherVR client startup hook reached");
#endif

    g_appInstance->BeginMain();
}
