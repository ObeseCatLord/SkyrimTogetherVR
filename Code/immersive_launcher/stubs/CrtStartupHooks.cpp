#include <FunctionHook.hpp>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#if !TP_SKYRIM_VR
#include "launcher.h"
#include <mutex>

static std::once_flag s_initGuard;
#endif

static uint16_t(WINAPI* Real_crtGetShowWindowMode)() = nullptr;
static int(WINAPI* Real_ismbbled)(uint32_t) = nullptr;

void TP_GetStartupInfoW(LPSTARTUPINFOW apInfo) noexcept
{
#if !TP_SKYRIM_VR
    std::call_once(s_initGuard, []() { launcher::InitClient(); });
#endif
    GetStartupInfoW(apInfo);
}

int TP_ismbblead(uint32_t c)
{
#if !TP_SKYRIM_VR
    std::call_once(s_initGuard, []() { launcher::InitClient(); });
#endif
    return Real_ismbbled(c);
}

// this is more of a workaround, till we add SEH table support.
void WINAPI TP_RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR* lpArguments)
{
    if (dwExceptionCode == 0x406D1388 && !IsDebuggerPresent())
        return; // thread naming

    RaiseException(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);
}

bool InstallStartHook()
{
    const auto startupInfo = TP_HOOK_IAT2("Kernel32.dll", "GetStartupInfoW", TP_GetStartupInfoW);
    const auto raiseException = TP_HOOK_IAT2("Kernel32.dll", "RaiseException", TP_RaiseException);
    return startupInfo != nullptr && raiseException != nullptr;
};
