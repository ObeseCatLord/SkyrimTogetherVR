#include <TiltedOnlinePCH.h>

#include "VRTickBridge.h"

#include <cstring>
#include <limits>

#include <VR/VRBodyPoseCapture.h>
#include <vr_common/VRTickBridge.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE
#define TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE 0
#endif

namespace SkyrimTogetherVR::TickBridge
{
namespace
{
HANDLE s_mapping = nullptr;
Endpoint* s_endpoint = nullptr;
volatile LONG s_updatePermit = 0;
volatile LONG64 s_lastDispatchSequence = 0;
volatile LONG64 s_lastBodyCaptureSequence = 0;
volatile LONG s_bodyCaptureInProgress = 0;
bool s_loggedFirstCallback = false;

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
    std::uintptr_t bodyCaptureCallbackAddress = 0;
#if TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE
    bodyCaptureCallbackAddress = reinterpret_cast<std::uintptr_t>(&CaptureBodyPose);
#endif
    if (!imageBase || callbackAddress < imageBase || (bodyCaptureCallbackAddress && bodyCaptureCallbackAddress < imageBase))
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
    s_endpoint->BodyCaptureCallbackRva = bodyCaptureCallbackAddress ? bodyCaptureCallbackAddress - imageBase : 0;
    WriteState(*s_endpoint, EndpointState::Prepared);

    wchar_t handleText[2 + sizeof(std::uintptr_t) * 2 + 1]{};
    const auto length = _snwprintf_s(handleText, _countof(handleText), _TRUNCATE, L"0x%llX", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(s_mapping)));
    if (length < 0 || !SetEnvironmentVariableW(kMappingHandleEnvironment, handleText))
    {
        spdlog::critical("SkyrimTogetherVR tick bridge could not publish its process-local endpoint handle: {}", GetLastError());
        WriteState(*s_endpoint, EndpointState::Faulted);
        UnmapViewOfFile(s_endpoint);
        CloseHandle(s_mapping);
        s_endpoint = nullptr;
        s_mapping = nullptr;
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

#if TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE
    if (!BodyPoseCapture::Activate())
        spdlog::warn("SkyrimTogetherVR body-pose capture names could not be initialized");
#endif

    s_endpoint->ActivationThreadId = GetCurrentThreadId();
    WriteState(*s_endpoint, EndpointState::Ready);
    spdlog::info("SkyrimTogetherVR tick bridge endpoint ready on activation thread {}", s_endpoint->ActivationThreadId);
#endif
}

void Retire() noexcept
{
#if TP_SKYRIM_VR
    if (s_endpoint)
    {
        WriteState(*s_endpoint, EndpointState::Retired);
        InterlockedExchange(&s_updatePermit, 0);
#if TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE
        BodyPoseCapture::Retire();
#endif
        spdlog::info("SkyrimTogetherVR tick bridge endpoint retired before client teardown");
    }
#endif
}

std::uint32_t GetActivationThreadId() noexcept
{
#if !TP_SKYRIM_VR
    return 0;
#else
    if (!s_endpoint || ReadState(*s_endpoint) != static_cast<LONG>(EndpointState::Ready))
        return 0;

    return s_endpoint->ActivationThreadId;
#endif
}

bool ConsumeUpdatePermit() noexcept
{
#if !TP_SKYRIM_VR
    return false;
#else
    return InterlockedExchange(&s_updatePermit, 0) != 0;
#endif
}

std::uint32_t __cdecl Dispatch(std::uint64_t aEpoch, std::uint64_t aSequence, std::uint32_t aExecutorThreadId) noexcept
{
#if !TP_SKYRIM_VR
    return static_cast<std::uint32_t>(DispatchResult::Inactive);
#else
    if (!s_endpoint || ReadState(*s_endpoint) != static_cast<LONG>(EndpointState::Ready))
        return static_cast<std::uint32_t>(DispatchResult::Inactive);
    if (s_endpoint->Epoch != aEpoch)
        return static_cast<std::uint32_t>(DispatchResult::InvalidEpoch);
    const auto currentThreadId = GetCurrentThreadId();
    if (aExecutorThreadId == 0 || aExecutorThreadId != currentThreadId)
        return static_cast<std::uint32_t>(DispatchResult::WrongThread);
    if (aSequence == 0 || aSequence > static_cast<std::uint64_t>(std::numeric_limits<LONG64>::max()))
    {
        WriteState(*s_endpoint, EndpointState::Faulted);
        return static_cast<std::uint32_t>(DispatchResult::InvalidSequence);
    }

    const auto sequence = static_cast<LONG64>(aSequence);
    const auto expectedPrevious = sequence - 1;
    if (InterlockedCompareExchange64(&s_lastDispatchSequence, sequence, expectedPrevious) != expectedPrevious)
    {
        WriteState(*s_endpoint, EndpointState::Faulted);
        return static_cast<std::uint32_t>(DispatchResult::InvalidSequence);
    }
    if (!s_loggedFirstCallback)
    {
        s_loggedFirstCallback = true;
        spdlog::info("SkyrimTogetherVR SKSE task callback accepted: sequence={} thread={}", aSequence, currentThreadId);
    }
    InterlockedExchange(&s_updatePermit, 1);

    return static_cast<std::uint32_t>(DispatchResult::Success);
#endif
}

std::uint32_t __cdecl CaptureBodyPose(std::uint64_t aEpoch, std::uint64_t aSequence, std::uint32_t aExecutorThreadId) noexcept
{
#if !TP_SKYRIM_VR || !TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE
    return static_cast<std::uint32_t>(DispatchResult::Inactive);
#else
    if (!s_endpoint || ReadState(*s_endpoint) != static_cast<LONG>(EndpointState::Ready))
        return static_cast<std::uint32_t>(DispatchResult::Inactive);
    if (s_endpoint->Epoch != aEpoch)
        return static_cast<std::uint32_t>(DispatchResult::InvalidEpoch);
    if (aExecutorThreadId == 0 || aExecutorThreadId != GetCurrentThreadId())
        return static_cast<std::uint32_t>(DispatchResult::WrongThread);
    if (aSequence == 0 || aSequence > static_cast<std::uint64_t>(std::numeric_limits<LONG64>::max()))
        return static_cast<std::uint32_t>(DispatchResult::InvalidSequence);

    const auto sequence = static_cast<LONG64>(aSequence);
    auto previous = InterlockedCompareExchange64(&s_lastBodyCaptureSequence, 0, 0);
    while (sequence > previous)
    {
        const auto observed = InterlockedCompareExchange64(&s_lastBodyCaptureSequence, sequence, previous);
        if (observed == previous)
            break;
        previous = observed;
    }
    if (sequence <= previous)
        return static_cast<std::uint32_t>(DispatchResult::InvalidSequence);
    if (InterlockedCompareExchange(&s_bodyCaptureInProgress, 1, 0) != 0)
        return static_cast<std::uint32_t>(DispatchResult::Reentrant);

    const bool published = BodyPoseCapture::CaptureFromPostHiggs(aSequence);
    InterlockedExchange(&s_bodyCaptureInProgress, 0);
    return static_cast<std::uint32_t>(published ? DispatchResult::Success : DispatchResult::Contended);
#endif
}
} // namespace SkyrimTogetherVR::TickBridge
