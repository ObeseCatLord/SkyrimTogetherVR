#include <BranchInfo.h>

#include "CrashContext.h"
#include "CrashHandler.h"

#include <DbgHelp.h>
#include <Windows.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <ctime>
#include <vector>

using time_point = std::chrono::system_clock::time_point;

namespace
{
constexpr std::size_t kRetainedCrashDumpCount = 5;

#if defined(TP_CRASH_HANDLER_TESTING) && TP_CRASH_HANDLER_TESTING
std::atomic_uint32_t s_crashHandlerInvocations{};
#endif

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(time);
    const std::tm utc = *std::gmtime(&timestamp);
    std::stringstream stream;
    stream << std::put_time(&utc, format.c_str());
    return stream.str();
}

void FlushCrashLogs() noexcept
{
    try
    {
        spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& apLogger) { apLogger->flush(); });
    }
    catch (...)
    {
    }
}

void WriteCrashDump(PEXCEPTION_POINTERS apExceptionInfo) noexcept
{
#if defined(TP_CRASH_HANDLER_TESTING) && TP_CRASH_HANDLER_TESTING
    (void)apExceptionInfo;
    return;
#endif
#if (IS_MASTER) && (!defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1)
    constexpr bool kWriteMiniDumpByDefault = false;
#else
    constexpr bool kWriteMiniDumpByDefault = true;
#endif
    if (!kWriteMiniDumpByDefault)
        return;

    HANDLE dumpFile = INVALID_HANDLE_VALUE;
    bool dumpWritten = false;
    DWORD dumpError = ERROR_SUCCESS;
    try
    {
        MINIDUMP_EXCEPTION_INFORMATION exception{};
        exception.ThreadId = GetCurrentThreadId();
        exception.ExceptionPointers = apExceptionInfo;
        exception.ClientPointers = FALSE;

        char moduleFilename[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, moduleFilename, static_cast<DWORD>(std::size(moduleFilename))) == 0)
        {
            dumpError = GetLastError();
        }
        else
        {
            auto dumpDirectory = std::filesystem::path(moduleFilename).parent_path();
            CrashHandler::PruneCrashDumps(dumpDirectory);
            const auto dumpPath = dumpDirectory /
                                  ("crash_" + SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S") + ".dmp");

            dumpFile = CreateFileA(dumpPath.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (dumpFile == INVALID_HANDLE_VALUE)
            {
                dumpError = GetLastError();
            }
            else
            {
                auto dumpSettings = MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData |
                                    MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo |
                                    MiniDumpWithCodeSegs;
                char fullMemoryValue[2]{};
                if (GetEnvironmentVariableA("STVR_FULL_MEMORY_DUMP", fullMemoryValue, static_cast<DWORD>(std::size(fullMemoryValue))) > 0 &&
                    fullMemoryValue[0] == '1')
                {
                    dumpSettings |= MiniDumpWithFullMemory;
                    spdlog::critical("TopLevelCrashHandler: full-memory crash capture enabled by STVR_FULL_MEMORY_DUMP=1.");
                }

                dumpWritten = MiniDumpWriteDump(
                                  GetCurrentProcess(), GetCurrentProcessId(), dumpFile, static_cast<MINIDUMP_TYPE>(dumpSettings),
                                  apExceptionInfo ? &exception : nullptr, nullptr, nullptr) != FALSE;
                if (!dumpWritten)
                    dumpError = GetLastError();
            }
        }
    }
    catch (...)
    {
        if (dumpError == ERROR_SUCCESS)
            dumpError = ERROR_UNHANDLED_EXCEPTION;
    }

    if (dumpFile == INVALID_HANDLE_VALUE)
    {
        spdlog::critical("TopLevelCrashHandler: coredump file creation failed (error {}).", dumpError);
        return;
    }

    CloseHandle(dumpFile);
    if (dumpWritten)
        spdlog::critical("TopLevelCrashHandler: coredump created -> flush logs.");
    else
        spdlog::critical("TopLevelCrashHandler: coredump write failed (error {}).", dumpError);
}
} // namespace

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::s_pPreviousUnhandledFilter = nullptr;
std::atomic_flag CrashHandler::s_handlingCrash = ATOMIC_FLAG_INIT;

CrashHandler::CrashHandler() = default;

CrashHandler::~CrashHandler() = default;

void CrashHandler::Install() noexcept
{
    if (m_installed)
        return;

    s_pPreviousUnhandledFilter = SetUnhandledExceptionFilter(&CrashHandler::TopLevelCrashHandler);
    if (s_pPreviousUnhandledFilter == &CrashHandler::TopLevelCrashHandler)
        s_pPreviousUnhandledFilter = nullptr;
    m_installed = true;
}

LONG WINAPI CrashHandler::TopLevelCrashHandler(PEXCEPTION_POINTERS apExceptionInfo) noexcept
{
    if (s_handlingCrash.test_and_set(std::memory_order_acquire))
        return EXCEPTION_CONTINUE_SEARCH;

#if defined(TP_CRASH_HANDLER_TESTING) && TP_CRASH_HANDLER_TESTING
    s_crashHandlerInvocations.fetch_add(1, std::memory_order_relaxed);
#endif

    LONG disposition = EXCEPTION_CONTINUE_SEARCH;
    if (s_pPreviousUnhandledFilter)
    {
        disposition = s_pPreviousUnhandledFilter(apExceptionInfo);
        if (disposition == EXCEPTION_CONTINUE_EXECUTION)
        {
            s_handlingCrash.clear(std::memory_order_release);
            return disposition;
        }
    }

    try
    {
        const auto context = CaptureCrashContext(apExceptionInfo);
        if (context.Valid)
        {
            spdlog::critical(
                "TopLevelCrashHandler: unhandled exception code=0x{:08X} flags=0x{:X} address=0x{:X} "
                "rip=0x{:X} rsp=0x{:X} rcx=0x{:X} rdx=0x{:X}",
                context.ExceptionCode, context.ExceptionFlags, context.ExceptionAddress, context.InstructionPointer,
                context.StackPointer, context.FirstArgument, context.SecondArgument);
            if (context.HasAccessDetails)
            {
                spdlog::critical(
                    "TopLevelCrashHandler: access type={} address=0x{:X}", context.AccessType, context.AccessAddress);
            }
            if (context.HasStackReturn)
                spdlog::critical("TopLevelCrashHandler: stack return=0x{:X}", context.StackReturn);
            else
                spdlog::critical("TopLevelCrashHandler: stack return unavailable");
        }
        else
        {
            spdlog::critical("TopLevelCrashHandler: unhandled exception context unavailable");
        }

        if (disposition == EXCEPTION_CONTINUE_SEARCH)
            WriteCrashDump(apExceptionInfo);
        FlushCrashLogs();
    }
    catch (...)
    {
        FlushCrashLogs();
    }

    // A terminal disposition should produce at most one capture. Only a prior
    // filter that resumes execution re-arms the handler.
    return disposition;
}

#if defined(TP_CRASH_HANDLER_TESTING) && TP_CRASH_HANDLER_TESTING
LONG CrashHandler::InvokeForTesting(PEXCEPTION_POINTERS apExceptionInfo) noexcept
{
    return TopLevelCrashHandler(apExceptionInfo);
}

void CrashHandler::ResetForTesting(LPTOP_LEVEL_EXCEPTION_FILTER apPreviousFilter) noexcept
{
    s_pPreviousUnhandledFilter = apPreviousFilter;
    s_handlingCrash.clear(std::memory_order_release);
    s_crashHandlerInvocations.store(0, std::memory_order_relaxed);
}

std::uint32_t CrashHandler::GetInvocationCountForTesting() noexcept
{
    return s_crashHandlerInvocations.load(std::memory_order_relaxed);
}

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::GetTopLevelFilterForTesting() noexcept
{
    return &CrashHandler::TopLevelCrashHandler;
}
#endif

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
            dumps.emplace_back(entry);
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
