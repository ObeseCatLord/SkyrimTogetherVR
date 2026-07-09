#pragma once

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace SkyrimTogetherVR::Handoff
{
inline std::filesystem::path GetEnvironmentGameDirectory()
{
#if defined(_WIN32)
    wchar_t buffer[32768]{};
    constexpr DWORD kBufferSize = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const auto length = GetEnvironmentVariableW(L"STVR_GAME_PATH", buffer, kBufferSize);
    if (length > 0 && length < kBufferSize)
        return std::filesystem::path(buffer);
#endif

    const char* pPath = std::getenv("STVR_GAME_PATH");
    if (pPath && pPath[0])
        return std::filesystem::path(pPath);

    return {};
}

inline std::filesystem::path GetWorkingDirectory()
{
    std::error_code ec;
    auto path = std::filesystem::current_path(ec);
    if (ec || path.empty())
        return {};

    return path;
}

inline std::filesystem::path GetExecutableDirectory()
{
#if defined(_WIN32)
    wchar_t buffer[32768]{};
    constexpr DWORD kBufferSize = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const auto length = GetModuleFileNameW(nullptr, buffer, kBufferSize);
    if (length > 0 && length < kBufferSize)
        return std::filesystem::path(buffer).parent_path();
#endif

    return {};
}

inline bool IsSkyrimVRGameDirectory(const std::filesystem::path& acPath)
{
    if (acPath.empty())
        return false;

    std::error_code ec;
    return std::filesystem::exists(acPath / "SkyrimVR.exe", ec);
}

inline std::filesystem::path GetGameDirectory()
{
    const auto envPath = GetEnvironmentGameDirectory();
    if (!envPath.empty())
        return envPath;

    const auto currentPath = GetWorkingDirectory();
    if (IsSkyrimVRGameDirectory(currentPath))
        return currentPath;

    const auto executablePath = GetExecutableDirectory();
    if (IsSkyrimVRGameDirectory(executablePath))
        return executablePath;

    if (!currentPath.empty())
        return currentPath;

    if (!executablePath.empty())
        return executablePath;

    return ".";
}

inline std::filesystem::path GetDirectory()
{
    return GetGameDirectory() / "Data" / "SkyrimTogetherReborn";
}

inline std::filesystem::path GetFile(const char* apFileName)
{
    return GetDirectory() / apFileName;
}
}
