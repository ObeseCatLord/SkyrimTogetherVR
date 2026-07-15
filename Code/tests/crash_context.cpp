#include <catch2/catch.hpp>

#if defined(_WIN32)
#include <CrashContext.h>

TEST_CASE("Crash context captures x64 registers and access details")
{
    const std::uint64_t returnAddress = 0x140123456;
    CONTEXT context{};
    context.Rip = 0x140ABCDEF;
    context.Rsp = reinterpret_cast<DWORD64>(&returnAddress);
    context.Rcx = 0x1111;
    context.Rdx = 0x2222;

    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = 1;
    record.ExceptionInformation[1] = 0xDEADBEEF;

    EXCEPTION_POINTERS exception{&record, &context};
    const auto snapshot = CaptureCrashContext(&exception);

    REQUIRE(snapshot.Valid);
    REQUIRE(snapshot.ExceptionCode == EXCEPTION_ACCESS_VIOLATION);
    REQUIRE(snapshot.InstructionPointer == context.Rip);
    REQUIRE(snapshot.StackPointer == context.Rsp);
    REQUIRE(snapshot.FirstArgument == context.Rcx);
    REQUIRE(snapshot.SecondArgument == context.Rdx);
    REQUIRE(snapshot.HasAccessDetails);
    REQUIRE(snapshot.AccessType == 1);
    REQUIRE(snapshot.AccessAddress == 0xDEADBEEF);
    REQUIRE(snapshot.HasStackReturn);
    REQUIRE(snapshot.StackReturn == returnAddress);
}

TEST_CASE("Crash context rejects unreadable stack memory without faulting")
{
    CONTEXT context{};
    context.Rsp = 1;
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    EXCEPTION_POINTERS exception{&record, &context};

    const auto snapshot = CaptureCrashContext(&exception);
    REQUIRE(snapshot.Valid);
    REQUIRE_FALSE(snapshot.HasStackReturn);
}

TEST_CASE("Crash context rejects incomplete exception pointers")
{
    REQUIRE_FALSE(CaptureCrashContext(nullptr).Valid);

    EXCEPTION_POINTERS exception{};
    REQUIRE_FALSE(CaptureCrashContext(&exception).Valid);
}
#endif
