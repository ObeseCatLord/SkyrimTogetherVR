#include "LegacySksePrefix.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>

#include <skse64/PluginAPI.h>
#include <skse64/PapyrusNativeFunctions.h>
#include <skse64/gamethreads.h>
#include <skse64_common/skse_version.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <vr_common/VRTickBridge.h>

// The official legacy SKSEVR SDK declares these primitive specializations in
// PapyrusArgs.cpp. Keep the exact small subset used by this bridge local so it
// does not link the SDK's broad argument implementation and its unrelated
// globals.
template <> void PackValue<bool>(VMValue* apDestination, bool* apSource, VMClassRegistry*)
{
    apDestination->SetBool(*apSource);
}

template <> void UnpackValue<SInt32>(SInt32* apDestination, VMValue* apSource)
{
    switch (apSource->type)
    {
    case VMValue::kType_Int:
        *apDestination = apSource->data.u;
        break;
    case VMValue::kType_Float:
        *apDestination = apSource->data.f;
        break;
    case VMValue::kType_Bool:
        *apDestination = apSource->data.b;
        break;
    default:
        *apDestination = 0;
        break;
    }
}

template <> UInt64 GetTypeID<bool>(VMClassRegistry*)
{
    return VMValue::kType_Bool;
}

template <> UInt64 GetTypeID<SInt32>(VMClassRegistry*)
{
    return VMValue::kType_Int;
}

// NativeFunction owns four StringCache::Ref members. This is the exact
// constructor used by the pinned SKSEVR GameTypes.cpp, whose fixed address is
// valid only after the explicit VR runtime gate below.
StringCache::Ref::Ref()
{
    CALL_MEMBER_FN(this, ctor)("");
}

// NativeFunction0's generic Run implementation emits this symbol even for a
// StaticFunctionTag. Its static-type branch never executes the handle path, so
// an inert implementation keeps this bridge independent of PapyrusArgs.cpp and
// its unvalidated object-handle globals.
void* UnpackHandle(VMValue*, UInt32)
{
    return nullptr;
}

namespace
{
using SkyrimTogetherVR::TickBridge::DispatchCallback;
using SkyrimTogetherVR::TickBridge::Endpoint;
using SkyrimTogetherVR::TickBridge::EndpointState;

constexpr char kPluginName[] = "SkyrimTogetherVRTickBridge";
constexpr char kPapyrusClass[] = "SkyrimTogetherVRTickBridge";

SKSETaskInterface* g_taskInterface = nullptr;
volatile LONG g_taskQueued = 0;
volatile LONG g_mappingFaulted = 0;
volatile LONG g_tickCallCount = 0;
volatile LONG g_taskEnqueueCount = 0;
volatile LONG g_taskRunCount = 0;
volatile LONG g_cadenceOwner = 0;
volatile LONG64 g_lastRateLimitedLogAt = 0;
const Endpoint* g_endpoint = nullptr;

bool IsSupportedRuntime(const SKSEInterface* apSkse) noexcept
{
    return apSkse && !apSkse->isEditor && apSkse->runtimeVersion == RUNTIME_VR_VERSION_1_4_15 && apSkse->skseVersion >= PACKED_SKSE_VERSION &&
           apSkse->GetReleaseIndex && apSkse->GetReleaseIndex() >= SKSE_VERSION_RELEASEIDX;
}

bool ShouldWriteRateLimitedLog() noexcept
{
    constexpr ULONGLONG kLogIntervalMilliseconds = 5'000;
    const auto now = GetTickCount64();
    const auto previous = static_cast<ULONGLONG>(InterlockedCompareExchange64(&g_lastRateLimitedLogAt, 0, 0));
    if (now < previous || now - previous < kLogIntervalMilliseconds)
        return false;

    return InterlockedCompareExchange64(&g_lastRateLimitedLogAt, static_cast<LONG64>(now), static_cast<LONG64>(previous)) == static_cast<LONG64>(previous);
}

void LogDebug(const char* apMessage) noexcept
{
    OutputDebugStringA(apMessage);
    OutputDebugStringA("\n");

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LogDebug),
            &module))
        return;

    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, path, _countof(path));
    if (length == 0 || length >= _countof(path))
        return;

    auto* const leaf = std::wcsrchr(path, L'\\');
    if (!leaf || (leaf - path) + 1 >= static_cast<ptrdiff_t>(_countof(path)))
        return;

    constexpr wchar_t kLogName[] = L"SkyrimTogetherVRTickBridge.log";
    if (wcscpy_s(leaf + 1, _countof(path) - static_cast<size_t>((leaf - path) + 1), kLogName) != 0)
        return;

    char line[640]{};
    const auto count = _snprintf_s(line, _countof(line), _TRUNCATE, "[%llu] %s\r\n", static_cast<unsigned long long>(GetTickCount64()), apMessage);
    if (count <= 0)
        return;

    const auto handle = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(handle, line, static_cast<DWORD>(count), &written, nullptr);
    CloseHandle(handle);
}

LONG ReadState(const Endpoint& acEndpoint) noexcept
{
    return InterlockedCompareExchange(const_cast<volatile LONG*>(reinterpret_cast<const volatile LONG*>(&acEndpoint.State)), 0, 0);
}

bool IsExecutableReadOnlyPage(DWORD aProtection) noexcept
{
    if ((aProtection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const auto baseProtection = aProtection & 0xffu;
    return baseProtection == PAGE_EXECUTE || baseProtection == PAGE_EXECUTE_READ;
}

bool ParseMappingHandle(HANDLE& arHandle) noexcept
{
    wchar_t text[2 + sizeof(std::uintptr_t) * 2 + 1]{};
    const auto length = GetEnvironmentVariableW(SkyrimTogetherVR::TickBridge::kMappingHandleEnvironment, text, _countof(text));
    if (length < 3 || length >= _countof(text) || text[0] != L'0' || (text[1] != L'x' && text[1] != L'X'))
        return false;

    wchar_t* end = nullptr;
    const auto value = std::wcstoull(text + 2, &end, 16);
    if (!end || *end != L'\0' || value == 0 || value > std::numeric_limits<std::uintptr_t>::max())
        return false;

    arHandle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
    return true;
}

bool MapEndpoint() noexcept
{
    if (g_endpoint)
        return true;
    if (InterlockedCompareExchange(&g_mappingFaulted, 0, 0) != 0)
        return false;

    HANDLE mapping = nullptr;
    if (!ParseMappingHandle(mapping))
    {
        LogDebug("SkyrimTogetherVRTickBridge: missing or malformed endpoint handle");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }

    auto* endpoint = static_cast<const Endpoint*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Endpoint)));
    if (!endpoint)
    {
        LogDebug("SkyrimTogetherVRTickBridge: endpoint map failed");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }

    g_endpoint = endpoint;
    return true;
}

bool ValidateReadyEndpoint(const Endpoint& acEndpoint, DispatchCallback& arCallback) noexcept
{
    if (acEndpoint.Magic != SkyrimTogetherVR::TickBridge::kEndpointMagic || acEndpoint.AbiVersion != SkyrimTogetherVR::TickBridge::kEndpointAbiVersion ||
        acEndpoint.StructSize != sizeof(Endpoint) || acEndpoint.PublisherProcessId != GetCurrentProcessId())
    {
        LogDebug("SkyrimTogetherVRTickBridge: endpoint ABI validation failed");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }

    if (acEndpoint.Reserved0 != 0)
    {
        LogDebug("SkyrimTogetherVRTickBridge: endpoint reserved fields are non-zero");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }

    for (const auto reserved : acEndpoint.Reserved)
    {
        if (reserved != 0)
        {
            LogDebug("SkyrimTogetherVRTickBridge: endpoint reserved fields are non-zero");
            InterlockedExchange(&g_mappingFaulted, 1);
            return false;
        }
    }

    const auto state = static_cast<EndpointState>(ReadState(acEndpoint));
    if (state != EndpointState::Ready)
        return false;
    if (!acEndpoint.ImageBase || !acEndpoint.CallbackRva || acEndpoint.CallbackRva > std::numeric_limits<std::uintptr_t>::max() - acEndpoint.ImageBase)
    {
        LogDebug("SkyrimTogetherVRTickBridge: endpoint callback range is invalid");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }
    if (acEndpoint.ImageBase != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))
    {
        LogDebug("SkyrimTogetherVRTickBridge: endpoint image base does not match this process");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }
    if (acEndpoint.ReadyThreadId != GetCurrentThreadId())
    {
        if (ShouldWriteRateLimitedLog())
            LogDebug("SkyrimTogetherVRTickBridge: task rejected reason=ready_thread_mismatch");
        return false;
    }

    const auto callbackAddress = acEndpoint.ImageBase + acEndpoint.CallbackRva;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(callbackAddress), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        memory.AllocationBase != reinterpret_cast<void*>(acEndpoint.ImageBase) || !IsExecutableReadOnlyPage(memory.Protect))
    {
        LogDebug("SkyrimTogetherVRTickBridge: endpoint callback page validation failed");
        InterlockedExchange(&g_mappingFaulted, 1);
        return false;
    }

    arCallback = reinterpret_cast<DispatchCallback>(callbackAddress);
    return true;
}

class ClientUpdateTask final : public TaskDelegate
{
public:
    void Run() override
    {
        const auto runCount = InterlockedIncrement(&g_taskRunCount);
        if (!g_endpoint)
        {
            if (ShouldWriteRateLimitedLog())
                LogDebug("SkyrimTogetherVRTickBridge: task rejected reason=missing_endpoint");
            return;
        }

        DispatchCallback callback = nullptr;
        if (!ValidateReadyEndpoint(*g_endpoint, callback))
            return;

        const auto dispatchResult = static_cast<SkyrimTogetherVR::TickBridge::DispatchResult>(callback(g_endpoint->Epoch));
        if (runCount == 1 || dispatchResult != SkyrimTogetherVR::TickBridge::DispatchResult::Success || ShouldWriteRateLimitedLog())
        {
            char message[192]{};
            _snprintf_s(
                message,
                _countof(message),
                _TRUNCATE,
                "SkyrimTogetherVRTickBridge: task_run=%ld dispatch_result=%u thread=%lu",
                runCount,
                static_cast<unsigned int>(dispatchResult),
                static_cast<unsigned long>(GetCurrentThreadId()));
            LogDebug(message);
        }
    }

    void Dispose() override
    {
        InterlockedExchange(&g_taskQueued, 0);
        delete this;
    }
};

bool QueueClientUpdate() noexcept
{
    if (!g_taskInterface || !MapEndpoint())
        return false;
    if (InterlockedCompareExchange(&g_mappingFaulted, 0, 0) != 0)
        return false;
    if (InterlockedCompareExchange(&g_taskQueued, 1, 0) != 0)
        return true;

    auto* const task = new (std::nothrow) ClientUpdateTask();
    if (!task)
    {
        InterlockedExchange(&g_taskQueued, 0);
        LogDebug("SkyrimTogetherVRTickBridge: task allocation failed");
        return false;
    }

    g_taskInterface->AddTask(task);
    const auto enqueueCount = InterlockedIncrement(&g_taskEnqueueCount);
    if (enqueueCount == 1 || (enqueueCount % 100) == 0)
    {
        char message[128]{};
        _snprintf_s(message, _countof(message), _TRUNCATE, "SkyrimTogetherVRTickBridge: task_enqueued=%ld", enqueueCount);
        LogDebug(message);
    }
    return true;
}

bool PapyrusTick(StaticFunctionTag*)
{
    const auto tickCount = InterlockedIncrement(&g_tickCallCount);
    if (tickCount == 1 || (tickCount % 100) == 0)
    {
        char message[128]{};
        _snprintf_s(message, _countof(message), _TRUNCATE, "SkyrimTogetherVRTickBridge: papyrus_tick=%ld", tickCount);
        LogDebug(message);
    }

    const auto queued = QueueClientUpdate();
    if (!queued && ShouldWriteRateLimitedLog())
        LogDebug("SkyrimTogetherVRTickBridge: papyrus_tick rejected");
    return queued;
}

bool PapyrusClaimCadence(StaticFunctionTag*, SInt32 aOwner)
{
    if (aOwner <= 0 || aOwner > 2)
    {
        LogDebug("SkyrimTogetherVRTickBridge: cadence claim rejected reason=invalid_owner");
        return false;
    }

    const auto owner = static_cast<LONG>(aOwner);
    const auto previous = InterlockedCompareExchange(&g_cadenceOwner, owner, 0);
    if (previous != 0 && previous != owner)
        return false;

    if (previous == 0)
    {
        char message[128]{};
        _snprintf_s(message, _countof(message), _TRUNCATE, "SkyrimTogetherVRTickBridge: papyrus_cadence_owner=%ld", owner);
        LogDebug(message);
    }
    return true;
}

bool PapyrusArmOnInit(StaticFunctionTag*)
{
    LogDebug("SkyrimTogetherVRTickBridge: papyrus_arm=OnInit");
    return g_taskInterface != nullptr;
}

bool PapyrusArmOnPlayerLoadGame(StaticFunctionTag*)
{
    LogDebug("SkyrimTogetherVRTickBridge: papyrus_arm=OnPlayerLoadGame");
    return g_taskInterface != nullptr;
}

bool RegisterPapyrusFunctions(VMClassRegistry* apRegistry)
{
    if (!apRegistry)
        return false;

    apRegistry->RegisterFunction(new NativeFunction0<StaticFunctionTag, bool>("Tick", kPapyrusClass, PapyrusTick, apRegistry));
    apRegistry->RegisterFunction(
        new NativeFunction1<StaticFunctionTag, bool, SInt32>("ClaimCadence", kPapyrusClass, PapyrusClaimCadence, apRegistry));
    apRegistry->RegisterFunction(new NativeFunction0<StaticFunctionTag, bool>("ArmOnInit", kPapyrusClass, PapyrusArmOnInit, apRegistry));
    apRegistry->RegisterFunction(
        new NativeFunction0<StaticFunctionTag, bool>("ArmOnPlayerLoadGame", kPapyrusClass, PapyrusArmOnPlayerLoadGame, apRegistry));
    LogDebug("SkyrimTogetherVRTickBridge: Papyrus registration callback completed");
    return true;
}
} // namespace

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSEInterface* apSkse, PluginInfo* apInfo)
{
    if (!apInfo)
        return false;

    apInfo->infoVersion = PluginInfo::kInfoVersion;
    apInfo->name = kPluginName;
    apInfo->version = 1;

    if (!IsSupportedRuntime(apSkse))
    {
        LogDebug("SkyrimTogetherVRTickBridge: requires Skyrim VR 1.4.15 and SKSEVR 2.0.12 or newer");
        return false;
    }

    return true;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!IsSupportedRuntime(apSkse) || !apSkse->QueryInterface)
        return false;

    g_taskInterface = static_cast<SKSETaskInterface*>(apSkse->QueryInterface(kInterface_Task));
    const auto* papyrus = static_cast<SKSEPapyrusInterface*>(apSkse->QueryInterface(kInterface_Papyrus));
    if (!g_taskInterface || g_taskInterface->interfaceVersion < SKSETaskInterface::kInterfaceVersion || !papyrus ||
        papyrus->interfaceVersion < SKSEPapyrusInterface::kInterfaceVersion || !papyrus->Register)
    {
        LogDebug("SkyrimTogetherVRTickBridge: required SKSEVR task or Papyrus interface is unavailable");
        return false;
    }

    if (!papyrus->Register(RegisterPapyrusFunctions))
    {
        LogDebug("SkyrimTogetherVRTickBridge: Papyrus registration failed");
        return false;
    }

    LogDebug("SkyrimTogetherVRTickBridge: SKSEVR task and Papyrus bridge registered");
    return true;
}
