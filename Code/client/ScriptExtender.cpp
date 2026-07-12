
#include <ScriptExtender.h>
#include <TiltedOnlinePCH.h>
#include <VersionDb.h>

#include <array>
#include <chrono>
#include <thread>

namespace
{
#if !defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1
#error "SkyrimTogetherVR client must be built with TP_SKYRIM_VR=1"
#endif

constexpr wchar_t kScriptExtenderName[] = L"sksevr";
constexpr wchar_t kScriptExtenderSteamLoaderName[] = L"sksevr_steam_loader.dll";
// SKSEVR 2.0.12 is the runtime 1.4.15 release used by Skyrim VR.
constexpr const char* kUnsupportedScriptExtenderMessage = "SKSEVR 2.0.12 or newer is required";
constexpr wchar_t kGamePathEnvironmentVariable[] = L"STVR_GAME_PATH";

HMODULE g_SKSEModuleHandle{nullptr};
HMODULE g_SKSESteamLoaderModuleHandle{nullptr};
bool g_scriptExtenderLoadAttempted{false};
ScriptExtenderLoadResult g_scriptExtenderLoadResult{ScriptExtenderLoadResult::kModuleLoadFailed};

struct FileVersion
{
    static constexpr uint8_t scVersionSize = 4;
    DWORD versions[scVersionSize];
};

bool IsSkseVrVersionAtLeast2012(const FileVersion& acVersion) noexcept
{
    // SKSEVR 2.0.12 is stored in the PE resource as 0.2.0.12.
    constexpr DWORD kMinimumVersion[FileVersion::scVersionSize]{0, 2, 0, 12};
    for (uint8_t i = 0; i < FileVersion::scVersionSize; ++i)
    {
        if (acVersion.versions[i] != kMinimumVersion[i])
            return acVersion.versions[i] > kMinimumVersion[i];
    }

    return true;
}

int GetFileVersion(const std::filesystem::path& acFilePath, FileVersion& aVersion)
{
    const auto filename = acFilePath.c_str();

    DWORD dwHandle = 0, sz = GetFileVersionInfoSizeW(filename, &dwHandle);
    if (0 == sz)
    {
        return 1;
    }
    std::string buf(sz, '\0');
    if (!GetFileVersionInfoW(filename, dwHandle, sz, &buf[0]))
    {
        return 2;
    }
    VS_FIXEDFILEINFO* pvi;
    sz = sizeof(VS_FIXEDFILEINFO);
    if (!VerQueryValueA(&buf[0], "\\", reinterpret_cast<LPVOID*>(&pvi), reinterpret_cast<unsigned int*>(&sz)) || pvi->dwSignature != VS_FFI_SIGNATURE)
    {
        return 3;
    }

    aVersion.versions[0] = pvi->dwFileVersionMS >> 16;
    aVersion.versions[1] = pvi->dwFileVersionMS & 0xFFFF;
    aVersion.versions[2] = pvi->dwFileVersionLS >> 16;
    aVersion.versions[3] = pvi->dwFileVersionLS & 0xFFFF;

    return 0;
}

std::string GetSKSEStyleExeVersion()
{
    auto exeBuild = VersionDb::Get().GetLoadedVersionString();

    if (exeBuild.ends_with(".0"))
    {
        exeBuild.erase(exeBuild.size() - 2);
    }

    std::replace(exeBuild.begin(), exeBuild.end(), '.', '_');

    return exeBuild;
}

std::filesystem::path GetGameDirectory()
{
    std::array<wchar_t, 32768> gamePath{};
    const auto length = GetEnvironmentVariableW(
        kGamePathEnvironmentVariable, gamePath.data(), static_cast<DWORD>(gamePath.size()));
    if (length > 0 && length < gamePath.size())
        return std::filesystem::path(std::wstring(gamePath.data(), length));

    return std::filesystem::current_path();
}

std::filesystem::path GetScriptExtenderCorePath()
{
    const auto exeVersion = GetSKSEStyleExeVersion();
    const std::wstring version(exeVersion.begin(), exeVersion.end());
    return GetGameDirectory() / (std::wstring(kScriptExtenderName) + L"_" + version + L".dll");
}

std::filesystem::path GetScriptExtenderSteamLoaderPath()
{
    return GetGameDirectory() / kScriptExtenderSteamLoaderName;
}

bool IsModuleLoadedFromExpectedPath(const HMODULE aModuleHandle, const std::filesystem::path& aExpectedPath)
{
    std::array<wchar_t, 32768> modulePath{};
    const auto length = GetModuleFileNameW(aModuleHandle, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= static_cast<DWORD>(modulePath.size()))
        return false;

    std::error_code ec;
    const auto expectedPath = std::filesystem::absolute(aExpectedPath, ec).lexically_normal();
    if (ec)
        return false;

    const auto loadedPath = std::filesystem::absolute(std::filesystem::path(std::wstring(modulePath.data(), length)), ec).lexically_normal();
    if (ec)
        return false;

    return _wcsicmp(expectedPath.c_str(), loadedPath.c_str()) == 0;
}

bool RefreshScriptExtenderCoreModule()
{
    if (g_SKSEModuleHandle)
        return true;

    const auto corePath = GetScriptExtenderCorePath();
    const auto moduleHandle = GetModuleHandleW(corePath.filename().c_str());
    if (!moduleHandle || !IsModuleLoadedFromExpectedPath(moduleHandle, corePath))
        return false;

    g_SKSEModuleHandle = moduleHandle;
    spdlog::info("SKSEVR core module observed at expected game path: {}", corePath.string());
    return true;
}
} // namespace

bool IsScriptExtenderLoaded()
{
    return RefreshScriptExtenderCoreModule();
}

bool WaitForScriptExtenderLoaded(const std::chrono::milliseconds aTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    do
    {
        if (IsScriptExtenderLoaded())
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (std::chrono::steady_clock::now() < deadline);

    return IsScriptExtenderLoaded();
}

bool WasScriptExtenderLoadAttempted() noexcept
{
    return g_scriptExtenderLoadAttempted;
}

ScriptExtenderLoadResult GetScriptExtenderLoadResult() noexcept
{
    return g_scriptExtenderLoadResult;
}

ScriptExtenderLoadResult LoadScriptExender()
{
    if (g_scriptExtenderLoadAttempted)
        return g_scriptExtenderLoadResult;

    g_scriptExtenderLoadAttempted = true;

    const auto corePath = GetScriptExtenderCorePath();
    const auto coreModuleName = corePath.filename().wstring();
    const auto steamLoaderPath = GetScriptExtenderSteamLoaderPath();
    const auto steamLoaderModuleName = steamLoaderPath.filename().wstring();

    std::error_code ec;
    if (!std::filesystem::exists(corePath, ec))
    {
        spdlog::error("Required SKSEVR core module is missing: {}", corePath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleMissing;
    }

    FileVersion fileVersion{};
    if (GetFileVersion(corePath, fileVersion) != 0)
    {
        spdlog::error("Unable to verify the SKSEVR core version resource: {}", corePath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kVersionUnsupported;
    }

    if (!IsSkseVrVersionAtLeast2012(fileVersion))
    {
        spdlog::error(kUnsupportedScriptExtenderMessage);
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kVersionUnsupported;
    }

    // An already loaded matching core does not require the Steam shim again.
    if (RefreshScriptExtenderCoreModule())
    {
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoaded;
    }

    if (GetModuleHandleW(coreModuleName.c_str()))
    {
        spdlog::error("A same-named SKSEVR core module is already loaded outside the expected game directory: {}", corePath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoadFailed;
    }

    if (!std::filesystem::exists(steamLoaderPath, ec))
    {
        spdlog::error("Required SKSEVR Steam bootstrap shim is missing: {}", steamLoaderPath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleMissing;
    }

    FileVersion steamLoaderVersion{};
    if (GetFileVersion(steamLoaderPath, steamLoaderVersion) != 0 || !IsSkseVrVersionAtLeast2012(steamLoaderVersion) ||
        !std::equal(std::begin(fileVersion.versions), std::end(fileVersion.versions), std::begin(steamLoaderVersion.versions)))
    {
        spdlog::error("SKSEVR core and Steam bootstrap shim must have matching version resources from a supported official release");
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kVersionUnsupported;
    }

    const auto steamLoaderModuleHandle = GetModuleHandleW(steamLoaderModuleName.c_str());
    if (steamLoaderModuleHandle)
    {
        if (!IsModuleLoadedFromExpectedPath(steamLoaderModuleHandle, steamLoaderPath))
        {
            spdlog::error("A same-named SKSEVR Steam bootstrap shim is already loaded outside the expected game directory: {}", steamLoaderPath.string());
            return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoadFailed;
        }

        g_SKSESteamLoaderModuleHandle = steamLoaderModuleHandle;
        spdlog::info("SKSEVR Steam bootstrap shim already loaded: {}", steamLoaderPath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoaded;
    }

    g_SKSESteamLoaderModuleHandle = LoadLibraryW(steamLoaderPath.c_str());
    if (!g_SKSESteamLoaderModuleHandle)
    {
        spdlog::error("Failed to load required SKSEVR Steam bootstrap shim: {}", steamLoaderPath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoadFailed;
    }

    if (!IsModuleLoadedFromExpectedPath(g_SKSESteamLoaderModuleHandle, steamLoaderPath))
    {
        spdlog::error("Loaded SKSEVR Steam bootstrap shim does not match the expected game-directory file: {}", steamLoaderPath.string());
        return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoadFailed;
    }

    spdlog::info("SKSEVR Steam bootstrap shim loaded: {}; SKSEVR core will load from the game startup thread and requires runtime verification", steamLoaderPath.string());
    return g_scriptExtenderLoadResult = ScriptExtenderLoadResult::kModuleLoaded;
}
