#pragma once

#include <Windows.h>

#include <cstdint>

struct CrashContextSnapshot
{
    bool Valid{};
    bool HasAccessDetails{};
    bool HasStackReturn{};
    std::uint32_t ExceptionCode{};
    std::uint32_t ExceptionFlags{};
    std::uint64_t ExceptionAddress{};
    std::uint64_t InstructionPointer{};
    std::uint64_t StackPointer{};
    std::uint64_t FirstArgument{};
    std::uint64_t SecondArgument{};
    std::uint64_t AccessType{};
    std::uint64_t AccessAddress{};
    std::uint64_t StackReturn{};
};

inline CrashContextSnapshot CaptureCrashContext(const EXCEPTION_POINTERS* apExceptionInfo) noexcept
{
    CrashContextSnapshot snapshot{};
    if (!apExceptionInfo || !apExceptionInfo->ExceptionRecord || !apExceptionInfo->ContextRecord)
        return snapshot;

    const auto& record = *apExceptionInfo->ExceptionRecord;
    const auto& context = *apExceptionInfo->ContextRecord;
    snapshot.Valid = true;
    snapshot.ExceptionCode = record.ExceptionCode;
    snapshot.ExceptionFlags = record.ExceptionFlags;
    snapshot.ExceptionAddress = reinterpret_cast<std::uint64_t>(record.ExceptionAddress);

#if defined(_M_X64) || defined(__x86_64__)
    snapshot.InstructionPointer = context.Rip;
    snapshot.StackPointer = context.Rsp;
    snapshot.FirstArgument = context.Rcx;
    snapshot.SecondArgument = context.Rdx;
#endif

    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
    {
        snapshot.HasAccessDetails = true;
        snapshot.AccessType = record.ExceptionInformation[0];
        snapshot.AccessAddress = record.ExceptionInformation[1];
    }

    if (snapshot.StackPointer != 0)
    {
        SIZE_T bytesRead = 0;
        snapshot.HasStackReturn = ReadProcessMemory(
                                      GetCurrentProcess(), reinterpret_cast<const void*>(snapshot.StackPointer), &snapshot.StackReturn,
                                      sizeof(snapshot.StackReturn), &bytesRead) != FALSE &&
                                  bytesRead == sizeof(snapshot.StackReturn);
    }

    return snapshot;
}
