
#include <TiltedReverse/Code/reverse/include/Debug.hpp>
#include "TargetConfig.h"
#include "launcher.h"

#include "loader/ExeLoader.h"
#include "loader/PathRerouting.h"

#include "Utils/Error.h"
#include "Utils/FileVersion.inl"

#include "oobe/PathSelection.h"
#include "oobe/PathArgument.h"
#include "oobe/SupportChecks.h"
#include "steam/SteamLoader.h"

#include "base/dialogues/win/TaskDialog.h"
#include "utils/Registry.h"

#include <BranchInfo.h>
#include <Shellapi.h>
#include <client/ScriptExtender.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

// These symbols are defined within the client code skyrimtogetherclient
extern void InstallStartHook();
extern void RunTiltedApp();
extern void RunTiltedInit(const std::filesystem::path& acGamePath, const TiltedPhoques::String& aExeVersion);

// Defined in EarlyLoad.dll
bool __declspec(dllimport) EarlyInstallSucceeded();

HICON g_SharedWindowIcon = nullptr;

namespace launcher
{
static LaunchContext* g_context = nullptr;

#if TP_SKYRIM_VR
static bool g_launchCompanionPanel = false;
static bool g_companionPanelOnly = false;
static bool g_disableCompanionPanel = false;

namespace
{
constexpr auto kScriptExtenderBootstrapTimeout = std::chrono::seconds(60);

bool BootstrapScriptExtenderOnLoaderThread()
{
    auto completion = std::make_shared<std::promise<ScriptExtenderLoadResult>>();
    auto result = completion->get_future();

    std::thread bootstrapThread(
        [completion]()
        {
            ScriptExtenderLoadResult loadResult = ScriptExtenderLoadResult::kModuleLoadFailed;
            spdlog::info("SkyrimTogetherVR SKSEVR bootstrap helper started (thread {})", GetCurrentThreadId());

            try
            {
                if (!ExeLoader::ApplyMappedTlsToCurrentThread())
                {
                    spdlog::error(
                        "SkyrimTogetherVR SKSEVR bootstrap helper could not apply mapped Skyrim VR TLS "
                        "(template {} bytes, slot capacity {} bytes)",
                        ExeLoader::GetMappedTlsTemplateSize(), ExeLoader::GetMappedTlsSlotCapacity());
                }
                else
                {
                    spdlog::info("SkyrimTogetherVR SKSEVR bootstrap helper applied mapped Skyrim VR TLS (thread {})", GetCurrentThreadId());
                    spdlog::info("SkyrimTogetherVR SKSEVR bootstrap helper entering LoadScriptExender");
                    loadResult = LoadScriptExender();
                    spdlog::info("SkyrimTogetherVR SKSEVR bootstrap helper returned from LoadScriptExender (result {})", static_cast<unsigned int>(loadResult));
                }
            }
            catch (const std::exception& e)
            {
                spdlog::error("SkyrimTogetherVR SKSEVR bootstrap helper threw: {}", e.what());
            }
            catch (...)
            {
                spdlog::error("SkyrimTogetherVR SKSEVR bootstrap helper threw an unknown exception");
            }

            completion->set_value(loadResult);
        });

    if (result.wait_for(kScriptExtenderBootstrapTimeout) != std::future_status::ready)
    {
        spdlog::critical("SkyrimTogetherVR SKSEVR bootstrap helper timed out after {} seconds", kScriptExtenderBootstrapTimeout.count());
        bootstrapThread.detach();
        spdlog::default_logger_raw()->flush();
        TerminateProcess(GetCurrentProcess(), 5);
        return false;
    }

    const auto loadResult = result.get();
    bootstrapThread.join();
    if (loadResult != ScriptExtenderLoadResult::kModuleLoaded)
    {
        spdlog::error("SkyrimTogetherVR SKSEVR bootstrap helper failed before mapped game entry (result {})", static_cast<unsigned int>(loadResult));
        return false;
    }

    spdlog::info("SkyrimTogetherVR SKSEVR bootstrap helper completed before mapped game entry");
    return true;
}
} // namespace

bool EnvRequestsCompanionPanel()
{
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(L"STVR_LAUNCH_COMPANION", value, _countof(value));
    if (length == 0 || length >= _countof(value))
        return false;

    return value[0] != L'\0' && value[0] != L'0' && value[0] != L'f' && value[0] != L'F' && value[0] != L'n' &&
           value[0] != L'N' && value[0] != L'o' && value[0] != L'O';
}

bool LaunchCompanionPanel(const LaunchContext& acContext)
{
    const std::filesystem::path candidates[]{
        TiltedPhoques::GetPath() / L"LaunchSkyrimTogetherVRCompanion.bat",
        acContext.gamePath / L"LaunchSkyrimTogetherVRCompanion.bat",
    };

    std::error_code ec;
    for (const auto& candidate : candidates)
    {
        if (!std::filesystem::exists(candidate, ec))
            continue;

        const auto parameters = L"--game-path \"" + acContext.gamePath.wstring() + L"\"";
        const auto result = ShellExecuteW(
            nullptr,
            L"open",
            candidate.c_str(),
            parameters.c_str(),
            candidate.parent_path().c_str(),
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) > 32)
            return true;
    }

    OutputDebugStringW(L"SkyrimTogetherVR companion panel launcher was not found or could not be started.\n");
    return false;
}
#endif

LaunchContext* GetLaunchContext()
{
#if 0
    if (!g_context)
        __debugbreak();
#endif
    return g_context;
}

bool LaunchContext::GetLoaded()
{
    return isLoaded;
}

// Everything is nothing, life is worth living, just look to the stars
#define DIE_NOW(err)  \
    {                 \
        Die(err);     \
        return false; \
    }

void SetMaxstdio()
{
    const auto handle = GetModuleHandleW(L"API-MS-WIN-CRT-STDIO-L1-1-0.DLL");
    if (!handle)
        return;

    const auto setmaxstdioFunc = reinterpret_cast<decltype(&_setmaxstdio)>(GetProcAddress(handle, "_setmaxstdio"));

    if (!setmaxstdioFunc)
        return;

    setmaxstdioFunc(8192);
}

int StartUp(int argc, char** argv)
{
    bool askSelect = (GetAsyncKeyState(VK_SPACE) & 0x8000);
    if (!HandleArguments(argc, argv, askSelect))
        return -1;

    // TODO(Force): Make some InitSharedResources func.
    g_SharedWindowIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102));

#if (!IS_MASTER)
    TiltedPhoques::Debug::CreateConsole();
#endif

    SetMaxstdio();

    if (!EarlyInstallSucceeded())
        DIE_NOW(L"Early load install failed. Tell Force about this.");

    auto LC = std::make_unique<LaunchContext>();
    g_context = LC.get();

    {
        const wchar_t* ec = nullptr;
        const auto status = oobe::ReportModCompatabilityStatus();
        switch (status)
        {
        case oobe::CompatabilityStatus::kDX11Unsupported: ec = L"Device does not support DirectX 11"; break;
        case oobe::CompatabilityStatus::kOldOS: ec = L"Operating system unsupported. Please upgrade to Windows 8.1 or greater"; break;
        }

        if (ec)
            DIE_NOW(ec);
    }

    if (!oobe::SelectInstall(askSelect))
        DIE_NOW(L"Failed to select game install.");

#if TP_SKYRIM_VR
    const bool shouldLaunchCompanion =
        !g_disableCompanionPanel && (g_launchCompanionPanel || EnvRequestsCompanionPanel());
    if (shouldLaunchCompanion)
    {
        if (!LaunchCompanionPanel(*LC) && g_companionPanelOnly)
        {
            Die(L"Failed to launch the SkyrimTogetherVR companion panel.");
            return 4;
        }

        if (g_companionPanelOnly)
            return 0;
    }
#endif

    // Bind path environment.
    loader::InstallPathRouting(LC->gamePath);
    steam::Load(LC->gamePath);

    if (!LoadProgram(*LC))
        return 3;

    InstallStartHook();
    // Initialize all hooks before calling game init
    // TiltedPhoques::Initializer::RunAll();
    RunTiltedInit(LC->gamePath, LC->Version);

#if TP_SKYRIM_VR
    // SKSEVR's official loader uses a separate injection thread before the
    // target thread begins. Recreate that thread topology for the mapped game.
    if (!BootstrapScriptExtenderOnLoaderThread())
    {
        SetLastError(ERROR_DLL_INIT_FAILED);
        Die(L"SkyrimTogetherVR could not initialize SKSEVR before starting Skyrim VR.", true);
        return 4;
    }
#endif

    // This shouldn't return until the game is killed
    spdlog::info("SkyrimTogetherVR entering mapped Skyrim VR gameMain");
    LC->gameMain();
    return 0;
}

bool LoadProgram(LaunchContext& LC)
{
    auto content = TiltedPhoques::LoadFile(LC.exePath);
    if (content.empty())
        DIE_NOW(L"Failed to mount game executable");

    LC.Version = QueryFileVersion(LC.exePath.c_str());
    if (LC.Version.empty())
        DIE_NOW(L"Failed to query game version");
    LC.SetLoaded();

    ExeLoader loader(CurrentTarget.exeLoadSz);
    if (!loader.Load(reinterpret_cast<uint8_t*>(content.data())))
        DIE_NOW(L"Fatal error while mapping executable");

    LC.gameMain = loader.GetEntryPoint();
    return true;
}

void InitClient()
{
    // Jump into client code.
    RunTiltedApp();
}

bool HandleArguments(int aArgc, char** aArgv, bool& aAskSelect)
{
    for (int i = 1; i < aArgc; i++)
    {
        if (std::strcmp(aArgv[i], "-r") == 0)
            aAskSelect = true;
#if TP_SKYRIM_VR
        else if (std::strcmp(aArgv[i], "--companion") == 0 || std::strcmp(aArgv[i], "--vr-companion") == 0)
            g_launchCompanionPanel = true;
        else if (std::strcmp(aArgv[i], "--companion-only") == 0)
        {
            g_launchCompanionPanel = true;
            g_companionPanelOnly = true;
        }
        else if (std::strcmp(aArgv[i], "--no-companion") == 0)
            g_disableCompanionPanel = true;
#endif
        else if (std::strcmp(aArgv[i], "--exePath") == 0)
        {
            if (i + 1 >= aArgc)
            {
                SetLastError(ERROR_BAD_PATHNAME);
                Die(L"No exe path specified", true);
                return false;
            }

            if (!oobe::PathArgument(aArgv[i + 1]))
            {
                SetLastError(ERROR_BAD_ARGUMENTS);
                Die(L"Failed to parse path argument", true);
                return false;
            }
        }
    }

    return true;
}
} // namespace launcher

// CreateProcess in suspended mode.
// Inject usvfs_64.dll -> invoke InitHooks
// (https://github.com/ModOrganizer2/usvfs/blob/f8051c179dee114b7e06c5dab2482977c285d611/src/usvfs_dll/usvfs.cpp#L352)
// Resume proc

// InjectDLLRemoteThread ->SkipInit
