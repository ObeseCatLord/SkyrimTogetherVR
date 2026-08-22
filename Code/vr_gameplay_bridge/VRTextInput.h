#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace SkyrimTogetherVR::GameplayAdapter
{
class VRTextInput
{
public:
    static constexpr std::uint32_t kChatMaximumText = 512;
    static constexpr std::uint64_t kRequestTimeoutMilliseconds = 120'000;

    using Generation = std::uint64_t;
    using OverlayHandle = std::uint64_t;

    enum class Result
    {
        Completed,
        Cancelled,
        Unsupported,
        Failed,
        InvalidRequest,
        Busy,
        WrongThread,
    };

    struct Completion
    {
        Result Status{Result::Failed};
        std::string Text{};
    };

    struct RequestContext
    {
        Generation Generation{};
        std::uint64_t SessionId{};

        [[nodiscard]] constexpr bool operator==(const RequestContext& a_other) const noexcept
        {
            return Generation == a_other.Generation && SessionId == a_other.SessionId;
        }
    };

    using Callback = std::function<void(Completion)>;

    enum class BackendResult
    {
        Success,
        Unsupported,
        Failed,
    };

    enum class EventType
    {
        Other,
        KeyboardDone,
        KeyboardClosed,
    };

    struct OverlayEvent
    {
        EventType Type{EventType::Other};
        Generation UserValue{};
    };

    // Keeps the state machine testable without an OpenVR runtime. Production
    // uses the private adapter in VRTextInput.cpp.
    class Backend
    {
    public:
        virtual ~Backend() = default;

        [[nodiscard]] virtual BackendResult CreateOverlay(std::string_view a_key, OverlayHandle& a_handle) noexcept = 0;
        [[nodiscard]] virtual BackendResult ShowKeyboard(
            OverlayHandle a_handle,
            std::string_view a_description,
            std::uint32_t a_maxText,
            std::string_view a_existingText,
            Generation a_generation) noexcept = 0;
        [[nodiscard]] virtual bool PollEvent(OverlayHandle a_handle, OverlayEvent& a_event) noexcept = 0;
        [[nodiscard]] virtual BackendResult GetKeyboardText(std::string& a_text, std::uint32_t a_capacity) noexcept = 0;
        [[nodiscard]] virtual BackendResult DestroyOverlay(OverlayHandle a_handle) noexcept = 0;
    };

    // All values are monotonically increasing milliseconds. Production uses
    // steady_clock; tests can advance a deterministic implementation.
    class Clock
    {
    public:
        virtual ~Clock() = default;

        [[nodiscard]] virtual std::uint64_t NowMilliseconds() noexcept = 0;
    };

    explicit VRTextInput(Backend& a_backend) noexcept;
    VRTextInput(Backend& a_backend, Clock& a_clock) noexcept;
    ~VRTextInput() = default;  // Never call OpenVR from static/DLL teardown.

    VRTextInput(const VRTextInput&) = delete;
    VRTextInput(VRTextInput&&) = delete;
    VRTextInput& operator=(const VRTextInput&) = delete;
    VRTextInput& operator=(VRTextInput&&) = delete;

    [[nodiscard]] static VRTextInput& Get() noexcept;

    // Must be called from the validated game main/command-pump thread.
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool RequestText(
        std::string_view a_description,
        std::string_view a_existingText,
        std::uint32_t a_maxText,
        Callback a_callback) noexcept;
    // May be called from a MessageBox/UI callback. The request starts only
    // when Pump runs on the verified command-pump owner thread.
    [[nodiscard]] bool QueueText(
        std::string_view a_description,
        std::string_view a_existingText,
        std::uint32_t a_maxText,
        RequestContext a_context,
        Callback a_callback) noexcept;
    void CancelQueued(RequestContext a_context) noexcept;
    void Pump() noexcept;

    // Explicit lifecycle shutdown only. It cancels an accepted request and
    // destroys this service's private overlay. No DLL detach path calls this.
    void Shutdown() noexcept;

    [[nodiscard]] bool IsActive() const noexcept { return _flowReserved.load(std::memory_order_acquire); }
    [[nodiscard]] Generation ActiveGeneration() const noexcept { return _active ? _generation : 0; }
    [[nodiscard]] bool IsOwnerThread() const noexcept;

private:
    struct QueuedRequest;

    [[nodiscard]] static Clock& DefaultClock() noexcept;
    [[nodiscard]] bool EnsureOverlay() noexcept;
    void PumpQueuedRequests() noexcept;
    [[nodiscard]] bool StartReservedRequest(QueuedRequest&& a_request) noexcept;
    void Finish(Result a_result, std::string a_text = {}) noexcept;
    void LogFailure(std::string_view a_operation, BackendResult a_result) const noexcept;
    void LogTransition(std::string_view a_transition, Result a_result) const noexcept;
    static void Invoke(Callback& a_callback, Completion a_completion) noexcept;

    struct QueuedRequest
    {
        std::string Description;
        std::string ExistingText;
        std::uint32_t MaximumText{};
        RequestContext Context;
        Callback CompletionCallback;
        std::uint64_t QueuedAt{};
        bool Cancelled{};
    };

    Backend& _backend;
    Clock& _clock;
    OverlayHandle _overlay{};
    Generation _generation{};
    std::uint64_t _requestStartedAt{};
    std::thread::id _ownerThread{};
    Callback _callback{};
    std::string _description{};
    std::string _existingText{};
    std::uint32_t _maximumText{};
    bool _initialized{};
    bool _active{};
    bool _shuttingDown{};
    RequestContext _activeContext{};
    bool _activeCancellationRequested{};
    std::atomic_bool _flowReserved{};
    std::mutex _queueLock;
    std::deque<QueuedRequest> _queuedRequests;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
