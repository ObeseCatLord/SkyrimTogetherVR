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
alignas(8) volatile LONG64 s_lastDispatchSequence = 0;
alignas(8) volatile LONG64 s_pendingDispatchSequence = 0;
alignas(8) volatile LONG64 s_lastConsumedSequence = 0;
alignas(8) volatile LONG64 s_permitPendingSinceMs = 0;
alignas(8) volatile LONG64 s_ownerHeartbeatMs = 0;
alignas(8) volatile LONG64 s_ownerUpdateCompletedMs = 0;
alignas(8) volatile LONG64 s_lastStarvationWarningMs = 0;
alignas(8) volatile LONG64 s_lastBodyCaptureSequence = 0;
volatile LONG s_bodyCaptureInProgress = 0;
volatile LONG s_loggedFirstCallback = 0;

constexpr std::uint64_t kOwnerStarvationWarningMs = 5000;
constexpr std::uint64_t kOwnerStarvationLogIntervalMs = 10000;

LONG ReadState(const Endpoint& acEndpoint) noexcept
{
    return InterlockedCompareExchange(const_cast<volatile LONG*>(reinterpret_cast<const volatile LONG*>(&acEndpoint.State)), 0, 0);
}

void WriteState(Endpoint& arEndpoint, EndpointState aState) noexcept
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&arEndpoint.State), static_cast<LONG>(aState));
}

std::uint64_t ReadCounter(volatile LONG64& arCounter) noexcept
{
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(&arCounter, 0, 0));
}

void WriteCounter(volatile LONG64& arCounter, std::uint64_t aValue) noexcept
{
    InterlockedExchange64(&arCounter, static_cast<LONG64>(aValue));
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
    WriteCounter(s_lastDispatchSequence, 0);
    WriteCounter(s_pendingDispatchSequence, 0);
    WriteCounter(s_lastConsumedSequence, 0);
    WriteCounter(s_permitPendingSinceMs, 0);
    WriteCounter(s_ownerHeartbeatMs, 0);
    WriteCounter(s_ownerUpdateCompletedMs, 0);
    WriteCounter(s_lastStarvationWarningMs, 0);
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
    WriteCounter(s_ownerHeartbeatMs, GetTickCount64());
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
        WriteCounter(s_pendingDispatchSequence, 0);
        WriteCounter(s_permitPendingSinceMs, 0);
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

Diagnostics GetDiagnostics() noexcept
{
#if !TP_SKYRIM_VR
    return {};
#else
    Diagnostics diagnostics{};
    if (!s_endpoint)
        return diagnostics;

    diagnostics.Ready = ReadState(*s_endpoint) == static_cast<LONG>(EndpointState::Ready);
    diagnostics.ActivationThreadId = diagnostics.Ready ? s_endpoint->ActivationThreadId : 0;
    diagnostics.ProducedSequence = ReadCounter(s_lastDispatchSequence);
    diagnostics.ConsumedSequence = ReadCounter(s_lastConsumedSequence);
    diagnostics.PermitPendingSinceMs = ReadCounter(s_permitPendingSinceMs);
    diagnostics.OwnerHeartbeatMs = ReadCounter(s_ownerHeartbeatMs);
    diagnostics.OwnerUpdateCompletedMs = ReadCounter(s_ownerUpdateCompletedMs);
    diagnostics.PermitPending = ReadCounter(s_pendingDispatchSequence) != 0;
    return diagnostics;
#endif
}

void RecordOwnerHeartbeat() noexcept
{
#if TP_SKYRIM_VR
    if (s_endpoint && ReadState(*s_endpoint) == static_cast<LONG>(EndpointState::Ready))
        WriteCounter(s_ownerHeartbeatMs, GetTickCount64());
#endif
}

void RecordOwnerUpdateCompleted(std::uint64_t aSequence) noexcept
{
#if TP_SKYRIM_VR
    WriteCounter(s_lastConsumedSequence, aSequence);
    WriteCounter(s_ownerUpdateCompletedMs, GetTickCount64());
#else
    (void)aSequence;
#endif
}

bool TryConsumeUpdatePermit(std::uint64_t& arSequence) noexcept
{
    arSequence = 0;
#if !TP_SKYRIM_VR
    return false;
#else
    if (!s_endpoint || ReadState(*s_endpoint) != static_cast<LONG>(EndpointState::Ready))
        return false;

    const auto sequence = InterlockedExchange64(&s_pendingDispatchSequence, 0);
    if (sequence <= 0)
        return false;

    arSequence = static_cast<std::uint64_t>(sequence);
    if (ReadCounter(s_pendingDispatchSequence) == 0)
        WriteCounter(s_permitPendingSinceMs, 0);
    return true;
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
    if (InterlockedCompareExchange(&s_loggedFirstCallback, 1, 0) == 0)
    {
        spdlog::info("SkyrimTogetherVR SKSE task callback accepted: sequence={} thread={}", aSequence, currentThreadId);
    }
    const auto now = GetTickCount64();
    const auto previousPending = InterlockedExchange64(&s_pendingDispatchSequence, sequence);
    if (previousPending == 0)
        InterlockedCompareExchange64(&s_permitPendingSinceMs, static_cast<LONG64>(now), 0);

    const auto ownerHeartbeat = ReadCounter(s_ownerHeartbeatMs);
    const auto ownerAge = ownerHeartbeat > 0 && now >= ownerHeartbeat ? now - ownerHeartbeat : 0;
    if (ownerHeartbeat > 0 && ownerAge >= kOwnerStarvationWarningMs)
    {
        const auto lastWarning = ReadCounter(s_lastStarvationWarningMs);
        if (now >= lastWarning + kOwnerStarvationLogIntervalMs &&
            InterlockedCompareExchange64(&s_lastStarvationWarningMs, static_cast<LONG64>(now), static_cast<LONG64>(lastWarning)) ==
                static_cast<LONG64>(lastWarning))
        {
            const auto pendingSince = ReadCounter(s_permitPendingSinceMs);
            const auto pendingAge = pendingSince > 0 && now >= pendingSince ? now - pendingSince : 0;
            spdlog::warn(
                "SkyrimTogetherVR update owner starved: ownerAgeMs={} pendingAgeMs={} producedSequence={} consumedSequence={} activationThread={}",
                ownerAge,
                pendingAge,
                aSequence,
                ReadCounter(s_lastConsumedSequence),
                s_endpoint->ActivationThreadId);
        }
    }

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
