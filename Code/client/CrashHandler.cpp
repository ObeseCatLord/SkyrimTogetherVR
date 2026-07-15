#include <BranchInfo.h>
#include "CrashHandler.h"
#include <DbgHelp.h>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <strsafe.h>
#include <vector>

using time_point = std::chrono::system_clock::time_point;

namespace
{
constexpr std::size_t kRetainedCrashDumpCount = 5;
}

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::gmtime(&tt); // GMT (UTC)
    // std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
    std::stringstream ss;
    ss << std::put_time(&tm, format.c_str());
    return ss.str();
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    static int alreadyCrashed = 0;
    auto retval = EXCEPTION_CONTINUE_SEARCH;

    // Serialize
    static std::mutex singleThreaded;
    std::unique_lock lock{singleThreaded};

    // Check for severe, not continuable and not software-originated exception
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && alreadyCrashed++ == 0)
    {
        spdlog::critical(__FUNCTION__ ": crash occurred!");

        spdlog::error(
            __FUNCTION__ ": exception code is {:x}, at address {}, flags {:x} ", pExceptionInfo->ExceptionRecord->ExceptionCode, pExceptionInfo->ExceptionRecord->ExceptionAddress,
            pExceptionInfo->ExceptionRecord->ExceptionFlags);

#if (IS_MASTER) && (!defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1)
        volatile static bool bMiniDump = false;
#else
        volatile static bool bMiniDump = true;
#endif
        if (bMiniDump)
        {
            HANDLE hDumpFile = INVALID_HANDLE_VALUE;
            bool dumpWritten = false;
            DWORD dumpError = ERROR_SUCCESS;
            try
            {
                MINIDUMP_EXCEPTION_INFORMATION M;
                char dumpPath[MAX_PATH];

                M.ThreadId = GetCurrentThreadId();
                M.ExceptionPointers = pExceptionInfo;
                M.ClientPointers = 0;

                std::ostringstream oss;
                oss << "crash_" << SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S") << ".dmp";

                GetModuleFileNameA(NULL, dumpPath, sizeof(dumpPath));
                std::filesystem::path modulePath(dumpPath);
                auto subPath = modulePath.parent_path();

                CrashHandler::PruneCrashDumps(subPath);

                subPath /= oss.str();

                hDumpFile = CreateFileA(subPath.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

                // Capture mapped engine code and thread metadata without writing a full-memory dump.
                auto dumpSettings = MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                    MiniDumpWithFullMemoryInfo | MiniDumpWithCodeSegs;
                char fullMemoryValue[2]{};
                if (GetEnvironmentVariableA("STVR_FULL_MEMORY_DUMP", fullMemoryValue, static_cast<DWORD>(sizeof(fullMemoryValue))) > 0 && fullMemoryValue[0] == '1')
                {
                    dumpSettings |= MiniDumpWithFullMemory;
                    spdlog::critical(__FUNCTION__ ": full-memory crash capture enabled by STVR_FULL_MEMORY_DUMP=1.");
                }

                if (hDumpFile != INVALID_HANDLE_VALUE)
                {
                    dumpWritten = MiniDumpWriteDump(
                                      GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, static_cast<MINIDUMP_TYPE>(dumpSettings), pExceptionInfo ? &M : nullptr, nullptr,
                                      nullptr) != FALSE;
                    if (!dumpWritten)
                        dumpError = GetLastError();
                }
                else
                {
                    dumpError = GetLastError();
                }
            }
            catch (...) // Mini-dump is best effort only.
            {
            }

            if (hDumpFile == INVALID_HANDLE_VALUE)
            {
                spdlog::critical(__FUNCTION__ ": coredump file creation failed (error {}).", dumpError);
            }
            else
            {
                CloseHandle(hDumpFile);
                if (dumpWritten)
                    spdlog::critical(__FUNCTION__ ": coredump created -> flush logs.");
                else
                    spdlog::critical(__FUNCTION__ ": coredump write failed (error {}).", dumpError);
            }
        }

        // Something in STR breaks top-level unhandled exception filters.
        // The Win API for them is pretty clunky (non-atomic, not chainable),
        // but they can do some important things. If someone actually set one
        // they probably meant it; make sure it actually runs.
        // This will make more CrashLogger mods work with STR.

        // Get the current unhandled exception filter. If it has changed
        // from when STR started up, invoke it here.
        LPTOP_LEVEL_EXCEPTION_FILTER pCurrentUnhandledExceptionFilter = SetUnhandledExceptionFilter(CrashHandler::GetOriginalUnhandledExceptionFilter());
        SetUnhandledExceptionFilter(pCurrentUnhandledExceptionFilter);
        if (pCurrentUnhandledExceptionFilter != CrashHandler::GetOriginalUnhandledExceptionFilter())
        {
            spdlog::critical(__FUNCTION__ ": UnhandledExceptionFilter() workaround triggered.");

            singleThreaded.unlock(); // Might reenter, but is safe at this point.
            if ((*pCurrentUnhandledExceptionFilter)(pExceptionInfo) == EXCEPTION_CONTINUE_EXECUTION)
                retval = EXCEPTION_CONTINUE_EXECUTION;
            singleThreaded.lock();
        }

        spdlog::shutdown();
    }
    return retval;
}

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::m_pUnhandled;
CrashHandler::CrashHandler()
{
    // Record the original (or as close as we can get) top-level unhandled exception handler.
    // We grab this so we can see if it is changed, presumably by a mod or even graphics drivers.
    // Something in STR breaks unhandled exception handling, so we'll fake it if necessary.
    // This is the only way to get the current setting, but the race is small.
    m_pUnhandled = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(m_pUnhandled);

    m_handler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
}

CrashHandler::~CrashHandler()
{
}

void CrashHandler::PruneCrashDumps(const std::filesystem::path& path)
{
    std::error_code error;
    std::vector<std::filesystem::directory_entry> dumps;
    std::filesystem::directory_iterator iterator(path, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        const auto& entry = *iterator;
        const auto filename = entry.path().filename().string();
        if (entry.is_regular_file(error) && filename.rfind("crash_", 0) == 0 && entry.path().extension() == ".dmp")
        {
            dumps.emplace_back(entry);
        }
        error.clear();
        iterator.increment(error);
    }

    if (dumps.size() < kRetainedCrashDumpCount)
        return;

    std::sort(
        dumps.begin(), dumps.end(),
        [](const std::filesystem::directory_entry& left, const std::filesystem::directory_entry& right)
        {
            std::error_code leftError;
            std::error_code rightError;
            return left.last_write_time(leftError) < right.last_write_time(rightError);
        });

    const auto dumpsToRemove = dumps.size() - (kRetainedCrashDumpCount - 1);
    for (std::size_t index = 0; index < dumpsToRemove; ++index)
    {
        std::filesystem::remove(dumps[index].path(), error);
        error.clear();
    }
}
