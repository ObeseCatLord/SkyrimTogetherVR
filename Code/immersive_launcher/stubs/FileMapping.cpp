
#include <strsafe.h>

#include <MinHook.h>
#include <winternl.h>

#include <intrin.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>

#include <FunctionHook.hpp>
#include <TiltedCore/Initializer.hpp>

#include "DllBlocklist.h"
#include "LoaderTrace.h"
#include "TargetConfig.h"
#include "Utils/NtInternal.h"
#include "utils/Error.h"
#include "launcher.h"

// defined in MemoryLayout.cpp
extern bool IsThisExeAddress(const uint8_t* apAddress);
extern bool IsGameMemoryAddress(const uint8_t* apAddress);

extern "C" __declspec(dllimport) NTSTATUS WINAPI LdrGetDllFullName(HMODULE, PUNICODE_STRING);

namespace
{
std::wstring s_OverridePath;

DWORD(WINAPI* RealGetModuleFileNameW)(HMODULE, LPWSTR, DWORD) = nullptr;
DWORD(WINAPI* RealGetModuleFileNameA)(HMODULE, LPSTR, DWORD) = nullptr;
HMODULE(WINAPI* RealGetModuleHandleW)(LPCWSTR) = nullptr;
HMODULE(WINAPI* RealGetModuleHandleA)(LPCSTR) = nullptr;
NTSTATUS(WINAPI* RealLdrLoadDll)(const wchar_t*, uint32_t*, UNICODE_STRING*, HANDLE*) = nullptr;
NTSTATUS(WINAPI* RealLdrGetDllHandle)(PWSTR, PULONG, PUNICODE_STRING, PVOID*) = nullptr;
NTSTATUS(WINAPI* RealLdrGetDllFullName)(HMODULE, PUNICODE_STRING) = nullptr;
NTSTATUS(WINAPI* RealLdrGetDllHandleEx)
(ULONG Flags, PWSTR DllPath, PULONG DllCharacteristics, UNICODE_STRING* DllName, PVOID* DllHandle) = nullptr;

#if TP_SKYRIM_VR
constexpr size_t kLdrLoadTraceCapacity = 256;
constexpr size_t kLdrLoadTraceThreadCapacity = 32;
constexpr size_t kLdrLoadTraceNameCapacity = 160;

enum LdrLoadTracePhase : LONG
{
    kLdrLoadTraceUnused,
    kLdrLoadTraceWriting,
    kLdrLoadTraceEntered,
    kLdrLoadTraceReturned,
};

struct LdrLoadTraceEntry
{
    volatile LONG sequence = -1;
    volatile LONG phase = kLdrLoadTraceUnused;
    DWORD threadId = 0;
    DWORD depth = 0;
    volatile LONG64 callSite = 0;
    volatile LONG64 startTick = 0;
    volatile LONG64 endTick = 0;
    volatile LONG status = 0;
    USHORT nameLength = 0;
    wchar_t name[kLdrLoadTraceNameCapacity]{};
};

struct LdrLoadTraceThreadState
{
    volatile LONG threadId = 0;
    volatile LONG depth = 0;
};

struct LdrLoadTraceScope
{
    LdrLoadTraceEntry* entry = nullptr;
    LdrLoadTraceThreadState* threadState = nullptr;
};

LdrLoadTraceEntry s_ldrLoadTrace[kLdrLoadTraceCapacity];
LdrLoadTraceThreadState s_ldrLoadTraceThreads[kLdrLoadTraceThreadCapacity];
volatile LONG s_ldrLoadTraceSequence = 0;
volatile LONG s_ldrLoadTraceThreadSlotOverflow = 0;

LdrLoadTraceThreadState* FindLdrLoadTraceThreadState(DWORD aThreadId) noexcept
{
    const auto threadId = static_cast<LONG>(aThreadId);
    for (auto& state : s_ldrLoadTraceThreads)
    {
        const auto observedThreadId = InterlockedCompareExchange(&state.threadId, threadId, 0);
        if (observedThreadId == 0 || observedThreadId == threadId)
            return &state;
    }

    return nullptr;
}

LdrLoadTraceScope BeginLdrLoadTrace(const UNICODE_STRING* apFileName) noexcept
{
    const auto sequence = InterlockedIncrement(&s_ldrLoadTraceSequence) - 1;
    auto& entry = s_ldrLoadTrace[static_cast<size_t>(sequence) % kLdrLoadTraceCapacity];

    InterlockedExchange(&entry.phase, kLdrLoadTraceWriting);
    InterlockedExchange(&entry.sequence, sequence);

    const auto threadId = GetCurrentThreadId();
    auto* threadState = FindLdrLoadTraceThreadState(threadId);
    if (!threadState)
        InterlockedIncrement(&s_ldrLoadTraceThreadSlotOverflow);
    const auto depth = threadState ? static_cast<DWORD>(InterlockedIncrement(&threadState->depth)) : 0;

    entry.threadId = threadId;
    entry.depth = depth;
    InterlockedExchange64(&entry.callSite, static_cast<LONG64>(reinterpret_cast<uintptr_t>(_ReturnAddress())));
    InterlockedExchange64(&entry.startTick, static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange64(&entry.endTick, 0);
    InterlockedExchange(&entry.status, static_cast<LONG>(0x00000103L)); // STATUS_PENDING

    const auto sourceLength = apFileName && apFileName->Buffer ? apFileName->Length / sizeof(wchar_t) : 0;
    const auto nameLength = static_cast<USHORT>(std::min<size_t>(sourceLength, kLdrLoadTraceNameCapacity - 1));
    for (USHORT index = 0; index < nameLength; ++index)
        entry.name[index] = apFileName->Buffer[index];
    entry.name[nameLength] = L'\0';
    entry.nameLength = nameLength;

    MemoryBarrier();
    InterlockedExchange(&entry.phase, kLdrLoadTraceEntered);

    LdrLoadTraceScope scope{};
    scope.entry = &entry;
    scope.threadState = threadState;
    return scope;
}

void CompleteLdrLoadTrace(const LdrLoadTraceScope& acScope, NTSTATUS aStatus) noexcept
{
    if (!acScope.entry)
        return;

    InterlockedExchange(&acScope.entry->status, static_cast<LONG>(aStatus));
    InterlockedExchange64(&acScope.entry->endTick, static_cast<LONG64>(GetTickCount64()));
    MemoryBarrier();
    InterlockedExchange(&acScope.entry->phase, kLdrLoadTraceReturned);

    if (acScope.threadState)
        InterlockedDecrement(&acScope.threadState->depth);
}

void CopyTraceModuleName(const LdrLoadTraceEntry& acEntry, char* apOutput, size_t aOutputSize) noexcept
{
    if (aOutputSize == 0)
        return;

    const auto length = WideCharToMultiByte(CP_UTF8, 0, acEntry.name, acEntry.nameLength, apOutput, static_cast<int>(aOutputSize - 1), nullptr, nullptr);
    if (length <= 0)
    {
        apOutput[0] = '\0';
        return;
    }

    apOutput[length] = '\0';
}

const char* GetLdrLoadTracePhaseName(LONG aPhase) noexcept
{
    switch (aPhase)
    {
    case kLdrLoadTraceEntered: return "pending";
    case kLdrLoadTraceReturned: return "returned";
    default: return "unstable";
    }
}
#endif

inline bool IsUsingMO2()
{
    return GetModuleHandleW(L"usvfs_x64.dll");
}

bool IsMyModule(HMODULE aHmod)
{
    return aHmod == nullptr || aHmod == NtInternal::ThePeb()->pImageBase;
}

HMODULE HModFromAddress(void* apAddress)
{
    HMODULE pModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(apAddress), &pModule);

    return pModule;
}

DWORD MYGetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
    if (!nSize)
        return 0;

    if (nSize > 0x7FFF)
        nSize = 0x7FFF;

    UNICODE_STRING str{};
    str.Buffer = lpFilename;
    str.MaximumLength = static_cast<USHORT>(sizeof(wchar_t) * nSize - sizeof(wchar_t));

    LdrGetDllFullName(hModule, &str);

    DWORD nChars = str.Length / sizeof(wchar_t);
    str.Buffer[nChars] = 0;
    return nChars;
}

bool IsLocalModulePath(HMODULE aHmod)
{
    std::wstring buf;

    size_t i = 1;
    DWORD res = 0;
    do
    {
        buf.resize(i * MAX_PATH);
        res = MYGetModuleFileNameW(aHmod, buf.data(), static_cast<DWORD>(buf.length()));
        i++;
    } while (res == ERROR_INSUFFICIENT_BUFFER);

    // does the file exist in the ST dir?
    return buf.find(s_OverridePath) != std::wstring::npos;
}

// Some mods do GetModuleHandle("SkyrimVR.exe") for some reason instead of GetModuleHandle(nullptr).
HMODULE WINAPI TP_GetModuleHandleW(LPCWSTR lpModuleName)
{
    constexpr auto pTarget = TARGET_NAME L".exe";
    auto targetSize = std::wcslen(pTarget);

    if (lpModuleName && std::wcsncmp(pTarget, lpModuleName, targetSize) == 0)
        lpModuleName = nullptr;
    return RealGetModuleHandleW(lpModuleName);
}

// Some mods do GetModuleHandle("SkyrimVR.exe") for some reason instead of GetModuleHandle(nullptr).
HMODULE WINAPI TP_GetModuleHandleA(LPCSTR lpModuleName)
{
    constexpr auto pTarget = TARGET_NAME_A ".exe";
    constexpr auto targetSize = sizeof(TARGET_NAME_A ".exe");

    if (lpModuleName && std::strncmp(pTarget, lpModuleName, targetSize) == 0)
        lpModuleName = nullptr;
    return RealGetModuleHandleA(lpModuleName);
}

// Some mods do GetModuleHandle("SkyrimVR.exe") for some reason instead of GetModuleHandle(nullptr).
NTSTATUS WINAPI TP_LdrGetDllHandle(PWSTR DllPath, PULONG DllCharacteristics, PUNICODE_STRING DllName, PVOID* DllHandle)
{
    // no need to check for nullptr here, this is handeled by the higher level GetModuleHandle function.
    TP_EMPTY_HOOK_PLACEHOLDER;

    if (std::wcsncmp(TARGET_NAME L".exe", DllName->Buffer, DllName->Length) == 0)
    {
        *DllHandle = NtInternal::ThePeb()->pImageBase;
        return 0; // success
    }

    return RealLdrGetDllHandle(DllPath, DllCharacteristics, DllName, DllHandle);
}

NTSTATUS WINAPI TP_LdrGetDllHandleEx(ULONG Flags, PWSTR DllPath, PULONG DllCharacteristics, UNICODE_STRING* DllName, PVOID* DllHandle)
{
    if (DllName && std::wcsncmp(TARGET_NAME L".exe", DllName->Buffer, DllName->Length) == 0)
    {
        *DllHandle = NtInternal::ThePeb()->pImageBase;
        return 0; // success
    }

    return RealLdrGetDllHandleEx(Flags, DllPath, DllCharacteristics, DllName, DllHandle);
}

NTSTATUS WINAPI TP_LdrGetDllFullName(HMODULE Module, PUNICODE_STRING DllName)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    void* rsp = _ReturnAddress();

    // we need to detect if GetModuleFileName(null) was called from our own code or the game code/other modules
    // if (!GetTls()->inLoadDll)
    if (launcher::GetLaunchContext())
    {
        if (Module == nullptr || Module == NtInternal::ThePeb()->pImageBase)
        {
            auto& aExePath = launcher::GetLaunchContext()->exePath;
            RtlInitUnicodeString(DllName, aExePath.c_str());
            return 0; // sucess
        }
    }

    return RealLdrGetDllFullName(Module, DllName);
}

DWORD WINAPI TP_GetModuleFileNameW(HMODULE aModule, LPWSTR alpFilename, DWORD aSize)
{
    // trampoline space for USVFS
    TP_EMPTY_HOOK_PLACEHOLDER;

    if (IsMyModule(aModule) && launcher::GetLaunchContext() && launcher::GetLaunchContext()->GetLoaded())
    {
        auto& aExePath = launcher::GetLaunchContext()->exePath;
        StringCchCopyW(alpFilename, aSize, aExePath.c_str());

        return static_cast<DWORD>(std::wcslen(alpFilename));
    }

    return RealGetModuleFileNameW(aModule, alpFilename, aSize);
}

struct ScopedOSHeapItem
{
    ScopedOSHeapItem(size_t aSize) { m_pBlock = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, aSize); }

    ~ScopedOSHeapItem()
    {
        if (m_pBlock)
            HeapFree(GetProcessHeap(), 0, m_pBlock);
    }

    void* m_pBlock;
};

// NOTE(Vince): Introduce one layer of indirection by calling GetModuleFileNameW function directly, to trigger usvfs
// indirection.
DWORD WINAPI TP_GetModuleFileNameA(HMODULE aModule, char* alpFileName, DWORD aBufferSize)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    ScopedOSHeapItem wideBuffer((aBufferSize * sizeof(wchar_t)) + 1);

    wchar_t* pBuffer = static_cast<wchar_t*>(wideBuffer.m_pBlock);

    // Under MO2, there's a bug caused when calling RealGetModuleFileNameW on XAudio2_7.dll,
    // it crashes in usvfs. This only happens during the thread shutdown paths 
    // of quitting, so just avoid the bug. 
    // TODO: Further analysis of what is under MO2 USVFS and what is needed.
    DWORD result = 0;
    HMODULE tMod = RealGetModuleHandleW(L"XAudio2_7.dll");
    if (tMod == nullptr || aModule != tMod)
        result = TP_GetModuleFileNameW(aModule, pBuffer, aBufferSize * sizeof(wchar_t)); // To make sure spoofing happens if needed

    if (result == 0)
    {
        return result;
    }

    UNICODE_STRING source{};
    RtlInitUnicodeString(&source, pBuffer);

    // convert using the proper OS function
    ANSI_STRING dest{.Length = static_cast<USHORT>(result), .MaximumLength = static_cast<USHORT>(aBufferSize), .Buffer = alpFileName};
    if (RtlUnicodeStringToAnsiString(&dest, &source, FALSE) != 0)
        return 0;

    return result;
}

// we use this function to enforce the DLL load policy
NTSTATUS WINAPI TP_LdrLoadDll(const wchar_t* apPath, uint32_t* apFlags, UNICODE_STRING* apFileName, HANDLE* apHandle)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

#if TP_SKYRIM_VR
    const auto traceScope = BeginLdrLoadTrace(apFileName);
#endif

    std::wstring_view fileName(apFileName->Buffer, apFileName->Length / sizeof(wchar_t));
    const size_t pos = fileName.find_last_of(L"\\/");
    const std::wstring_view name = pos != std::wstring_view::npos ? fileName.substr(pos + 1) : fileName;
    if (!name.empty())
    {
        if (stubs::IsSoulsRE(name))
            stubs::g_IsSoulsREActive = true;

        if (stubs::IsHiggs(name))
            stubs::g_IsHiggsActive = true;

        if (stubs::IsPlanck(name))
            stubs::g_IsPlanckActive = true;
        
        if (stubs::IsDllBlocked(name))
        {
            // invalid image hash
            // this signals windows to *NOT TRY* loading it again at a later time.
#if TP_SKYRIM_VR
            CompleteLdrLoadTrace(traceScope, static_cast<NTSTATUS>(0xC0000428L));
#endif
            return 0xC0000428;
        }
    }

    const auto result = RealLdrLoadDll(apPath, apFlags, apFileName, apHandle);
#if TP_SKYRIM_VR
    CompleteLdrLoadTrace(traceScope, result);
#endif
    return result;
}
} // namespace

#if TP_SKYRIM_VR
void stubs::FlushLdrLoadTrace()
{
    const auto nextSequence = InterlockedCompareExchange(&s_ldrLoadTraceSequence, 0, 0);
    if (nextSequence <= 0)
    {
        spdlog::critical("SkyrimTogetherVR LdrLoadDll trace is empty");
        return;
    }

    const auto firstSequence = nextSequence > static_cast<LONG>(kLdrLoadTraceCapacity) ? nextSequence - static_cast<LONG>(kLdrLoadTraceCapacity) : 0;
    if (firstSequence != 0)
        spdlog::critical("SkyrimTogetherVR LdrLoadDll trace retained the most recent {} requests", kLdrLoadTraceCapacity);

    const auto threadSlotOverflow = InterlockedCompareExchange(&s_ldrLoadTraceThreadSlotOverflow, 0, 0);
    if (threadSlotOverflow != 0)
        spdlog::critical("SkyrimTogetherVR LdrLoadDll trace could not assign a depth slot for {} requests", threadSlotOverflow);

    const auto now = static_cast<LONG64>(GetTickCount64());
    for (LONG sequence = firstSequence; sequence < nextSequence; ++sequence)
    {
        auto& entry = s_ldrLoadTrace[static_cast<size_t>(sequence) % kLdrLoadTraceCapacity];
        if (InterlockedCompareExchange(&entry.sequence, 0, 0) != sequence)
            continue;

        const auto phase = InterlockedCompareExchange(&entry.phase, 0, 0);
        if (phase != kLdrLoadTraceEntered && phase != kLdrLoadTraceReturned)
            continue;

        char moduleName[kLdrLoadTraceNameCapacity * 4]{};
        CopyTraceModuleName(entry, moduleName, sizeof(moduleName));

        const auto startTick = InterlockedCompareExchange64(&entry.startTick, 0, 0);
        const auto endTick = phase == kLdrLoadTraceReturned ? InterlockedCompareExchange64(&entry.endTick, 0, 0) : now;
        const auto elapsed = endTick >= startTick ? endTick - startTick : 0;
        const auto status = static_cast<uint32_t>(InterlockedCompareExchange(&entry.status, 0, 0));
        const auto callSite = static_cast<uint64_t>(InterlockedCompareExchange64(&entry.callSite, 0, 0));

        spdlog::critical(
            "SkyrimTogetherVR LdrLoadDll trace: sequence={} thread={} depth={} phase={} status=0x{:08X} elapsedMs={} "
            "callsite=0x{:016X} module={}",
            sequence, entry.threadId, entry.depth, GetLdrLoadTracePhaseName(phase), status, elapsed, callSite, moduleName[0] ? moduleName : "<unnamed>");
    }
}
#endif

#define VALIDATE(x) \
    if (x != MH_OK) \
        Die(L"CoreStubsInit(): Fatal Minhook error.", true);

// pre eat hook?? loadmodule hook??
// the idea would be to link against an external dll, which in its init routine then hooks, so we execute before mo2?
void CoreStubsInit()
{
    s_OverridePath = TiltedPhoques::GetPath().wstring();
    std::replace(s_OverridePath.begin(), s_OverridePath.end(), L'/', L'\\');

    VALIDATE(MH_Initialize());

    if (!IsUsingMO2())
    {
        // we need two hooks here, even if this is kinda redundant, since we want to capture the RBP register
        // to detect if its game code, or launcher code being executed
        VALIDATE(MH_CreateHookApi(L"KernelBase.dll", "GetModuleFileNameW", &TP_GetModuleFileNameW, (void**)&RealGetModuleFileNameW));
        VALIDATE(MH_CreateHookApi(L"KernelBase.dll", "GetModuleFileNameA", &TP_GetModuleFileNameA, (void**)&RealGetModuleFileNameA));
    }
    else
    {
        // https://github.com/ModOrganizer2/usvfs/blob/master/src/usvfs_dll/hooks/kernel32.h#L42
        VALIDATE(MH_CreateHookApi(L"usvfs_x64.dll", "?hook_GetModuleFileNameW@usvfs@@YAKPEAUHINSTANCE__@@PEA_WK@Z", &TP_GetModuleFileNameW, (void**)&RealGetModuleFileNameW));
        VALIDATE(MH_CreateHookApi(L"KernelBase.dll", "GetModuleFileNameA", &TP_GetModuleFileNameA, (void**)&RealGetModuleFileNameA));
    }

    // SKSE calls
    // SKSEVR's runtime lookup can ask for the executable module by name.

    // VALIDATE(MH_CreateHookApi(L"ntdll.dll", "LdrGetDllHandle", &TP_LdrGetDllHandle, (void**)&RealLdrGetDllHandle));
    VALIDATE(MH_CreateHookApi(L"ntdll.dll", "LdrGetDllHandleEx", &TP_LdrGetDllHandleEx, (void**)&RealLdrGetDllHandleEx));

    // TODO(Vince): we need some check if usvfs already fucked with this?
    // MH_CreateHookApi(L"ntdll.dll", "LdrGetDllFullName", &TP_LdrGetDllFullName, (void**)&RealLdrGetDllFullName);
    VALIDATE(MH_CreateHookApi(L"ntdll.dll", "LdrLoadDll", &TP_LdrLoadDll, (void**)&RealLdrLoadDll));

    // Starting with Windows 11 24H2 the call stack has changed and GetModuleHandle() no longer
    // downcalls to LdrGetDllHandleEx, so we have to hook this too.
    VALIDATE(MH_CreateHookApi(L"kernel32.dll", "GetModuleHandleW", &TP_GetModuleHandleW, (void**)&RealGetModuleHandleW));
    VALIDATE(MH_CreateHookApi(L"kernel32.dll", "GetModuleHandleA", &TP_GetModuleHandleA, (void**)&RealGetModuleHandleA));

    VALIDATE(MH_EnableHook(nullptr));
}

#undef VALIDATE
