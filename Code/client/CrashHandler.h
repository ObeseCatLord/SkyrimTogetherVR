#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>

class CrashHandler
{
    static LPTOP_LEVEL_EXCEPTION_FILTER s_pPreviousUnhandledFilter;
    static std::atomic_flag s_handlingCrash;

    bool m_installed{};

    static LONG WINAPI TopLevelCrashHandler(PEXCEPTION_POINTERS apExceptionInfo) noexcept;

  public:
    CrashHandler();
    ~CrashHandler();

    void Install() noexcept;
    static void PruneCrashDumps(const std::filesystem::path& path);

#if defined(TP_CRASH_HANDLER_TESTING) && TP_CRASH_HANDLER_TESTING
    static LONG InvokeForTesting(PEXCEPTION_POINTERS apExceptionInfo) noexcept;
    static void ResetForTesting(LPTOP_LEVEL_EXCEPTION_FILTER apPreviousFilter = nullptr) noexcept;
    static std::uint32_t GetInvocationCountForTesting() noexcept;
    static LPTOP_LEVEL_EXCEPTION_FILTER GetTopLevelFilterForTesting() noexcept;
#endif
};
