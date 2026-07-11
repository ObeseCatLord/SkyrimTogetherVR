
#include <ScriptExtender.h>
#include <TiltedOnlinePCH.h>
#include <VersionDb.h>

#include <array>

namespace
{
#if !defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1
#error "SkyrimTogetherVR client must be built with TP_SKYRIM_VR=1"
#endif

constexpr wchar_t kScriptExtenderName[] = L"sksevr";
// SKSEVR 2.0.12 is the runtime 1.4.15 release used by Skyrim VR.
constexpr int kSKSEMinBuild = 2001200;
constexpr const char* kUnsupportedScriptExtenderMessage = "SKSEVR 2.0.12 or newer is required";
constexpr wchar_t kGamePathEnvironmentVariable[] = L"STVR_GAME_PATH";

HMODULE g_SKSEModuleHandle{nullptr};

struct FileVersion
{
    static constexpr uint8_t scVersionSize = 4;
    DWORD versions[scVersionSize];
};

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
    if (!VerQueryValueA(&buf[0], "\\", reinterpret_cast<LPVOID*>(&pvi), reinterpret_cast<unsigned int*>(&sz)))
    {
        return 3;
    }

    aVersion.versions[0] = pvi->dwProductVersionMS >> 16;
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

std::filesystem::path GetScriptExtenderPath()
{
    const auto exeVersion = GetSKSEStyleExeVersion();
    const std::wstring version(exeVersion.begin(), exeVersion.end());
    return GetGameDirectory() / (std::wstring(kScriptExtenderName) + L"_" + version + L".dll");
}
} // namespace

bool IsScriptExtenderLoaded()
{
    return g_SKSEModuleHandle;
}

ScriptExtenderLoadResult LoadScriptExender()
{
    const auto path = GetScriptExtenderPath();
    const auto moduleName = path.filename().wstring();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        spdlog::error("Required SKSEVR module is missing: {}", path.string());
        return ScriptExtenderLoadResult::kModuleMissing;
    }

    FileVersion fileVersion{};
    const bool versionVerified = GetFileVersion(path, fileVersion) == 0;
    if (!versionVerified)
    {
        spdlog::warn("Unable to verify SKSEVR version resource for {}; using the runtime-pinned module name", path.string());
    }

    const int skseVersion = fileVersion.versions[0] * 1000000 + fileVersion.versions[1] * 10000 + fileVersion.versions[2] * 100 + fileVersion.versions[3];
    if (versionVerified && skseVersion < kSKSEMinBuild)
    {
        spdlog::error(kUnsupportedScriptExtenderMessage);
        return ScriptExtenderLoadResult::kVersionUnsupported;
    }

    g_SKSEModuleHandle = GetModuleHandleW(moduleName.c_str());
    if (g_SKSEModuleHandle)
    {
        spdlog::info("SKSEVR module already loaded: {}", path.string());
        return ScriptExtenderLoadResult::kModuleLoaded;
    }

    g_SKSEModuleHandle = LoadLibraryW(path.c_str());
    if (!g_SKSEModuleHandle)
    {
        spdlog::error("Failed to load required SKSEVR module: {}", path.string());
        return ScriptExtenderLoadResult::kModuleLoadFailed;
    }

    spdlog::info("SKSEVR module loaded: {}; operational initialization requires runtime verification", path.string());
    return ScriptExtenderLoadResult::kModuleLoaded;
}
