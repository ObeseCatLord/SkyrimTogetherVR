#pragma once

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace SkyrimTogetherVR::Handoff
{
inline constexpr std::size_t kLaunchNonceHexLength = 32;

inline bool NormalizeLaunchNonce(const std::string_view acValue, std::string& aOut)
{
    aOut.clear();
    if (acValue.size() != kLaunchNonceHexLength)
        return false;

    std::string normalized;
    normalized.reserve(kLaunchNonceHexLength);
    for (const auto character : acValue)
    {
        if (character >= '0' && character <= '9')
            normalized.push_back(character);
        else if (character >= 'a' && character <= 'f')
            normalized.push_back(character);
        else if (character >= 'A' && character <= 'F')
            normalized.push_back(static_cast<char>(character - 'A' + 'a'));
        else
            return false;
    }

    aOut = std::move(normalized);
    return true;
}

inline std::string GetLaunchNonce()
{
#if defined(_WIN32)
    wchar_t buffer[kLaunchNonceHexLength + 1]{};
    const auto length = GetEnvironmentVariableW(
        L"STVR_LAUNCH_NONCE", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer))
    {
        std::string value;
        value.reserve(length);
        for (DWORD index = 0; index < length; ++index)
        {
            if (buffer[index] > 0x7f)
                return {};
            value.push_back(static_cast<char>(buffer[index]));
        }

        std::string nonce;
        return NormalizeLaunchNonce(value, nonce) ? nonce : std::string{};
    }
#endif

    const char* pValue = std::getenv("STVR_LAUNCH_NONCE");
    std::string nonce;
    if (!pValue || !NormalizeLaunchNonce(pValue, nonce))
        return {};

    return nonce;
}

inline std::uint32_t GetProcessId() noexcept
{
#if defined(_WIN32)
    return ::GetCurrentProcessId();
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

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

inline bool ReplaceFileAtomically(const std::filesystem::path& acTemporaryPath, const std::filesystem::path& acDestinationPath) noexcept
{
#if defined(_WIN32)
    return MoveFileExW(acTemporaryPath.c_str(), acDestinationPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code ec;
    std::filesystem::rename(acTemporaryPath, acDestinationPath, ec);
    return !ec;
#endif
}

template <class TWriter> inline bool WriteFileAtomically(const std::filesystem::path& acDestinationPath, TWriter&& aWriter) noexcept
{
    static std::atomic<std::uint64_t> s_temporaryFileSequence{0};
    std::filesystem::path temporaryPath;
    bool temporaryFileOpened = false;

    try
    {
        if (acDestinationPath.empty())
            return false;

        std::error_code ec;
        const auto directory = acDestinationPath.parent_path();
        if (!directory.empty())
        {
            std::filesystem::create_directories(directory, ec);
            if (ec)
                return false;
        }

        temporaryPath = acDestinationPath;
        temporaryPath += std::filesystem::path(".tmp.");
        temporaryPath += std::filesystem::path(std::to_string(GetProcessId()));
        temporaryPath += std::filesystem::path(".");
        temporaryPath += std::filesystem::path(std::to_string(s_temporaryFileSequence.fetch_add(1, std::memory_order_relaxed) + 1));

        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        temporaryFileOpened = true;

        aWriter(file);
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(temporaryPath, ec);
            return false;
        }

        file.close();
        if (file.fail())
        {
            std::filesystem::remove(temporaryPath, ec);
            return false;
        }

        if (ReplaceFileAtomically(temporaryPath, acDestinationPath))
            return true;

        std::filesystem::remove(temporaryPath, ec);
    }
    catch (...)
    {
        if (temporaryFileOpened && !temporaryPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(temporaryPath, ec);
        }
        return false;
    }

    return false;
}

inline void WriteLaunchIdentity(std::ostream& aOut)
{
    aOut << "launchNonce=" << GetLaunchNonce() << "\n";
    aOut << "processId=" << GetProcessId() << "\n";
    aOut << "gamePath=" << GetGameDirectory().string() << "\n";
}
}
