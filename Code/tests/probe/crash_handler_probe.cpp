#include <CrashHandler.h>

namespace
{
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
} // namespace

int main()
{
    const auto originalFilter = SetUnhandledExceptionFilter(&PriorContinueSearch);
    CrashHandler::ResetForTesting();
    CrashHandler handler;
    handler.Install();

    RaiseFrameHandledAccessViolation();
    const auto invocationCount = CrashHandler::GetInvocationCountForTesting();

    SetUnhandledExceptionFilter(originalFilter);
    return invocationCount == 0 ? 0 : 1;
}
