#include <TiltedOnlinePCH.h>

#include "VRTickBridge.h"

#include <cstring>

#include <TiltedOnlineApp.h>
#include <vr_common/VRTickBridge.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

namespace SkyrimTogetherVR::TickBridge
{
namespace
{
HANDLE s_mapping = nullptr;
Endpoint* s_endpoint = nullptr;
volatile LONG s_dispatchInProgress = 0;
bool s_loggedFirstDispatch = false;

LONG ReadState(const Endpoint& acEndpoint) noexcept
{
    return InterlockedCompareExchange(const_cast<volatile LONG*>(reinterpret_cast<const volatile LONG*>(&acEndpoint.State)), 0, 0);
}

void WriteState(Endpoint& arEndpoint, EndpointState aState) noexcept
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&arEndpoint.State), static_cast<LONG>(aState));
}

std::uint64_t MakeEpoch() noexcept
{
    return (static_cast<std::uint64_t>(GetTickCount64()) << 16u) ^ static_cast<std::uint64_t>(GetCurrentProcessId());
}
} // namespace

bool Initialize() noexcept
{
#if !TP_SKYRIM_VR
    return true;
#else
    if (s_endpoint)
        return true;

    s_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Endpoint), nullptr);
    if (!s_mapping)
    {
        spdlog::critical("SkyrimTogetherVR tick bridge could not create its process-local endpoint mapping: {}", GetLastError());
        return false;
    }

    s_endpoint = static_cast<Endpoint*>(MapViewOfFile(s_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Endpoint)));
    if (!s_endpoint)
    {
        spdlog::critical("SkyrimTogetherVR tick bridge could not map its process-local endpoint: {}", GetLastError());
        CloseHandle(s_mapping);
        s_mapping = nullptr;
        return false;
    }

    const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto callbackAddress = reinterpret_cast<std::uintptr_t>(&Dispatch);
    if (!imageBase || callbackAddress < imageBase)
    {
        spdlog::critical("SkyrimTogetherVR tick bridge could not derive a launcher-relative callback address");
        UnmapViewOfFile(s_endpoint);
        CloseHandle(s_mapping);
        s_endpoint = nullptr;
        s_mapping = nullptr;
        return false;
    }

    std::memset(s_endpoint, 0, sizeof(*s_endpoint));
    s_endpoint->Magic = kEndpointMagic;
    s_endpoint->AbiVersion = kEndpointAbiVersion;
    s_endpoint->StructSize = static_cast<std::uint16_t>(sizeof(Endpoint));
    s_endpoint->PublisherProcessId = GetCurrentProcessId();
    s_endpoint->Epoch = MakeEpoch();
    s_endpoint->ImageBase = imageBase;
    s_endpoint->CallbackRva = callbackAddress - imageBase;
    WriteState(*s_endpoint, EndpointState::Prepared);

    wchar_t handleText[2 + sizeof(std::uintptr_t) * 2 + 1]{};
    const auto length = _snwprintf_s(handleText, _countof(handleText), _TRUNCATE, L"0x%llX", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(s_mapping)));
    if (length < 0 || !SetEnvironmentVariableW(kMappingHandleEnvironment, handleText))
    {
        spdlog::critical("SkyrimTogetherVR tick bridge could not publish its process-local endpoint handle: {}", GetLastError());
        WriteState(*s_endpoint, EndpointState::Faulted);
        return false;
    }

    spdlog::info("SkyrimTogetherVR tick bridge endpoint prepared: epoch={} callbackRva={:x}", s_endpoint->Epoch, s_endpoint->CallbackRva);
    return true;
#endif
}

void Activate() noexcept
{
#if TP_SKYRIM_VR
    if (!s_endpoint || ReadState(*s_endpoint) != static_cast<LONG>(EndpointState::Prepared))
    {
        spdlog::error("SkyrimTogetherVR tick bridge endpoint was not prepared when client startup completed");
        return;
    }

    s_endpoint->ReadyThreadId = GetCurrentThreadId();
    WriteState(*s_endpoint, EndpointState::Ready);
    spdlog::info("SkyrimTogetherVR tick bridge endpoint ready on thread {}", s_endpoint->ReadyThreadId);
#endif
}

void Retire() noexcept
{
#if TP_SKYRIM_VR
    if (s_endpoint)
    {
        WriteState(*s_endpoint, EndpointState::Retired);
        spdlog::info("SkyrimTogetherVR tick bridge endpoint retired before client teardown");
    }
#endif
}

std::uint32_t __cdecl Dispatch(std::uint64_t aEpoch) noexcept
{
#if !TP_SKYRIM_VR
    return static_cast<std::uint32_t>(DispatchResult::Inactive);
#else
    if (!s_endpoint || ReadState(*s_endpoint) != static_cast<LONG>(EndpointState::Ready))
        return static_cast<std::uint32_t>(DispatchResult::Inactive);
    if (s_endpoint->Epoch != aEpoch)
        return static_cast<std::uint32_t>(DispatchResult::InvalidEpoch);
    if (s_endpoint->ReadyThreadId != GetCurrentThreadId())
        return static_cast<std::uint32_t>(DispatchResult::WrongThread);
    if (InterlockedCompareExchange(&s_dispatchInProgress, 1, 0) != 0)
        return static_cast<std::uint32_t>(DispatchResult::Reentrant);
    if (!g_appInstance)
    {
        InterlockedExchange(&s_dispatchInProgress, 0);
        return static_cast<std::uint32_t>(DispatchResult::MissingClient);
    }

    const auto start = GetTickCount64();
    g_appInstance->Update();
    const auto elapsed = GetTickCount64() - start;
    InterlockedExchange(&s_dispatchInProgress, 0);

    if (!s_loggedFirstDispatch)
    {
        s_loggedFirstDispatch = true;
        spdlog::info("SkyrimTogetherVR SKSE task tick dispatched World::Update on thread {}", GetCurrentThreadId());
    }
    if (elapsed > 10)
        spdlog::warn("SkyrimTogetherVR SKSE task tick took {} ms", elapsed);

    return static_cast<std::uint32_t>(DispatchResult::Success);
#endif
}
} // namespace SkyrimTogetherVR::TickBridge
