#include "VRTextInput.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <utility>

#if defined(_WIN32) && !defined(TP_VR_TEXT_INPUT_TESTING)
#    include <RE/Skyrim.h>
#    include <SKSE/Logger.h>
#endif

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
std::atomic<std::uint64_t> g_overlayKeySerial{};
std::atomic<std::uint32_t> g_failureLogCount{};
std::atomic<std::uint32_t> g_transitionLogCount{};
constexpr std::uint32_t kMaximumFailureLogs = 8;
constexpr std::uint32_t kMaximumTransitionLogs = 16;

[[nodiscard]] VRTextInput::Generation NextGeneration(const VRTextInput::Generation a_generation) noexcept
{
    return a_generation == std::numeric_limits<VRTextInput::Generation>::max() ? 1 : a_generation + 1;
}

[[nodiscard]] bool IsValidText(const std::string_view a_text, const std::uint32_t a_maximum) noexcept
{
    if (a_text.size() > a_maximum)
        return false;

    for (std::size_t index = 0; index < a_text.size();) {
        const auto first = static_cast<unsigned char>(a_text[index]);
        if (first <= 0x1F || first == 0x7F)
            return false;
        if (first <= 0x7F) {
            ++index;
            continue;
        }

        std::uint32_t codepoint{};
        std::size_t continuationCount{};
        if ((first & 0xE0) == 0xC0) {
            codepoint = first & 0x1F;
            continuationCount = 1;
        } else if ((first & 0xF0) == 0xE0) {
            codepoint = first & 0x0F;
            continuationCount = 2;
        } else if ((first & 0xF8) == 0xF0) {
            codepoint = first & 0x07;
            continuationCount = 3;
        } else {
            return false;
        }
        if (index + continuationCount >= a_text.size())
            return false;
        for (std::size_t continuation = 1; continuation <= continuationCount; ++continuation) {
            const auto byte = static_cast<unsigned char>(a_text[index + continuation]);
            if ((byte & 0xC0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (byte & 0x3F);
        }
        const auto minimum = continuationCount == 1 ? 0x80u : continuationCount == 2 ? 0x800u : 0x10000u;
        if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            (codepoint >= 0x80 && codepoint <= 0x9F))
            return false;
        index += continuationCount + 1;
    }
    return true;
}

class UnavailableBackend final : public VRTextInput::Backend
{
public:
    [[nodiscard]] VRTextInput::BackendResult CreateOverlay(std::string_view, VRTextInput::OverlayHandle&) noexcept override
    {
        return VRTextInput::BackendResult::Unsupported;
    }
    [[nodiscard]] VRTextInput::BackendResult ShowKeyboard(
        VRTextInput::OverlayHandle,
        std::string_view,
        std::uint32_t,
        std::string_view,
        VRTextInput::Generation) noexcept override
    {
        return VRTextInput::BackendResult::Unsupported;
    }
    [[nodiscard]] bool PollEvent(VRTextInput::OverlayHandle, VRTextInput::OverlayEvent&) noexcept override { return false; }
    [[nodiscard]] VRTextInput::BackendResult GetKeyboardText(std::string&, std::uint32_t) noexcept override
    {
        return VRTextInput::BackendResult::Unsupported;
    }
    [[nodiscard]] VRTextInput::BackendResult DestroyOverlay(VRTextInput::OverlayHandle) noexcept override
    {
        return VRTextInput::BackendResult::Success;
    }
};

class SteadyClock final : public VRTextInput::Clock
{
public:
    [[nodiscard]] std::uint64_t NowMilliseconds() noexcept override
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

#if defined(_WIN32) && !defined(TP_VR_TEXT_INPUT_TESTING)
[[nodiscard]] VRTextInput::BackendResult MapResult(const vr::EVROverlayError a_result) noexcept
{
    if (a_result == vr::VROverlayError_None)
        return VRTextInput::BackendResult::Success;
    if (a_result == vr::VROverlayError_PermissionDenied || a_result == vr::VROverlayError_RequestFailed ||
        a_result == vr::VROverlayError_KeyboardAlreadyInUse)
        return VRTextInput::BackendResult::Unsupported;
    return VRTextInput::BackendResult::Failed;
}

class OpenVRBackend final : public VRTextInput::Backend
{
public:
    [[nodiscard]] VRTextInput::BackendResult CreateOverlay(
        const std::string_view a_key, VRTextInput::OverlayHandle& a_handle) noexcept override
    {
        auto* overlay = RE::BSOpenVR::GetCleanIVROverlay();
        if (!overlay)
            return VRTextInput::BackendResult::Unsupported;
        vr::VROverlayHandle_t handle{vr::k_ulOverlayHandleInvalid};
        const auto result = MapResult(overlay->CreateOverlay(a_key.data(), "Skyrim Together VR Text Input", &handle));
        if (result == VRTextInput::BackendResult::Success)
            a_handle = handle;
        return result;
    }

    [[nodiscard]] VRTextInput::BackendResult ShowKeyboard(
        const VRTextInput::OverlayHandle a_handle,
        const std::string_view a_description,
        const std::uint32_t a_maxText,
        const std::string_view a_existingText,
        const VRTextInput::Generation a_generation) noexcept override
    {
        auto* overlay = RE::BSOpenVR::GetCleanIVROverlay();
        if (!overlay)
            return VRTextInput::BackendResult::Unsupported;
        return MapResult(overlay->ShowKeyboardForOverlay(
            static_cast<vr::VROverlayHandle_t>(a_handle),
            vr::k_EGamepadTextInputModeNormal,
            vr::k_EGamepadTextInputLineModeSingleLine,
            a_description.data(),
            a_maxText,
            a_existingText.data(),
            false,
            a_generation));
    }

    [[nodiscard]] bool PollEvent(const VRTextInput::OverlayHandle a_handle, VRTextInput::OverlayEvent& a_event) noexcept override
    {
        auto* overlay = RE::BSOpenVR::GetCleanIVROverlay();
        if (!overlay)
            return false;
        vr::VREvent_t event{};
        if (!overlay->PollNextOverlayEvent(static_cast<vr::VROverlayHandle_t>(a_handle), &event, sizeof(event)))
            return false;
        switch (event.eventType) {
        case vr::VREvent_KeyboardDone:
            a_event.Type = VRTextInput::EventType::KeyboardDone;
            a_event.UserValue = event.data.keyboard.uUserValue;
            break;
        case vr::VREvent_KeyboardClosed:
            a_event.Type = VRTextInput::EventType::KeyboardClosed;
            a_event.UserValue = event.data.keyboard.uUserValue;
            break;
        default:
            a_event = {};
            break;
        }
        return true;
    }

    [[nodiscard]] VRTextInput::BackendResult GetKeyboardText(std::string& a_text, const std::uint32_t a_capacity) noexcept override
    {
        auto* overlay = RE::BSOpenVR::GetCleanIVROverlay();
        if (!overlay || a_capacity == 0)
            return VRTextInput::BackendResult::Unsupported;
        std::array<char, VRTextInput::kChatMaximumText + 1> text{};
        const auto copied = overlay->GetKeyboardText(text.data(), a_capacity);
        if (copied > a_capacity)
            return VRTextInput::BackendResult::Failed;
        const auto terminator = std::find(text.begin(), text.begin() + a_capacity, '\0');
        if (terminator == text.begin() + a_capacity)
            return VRTextInput::BackendResult::Failed;
        a_text.assign(text.begin(), terminator);
        return VRTextInput::BackendResult::Success;
    }

    [[nodiscard]] VRTextInput::BackendResult DestroyOverlay(const VRTextInput::OverlayHandle a_handle) noexcept override
    {
        auto* overlay = RE::BSOpenVR::GetCleanIVROverlay();
        return overlay ? MapResult(overlay->DestroyOverlay(static_cast<vr::VROverlayHandle_t>(a_handle))) :
                         VRTextInput::BackendResult::Unsupported;
    }
};
#endif

[[nodiscard]] VRTextInput::Backend& ProductionBackend() noexcept
{
#if defined(_WIN32) && !defined(TP_VR_TEXT_INPUT_TESTING)
    static OpenVRBackend backend;
#else
    static UnavailableBackend backend;
#endif
    return backend;
}
} // namespace

VRTextInput::VRTextInput(Backend& a_backend) noexcept : VRTextInput(a_backend, DefaultClock()) {}

VRTextInput::VRTextInput(Backend& a_backend, Clock& a_clock) noexcept : _backend(a_backend), _clock(a_clock) {}

VRTextInput& VRTextInput::Get() noexcept
{
    static VRTextInput service{ProductionBackend()};
    return service;
}

VRTextInput::Clock& VRTextInput::DefaultClock() noexcept
{
    static SteadyClock clock;
    return clock;
}

bool VRTextInput::Initialize() noexcept
{
    if (_initialized)
        return IsOwnerThread();
    _ownerThread = std::this_thread::get_id();
    _initialized = true;
    return true;
}

bool VRTextInput::RequestText(
    const std::string_view a_description,
    const std::string_view a_existingText,
    const std::uint32_t a_maxText,
    Callback a_callback) noexcept
{
    if (!a_callback)
        return false;
    if (!_initialized || !IsOwnerThread()) {
        Invoke(a_callback, {.Status = Result::WrongThread});
        return false;
    }
    if (_shuttingDown) {
        Invoke(a_callback, {.Status = Result::Busy});
        return false;
    }
    if (a_description.empty() || a_maxText == 0 || a_maxText > kChatMaximumText || !IsValidText(a_existingText, a_maxText)) {
        Invoke(a_callback, {.Status = Result::InvalidRequest});
        return false;
    }
    if (_flowReserved.exchange(true, std::memory_order_acq_rel)) {
        Invoke(a_callback, {.Status = Result::Busy});
        return false;
    }
    return StartReservedRequest({
        .Description = std::string(a_description),
        .ExistingText = std::string(a_existingText),
        .MaximumText = a_maxText,
        .CompletionCallback = std::move(a_callback),
        .QueuedAt = _clock.NowMilliseconds(),
    });
}

bool VRTextInput::QueueText(
    const std::string_view a_description,
    const std::string_view a_existingText,
    const std::uint32_t a_maxText,
    const RequestContext a_context,
    Callback a_callback) noexcept
{
    if (!a_callback || a_context.Generation == 0 || a_context.SessionId == 0 || a_description.empty() || a_maxText == 0 ||
        a_maxText > kChatMaximumText || !IsValidText(a_existingText, a_maxText) || _flowReserved.exchange(true, std::memory_order_acq_rel))
        return false;

    try {
        std::lock_guard lock(_queueLock);
        if (_shuttingDown || !_queuedRequests.empty())
        {
            _flowReserved.store(false, std::memory_order_release);
            return false;
        }
        _queuedRequests.push_back({
            .Description = std::string(a_description),
            .ExistingText = std::string(a_existingText),
            .MaximumText = a_maxText,
            .Context = a_context,
            .CompletionCallback = std::move(a_callback),
            .QueuedAt = _clock.NowMilliseconds(),
        });
        return true;
    } catch (...) {
        _flowReserved.store(false, std::memory_order_release);
        return false;
    }
}

void VRTextInput::CancelQueued(const RequestContext a_context) noexcept
{
    if (a_context.Generation == 0 || a_context.SessionId == 0)
        return;

    std::lock_guard lock(_queueLock);
    for (auto& request : _queuedRequests) {
        if (request.Context == a_context)
            request.Cancelled = true;
    }
    if (_activeContext == a_context)
        _activeCancellationRequested = true;
}

void VRTextInput::Pump() noexcept
{
    if (!_initialized || !IsOwnerThread())
        return;

    PumpQueuedRequests();
    if (!_active)
        return;

    bool cancellationRequested{};
    {
        std::lock_guard lock(_queueLock);
        cancellationRequested = _activeCancellationRequested;
    }
    if (cancellationRequested) {
        Finish(Result::Cancelled);
        return;
    }

    if (_clock.NowMilliseconds() - _requestStartedAt >= kRequestTimeoutMilliseconds) {
        LogTransition("request timed out", Result::Failed);
        Finish(Result::Failed);
        return;
    }

    OverlayEvent event{};
    while (_active && _backend.PollEvent(_overlay, event)) {
        if (event.UserValue != _generation)
            continue;
        if (event.Type == EventType::KeyboardClosed) {
            Finish(Result::Cancelled);
            return;
        }
        if (event.Type != EventType::KeyboardDone)
            continue;

        std::string text;
        const auto result = _backend.GetKeyboardText(text, _maximumText + 1);
        if (result != BackendResult::Success) {
            LogFailure("GetKeyboardText", result);
            Finish(result == BackendResult::Unsupported ? Result::Unsupported : Result::Failed);
            return;
        }
        if (!IsValidText(text, _maximumText)) {
            Finish(Result::InvalidRequest);
            return;
        }
        Finish(Result::Completed, std::move(text));
        return;
    }
}

void VRTextInput::Shutdown() noexcept
{
    if (!_initialized || !IsOwnerThread())
        return;
    {
        std::lock_guard lock(_queueLock);
        _shuttingDown = true;
    }
    if (_active)
        Finish(Result::Cancelled);
    std::deque<QueuedRequest> queued;
    {
        std::lock_guard lock(_queueLock);
        queued.swap(_queuedRequests);
        _activeContext = {};
        _activeCancellationRequested = false;
    }
    for (auto& request : queued)
        Invoke(request.CompletionCallback, {.Status = Result::Cancelled});
    _flowReserved.store(false, std::memory_order_release);
    _generation = NextGeneration(_generation);
    if (_overlay != 0) {
        const auto result = _backend.DestroyOverlay(_overlay);
        if (result != BackendResult::Success)
            LogFailure("DestroyOverlay", result);
        _overlay = 0;
    }
    _initialized = false;
    _ownerThread = {};
    {
        std::lock_guard lock(_queueLock);
        _shuttingDown = false;
    }
}

bool VRTextInput::IsOwnerThread() const noexcept
{
    return _ownerThread == std::this_thread::get_id();
}

void VRTextInput::PumpQueuedRequests() noexcept
{
    if (_active)
        return;

    QueuedRequest request;
    {
        std::lock_guard lock(_queueLock);
        if (_queuedRequests.empty())
            return;
        request = std::move(_queuedRequests.front());
        _queuedRequests.pop_front();
    }

    if (request.Cancelled) {
        _flowReserved.store(false, std::memory_order_release);
        Invoke(request.CompletionCallback, {.Status = Result::Cancelled});
        return;
    }
    if (_clock.NowMilliseconds() - request.QueuedAt >= kRequestTimeoutMilliseconds) {
        LogTransition("queued request timed out", Result::Failed);
        _flowReserved.store(false, std::memory_order_release);
        Invoke(request.CompletionCallback, {.Status = Result::Failed});
        return;
    }
    StartReservedRequest(std::move(request));
}

bool VRTextInput::StartReservedRequest(QueuedRequest&& a_request) noexcept
{
    if (!_initialized || !IsOwnerThread() || !_flowReserved.load(std::memory_order_acquire))
        return false;
    if (_shuttingDown) {
        _flowReserved.store(false, std::memory_order_release);
        Invoke(a_request.CompletionCallback, {.Status = Result::Cancelled});
        return false;
    }
    if (!EnsureOverlay()) {
        _flowReserved.store(false, std::memory_order_release);
        Invoke(a_request.CompletionCallback, {.Status = Result::Unsupported});
        return false;
    }

    _generation = NextGeneration(_generation);
    _description = std::move(a_request.Description);
    _existingText = std::move(a_request.ExistingText);
    _maximumText = a_request.MaximumText;
    {
        std::lock_guard lock(_queueLock);
        _activeContext = std::move(a_request.Context);
        _activeCancellationRequested = a_request.Cancelled;
    }
    _callback = std::move(a_request.CompletionCallback);
    _active = true;
    const auto result = _backend.ShowKeyboard(_overlay, _description, _maximumText, _existingText, _generation);
    if (result != BackendResult::Success) {
        LogFailure("ShowKeyboardForOverlay", result);
        Finish(result == BackendResult::Unsupported ? Result::Unsupported : Result::Failed);
        return false;
    }
    _requestStartedAt = _clock.NowMilliseconds();
    LogTransition("request started", Result::Completed);
    return true;
}

bool VRTextInput::EnsureOverlay() noexcept
{
    if (_overlay != 0)
        return true;

    char key[128]{};
    const auto serial = g_overlayKeySerial.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto processId = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto written = std::snprintf(
        key, sizeof(key), "skyrimtogethervr.textinput.%llx.%llx", processId, static_cast<unsigned long long>(serial));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(key))
        return false;
    const auto result = _backend.CreateOverlay(std::string_view{key, static_cast<std::size_t>(written)}, _overlay);
    if (result == BackendResult::Success)
        return true;
    LogFailure("CreateOverlay", result);
    _overlay = 0;
    return false;
}

void VRTextInput::Finish(const Result a_result, std::string a_text) noexcept
{
    if (!_active)
        return;
    _active = false;
    _flowReserved.store(false, std::memory_order_release);
    _requestStartedAt = 0;
    _maximumText = 0;
    _description.clear();
    _existingText.clear();
    _generation = NextGeneration(_generation);
    {
        std::lock_guard lock(_queueLock);
        _activeContext = {};
        _activeCancellationRequested = false;
    }
    auto callback = std::move(_callback);
    _callback = {};
    LogTransition("request finished", a_result);
    Invoke(callback, {.Status = a_result, .Text = std::move(a_text)});
}

void VRTextInput::LogFailure(const std::string_view a_operation, const BackendResult a_result) const noexcept
{
    const auto count = g_failureLogCount.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32) && !defined(TP_VR_TEXT_INPUT_TESTING)
    if (count < kMaximumFailureLogs) {
        SKSE::log::warn("SkyrimTogetherVR VR text input {} failed ({})", a_operation, static_cast<int>(a_result));
    } else if (count == kMaximumFailureLogs) {
        SKSE::log::warn("SkyrimTogetherVR VR text input failure logging suppressed after {} entries", kMaximumFailureLogs);
    }
#else
    static_cast<void>(a_operation);
    static_cast<void>(a_result);
    static_cast<void>(count);
#endif
}

void VRTextInput::LogTransition(const std::string_view a_transition, const Result a_result) const noexcept
{
    const auto count = g_transitionLogCount.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32) && !defined(TP_VR_TEXT_INPUT_TESTING)
    if (count < kMaximumTransitionLogs) {
        SKSE::log::info("SkyrimTogetherVR VR text input {} ({})", a_transition, static_cast<int>(a_result));
    } else if (count == kMaximumTransitionLogs) {
        SKSE::log::info("SkyrimTogetherVR VR text input transition logging suppressed after {} entries", kMaximumTransitionLogs);
    }
#else
    static_cast<void>(a_transition);
    static_cast<void>(a_result);
    static_cast<void>(count);
#endif
}

void VRTextInput::Invoke(Callback& a_callback, Completion a_completion) noexcept
{
    if (!a_callback)
        return;
    try {
        a_callback(std::move(a_completion));
    } catch (...) {
        // A consumer callback must not unwind into the game pump.
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
