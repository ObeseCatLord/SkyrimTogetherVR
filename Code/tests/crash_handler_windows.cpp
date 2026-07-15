#include <catch2/catch.hpp>

#if defined(_WIN32)
#include <CrashHandler.h>

#include <spdlog/spdlog.h>

#include <atomic>

namespace
{
struct SyntheticException
{
    CONTEXT Context{};
    EXCEPTION_RECORD Record{};
    EXCEPTION_POINTERS Pointers{&Record, &Context};

    SyntheticException()
    {
        Context.Rip = 0x140ABCDEF;
        Context.Rsp = 1;
        Context.Rcx = 0x1111;
        Context.Rdx = 0x2222;
        Record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        Record.ExceptionAddress = reinterpret_cast<void*>(Context.Rip);
        Record.NumberParameters = 2;
        Record.ExceptionInformation[0] = 0;
        Record.ExceptionInformation[1] = 0xDEADBEEF;
    }
};

struct ProcessFilterRestore
{
    LPTOP_LEVEL_EXCEPTION_FILTER Previous{};
    ~ProcessFilterRestore() { SetUnhandledExceptionFilter(Previous); }
};

std::atomic_uint32_t s_priorCalls{};
std::atomic_long s_recursiveDisposition{EXCEPTION_EXECUTE_HANDLER};

LONG WINAPI PriorContinueExecution(PEXCEPTION_POINTERS)
{
    s_priorCalls.fetch_add(1, std::memory_order_relaxed);
    return EXCEPTION_CONTINUE_EXECUTION;
}

LONG WINAPI PriorExecuteHandler(PEXCEPTION_POINTERS)
{
    s_priorCalls.fetch_add(1, std::memory_order_relaxed);
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI PriorContinueSearch(PEXCEPTION_POINTERS)
{
    s_priorCalls.fetch_add(1, std::memory_order_relaxed);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI PriorRecursiveEntry(PEXCEPTION_POINTERS apExceptionInfo)
{
    s_priorCalls.fetch_add(1, std::memory_order_relaxed);
    s_recursiveDisposition.store(CrashHandler::InvokeForTesting(apExceptionInfo), std::memory_order_relaxed);
    return EXCEPTION_CONTINUE_EXECUTION;
}

__declspec(noinline) void RaiseFrameHandledAccessViolation()
{
    __try
    {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
} // namespace

TEST_CASE("Frame-handled access violations do not reach the STVR top-level filter")
{
    const auto originalFilter = SetUnhandledExceptionFilter(&PriorContinueSearch);
    ProcessFilterRestore restore{originalFilter};
    CrashHandler::ResetForTesting();
    CrashHandler handler;
    handler.Install();
    const auto logger = spdlog::default_logger();

    RaiseFrameHandledAccessViolation();

    REQUIRE(CrashHandler::GetInvocationCountForTesting() == 0);
    REQUIRE(spdlog::default_logger() == logger);
}

TEST_CASE("Crash handler installs outermost and preserves the existing filter")
{
    s_priorCalls.store(0, std::memory_order_relaxed);
    const auto originalFilter = SetUnhandledExceptionFilter(&PriorContinueSearch);
    ProcessFilterRestore restore{originalFilter};
    CrashHandler::ResetForTesting();
    CrashHandler handler;
    handler.Install();

    const auto displacedFilter = SetUnhandledExceptionFilter(&PriorExecuteHandler);
    REQUIRE(displacedFilter == CrashHandler::GetTopLevelFilterForTesting());

    SyntheticException exception;
    REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_SEARCH);
    REQUIRE(s_priorCalls.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("Crash handler propagates prior filter dispositions")
{
    SyntheticException exception;
    const auto logger = spdlog::default_logger();

    SECTION("continue execution rearms capture")
    {
        s_priorCalls.store(0, std::memory_order_relaxed);
        CrashHandler::ResetForTesting(&PriorContinueExecution);
        REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_EXECUTION);
        REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_EXECUTION);
        REQUIRE(CrashHandler::GetInvocationCountForTesting() == 2);
        REQUIRE(s_priorCalls.load(std::memory_order_relaxed) == 2);
    }

    SECTION("execute handler records once and stays latched")
    {
        s_priorCalls.store(0, std::memory_order_relaxed);
        CrashHandler::ResetForTesting(&PriorExecuteHandler);
        REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_EXECUTE_HANDLER);
        REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_SEARCH);
        REQUIRE(CrashHandler::GetInvocationCountForTesting() == 1);
        REQUIRE(s_priorCalls.load(std::memory_order_relaxed) == 1);
    }

    SECTION("continue search performs one fallback capture and stays latched")
    {
        s_priorCalls.store(0, std::memory_order_relaxed);
        CrashHandler::ResetForTesting(&PriorContinueSearch);
        REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_SEARCH);
        REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_SEARCH);
        REQUIRE(CrashHandler::GetInvocationCountForTesting() == 1);
        REQUIRE(s_priorCalls.load(std::memory_order_relaxed) == 1);
    }

    REQUIRE(spdlog::default_logger() == logger);
}

TEST_CASE("Recursive crash-handler entry does not recurse or deadlock")
{
    s_priorCalls.store(0, std::memory_order_relaxed);
    s_recursiveDisposition.store(EXCEPTION_EXECUTE_HANDLER, std::memory_order_relaxed);
    CrashHandler::ResetForTesting(&PriorRecursiveEntry);
    SyntheticException exception;

    REQUIRE(CrashHandler::InvokeForTesting(&exception.Pointers) == EXCEPTION_CONTINUE_EXECUTION);
    REQUIRE(s_recursiveDisposition.load(std::memory_order_relaxed) == EXCEPTION_CONTINUE_SEARCH);
    REQUIRE(CrashHandler::GetInvocationCountForTesting() == 1);
    REQUIRE(s_priorCalls.load(std::memory_order_relaxed) == 1);
}
#endif
