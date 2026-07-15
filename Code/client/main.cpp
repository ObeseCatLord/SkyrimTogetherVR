
#include <TiltedOnlineApp.h>
#include <TiltedOnlinePCH.h>
#include <VRCompatibilityStatus.h>

#include "VRTickBridge.h"

#include <Commctrl.h>
#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

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
    const auto& error = VersionDb::Get().GetLastError();
    const std::wstring wideError(error.begin(), error.end());
    auto errorDetail = fmt::format(
        L"Looking for VR Address Library and SkyrimTogetherVR helper CSVs here: {}\\Data\\SKSE\\Plugins\n\nReason: {}",
        apGamePath, wideError.empty() ? L"unspecified address-loader failure" : wideError.c_str());

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

#if TP_SKYRIM_VR
namespace
{
struct VrHookContract
{
    unsigned long long Id;
    unsigned long long Rva;
    const uint8_t* ExpectedBytes;
    size_t ExpectedSize;
    const char* Name;
};

bool IsExecutableProtection(DWORD aProtection) noexcept
{
    if ((aProtection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const auto baseProtection = aProtection & 0xFF;
    return baseProtection == PAGE_EXECUTE_READ || baseProtection == PAGE_EXECUTE_READWRITE || baseProtection == PAGE_EXECUTE_WRITECOPY;
}

bool ValidateVrHookContract(const VrHookContract& acContract) noexcept
{
    unsigned long long resolvedRva = 0;
    if (!VersionDb::Get().FindOffsetById(acContract.Id, resolvedRva) || resolvedRva != acContract.Rva)
    {
        spdlog::critical(
            "SkyrimTogetherVR rejected {} address ID {}: expected RVA 0x{:X}, resolved RVA 0x{:X}", acContract.Name, acContract.Id, acContract.Rva, resolvedRva);
        return false;
    }

    const auto* pTarget = static_cast<const uint8_t*>(VersionDb::Get().FindAddressById(acContract.Id));
    MEMORY_BASIC_INFORMATION memory{};
    if (!pTarget || VirtualQuery(pTarget, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT || !IsExecutableProtection(memory.Protect))
    {
        spdlog::critical("SkyrimTogetherVR rejected {} address ID {}: target is not committed executable memory", acContract.Name, acContract.Id);
        return false;
    }

    const auto regionEnd = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    const auto targetEnd = reinterpret_cast<uintptr_t>(pTarget) + acContract.ExpectedSize;
    if (targetEnd < reinterpret_cast<uintptr_t>(pTarget) || targetEnd > regionEnd || std::memcmp(pTarget, acContract.ExpectedBytes, acContract.ExpectedSize) != 0)
    {
        spdlog::critical("SkyrimTogetherVR rejected {} address ID {}: Skyrim VR 1.4.15 prologue mismatch", acContract.Name, acContract.Id);
        return false;
    }

    spdlog::info("Validated Skyrim VR hook target {}: id={} rva=0x{:X}", acContract.Name, acContract.Id, acContract.Rva);
    return true;
}

bool ValidateVrDefaultHookContracts() noexcept
{
    static constexpr std::array<uint8_t, 16> s_winMainBytes{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x0D, 0x1D, 0x75, 0xA3, 0x02, 0x4C, 0x89, 0x05, 0x1E, 0x75};
    static constexpr std::array<uint8_t, 16> s_mainDrawBytes{
        0x48, 0x8B, 0xC4, 0x89, 0x50, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41};

    const VrHookContract contracts[]{
        {35545, 0x5B4290, s_winMainBytes.data(), s_winMainBytes.size(), "WinMain"},
        {35560, 0x5B9330, s_mainDrawBytes.data(), s_mainDrawBytes.size(), "Main::Draw"},
    };

    for (const auto& contract : contracts)
    {
        if (!ValidateVrHookContract(contract))
            return false;
    }
    return true;
}
} // namespace
#endif

bool RunTiltedInit(const std::filesystem::path& acGamePath, int aMajor, int aMinor, int aRevision, int aBuild)
{
    try
    {
        g_appInstance = std::make_unique<TiltedOnlineApp>();
    }
    catch (...)
    {
        OutputDebugStringA("SkyrimTogetherVR: failed to initialize bootstrap logging.\n");
        return false;
    }

    spdlog::info("Loading Skyrim address data for runtime {}.{}.{}.{} from {}", aMajor, aMinor, aRevision, aBuild, acGamePath.string());
    if (!VersionDb::Get().Load(acGamePath, aMajor, aMinor, aRevision, aBuild))
    {
        spdlog::critical("SkyrimTogetherVR address loading failed: {}", VersionDb::Get().GetLastError());
        ShowAddressLibraryError(acGamePath.c_str());
    }

    // VersionDb::Get().DumpToTextFile(R"(S:\Work\Tilted\fallout\_addresslib.txt)");

    spdlog::info("Loaded Skyrim address data for runtime {} with {} entries", VersionDb::Get().GetLoadedVersionString(), VersionDb::Get().GetOffsetMap().size());

#if TP_SKYRIM_VR
    spdlog::info(
        "SkyrimTogetherVR runtime flags: connectionOnly={}, bringupHooks={}, unvalidatedHooks={}, validatedInlinePatches={}", TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY,
        TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS, TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS, TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES);
#endif

#if TP_SKYRIM_VR
    if (!ValidateVrDefaultHookContracts())
        return false;

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
