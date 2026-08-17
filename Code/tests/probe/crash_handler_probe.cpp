#include <CrashHandler.h>

#include <iterator>

namespace
{
constexpr DWORD kMsVcThreadNameException = 0x406D1388;

#pragma pack(push, 8)
struct THREADNAME_INFO
{
    DWORD Type;
    LPCSTR Name;
    DWORD ThreadId;
    DWORD Flags;
};
#pragma pack(pop)

LONG WINAPI PriorContinueSearch(PEXCEPTION_POINTERS)
{
    return EXCEPTION_CONTINUE_SEARCH;
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

__declspec(noinline) void RaiseMsVcThreadNameNotification()
{
    const char threadName[] = "STVR crash-handler probe";
    const THREADNAME_INFO threadNameInfo{
        0x1000,
        threadName,
        static_cast<DWORD>(-1),
        0,
    };
    static_assert(sizeof(threadNameInfo) % sizeof(ULONG_PTR) == 0);
    RaiseException(
        kMsVcThreadNameException, 0, static_cast<DWORD>(sizeof(threadNameInfo) / sizeof(ULONG_PTR)),
        reinterpret_cast<const ULONG_PTR*>(&threadNameInfo));
}
} // namespace

int main()
{
    const auto originalFilter = SetUnhandledExceptionFilter(&PriorContinueSearch);
    CrashHandler::ResetForTesting();
    CrashHandler handler;
    handler.Install();

    RaiseMsVcThreadNameNotification();
    const auto ignoredNotificationCount = CrashHandler::GetIgnoredNotificationCountForTesting();
    const auto notificationInvocationCount = CrashHandler::GetInvocationCountForTesting();
    RaiseFrameHandledAccessViolation();
    const auto frameHandledInvocationCount = CrashHandler::GetInvocationCountForTesting();

    SetUnhandledExceptionFilter(originalFilter);
    return ignoredNotificationCount == 1 && notificationInvocationCount == 0 && frameHandledInvocationCount == 0 ? 0 : 1;
}
