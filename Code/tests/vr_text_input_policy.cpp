#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/VRTextInput.h>

#include <deque>
#include <atomic>
#include <thread>
#include <utility>

namespace
{
using TextInput = SkyrimTogetherVR::GameplayAdapter::VRTextInput;

class FakeBackend final : public TextInput::Backend
{
public:
    [[nodiscard]] TextInput::BackendResult CreateOverlay(std::string_view, TextInput::OverlayHandle& a_handle) noexcept override
    {
        ++CreateCalls;
        a_handle = 42;
        return CreateResult;
    }
    [[nodiscard]] TextInput::BackendResult ShowKeyboard(
        TextInput::OverlayHandle,
        std::string_view,
        const std::uint32_t a_maxText,
        std::string_view,
        const TextInput::Generation a_generation) noexcept override
    {
        MaximumText = a_maxText;
        LastGeneration = a_generation;
        return ShowResult;
    }
    [[nodiscard]] bool PollEvent(TextInput::OverlayHandle, TextInput::OverlayEvent& a_event) noexcept override
    {
        if (Events.empty())
            return false;
        a_event = Events.front();
        Events.pop_front();
        return true;
    }
    [[nodiscard]] TextInput::BackendResult GetKeyboardText(std::string& a_text, std::uint32_t) noexcept override
    {
        ++GetTextCalls;
        a_text = Text;
        return TextResult;
    }
    [[nodiscard]] TextInput::BackendResult DestroyOverlay(TextInput::OverlayHandle) noexcept override
    {
        ++DestroyCalls;
        return TextInput::BackendResult::Success;
    }

    TextInput::BackendResult CreateResult{TextInput::BackendResult::Success};
    TextInput::BackendResult ShowResult{TextInput::BackendResult::Success};
    TextInput::BackendResult TextResult{TextInput::BackendResult::Success};
    std::deque<TextInput::OverlayEvent> Events{};
    std::string Text{};
    TextInput::Generation LastGeneration{};
    std::uint32_t MaximumText{};
    std::uint32_t CreateCalls{};
    std::uint32_t GetTextCalls{};
    std::uint32_t DestroyCalls{};
};

class FakeClock final : public TextInput::Clock
{
public:
    [[nodiscard]] std::uint64_t NowMilliseconds() noexcept override { return Now; }

    void Advance(const std::uint64_t a_milliseconds) noexcept { Now += a_milliseconds; }

    std::uint64_t Now{};
};
} // namespace

TEST_CASE("VR text input completes only the active generated request", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    TextInput input{backend};
    REQUIRE(input.Initialize());

    TextInput::Completion completion{};
    REQUIRE(input.RequestText("Chat", "", TextInput::kChatMaximumText, [&](TextInput::Completion a_completion) {
        completion = std::move(a_completion);
    }));
    REQUIRE(backend.MaximumText == TextInput::kChatMaximumText);

    backend.Events.push_back({TextInput::EventType::KeyboardDone, backend.LastGeneration});
    backend.Text.assign(TextInput::kChatMaximumText, 'x');
    input.Pump();

    REQUIRE(completion.Status == TextInput::Result::Completed);
    REQUIRE(completion.Text.size() == TextInput::kChatMaximumText);
    REQUIRE(backend.GetTextCalls == 1);
    REQUIRE_FALSE(input.IsActive());
}

TEST_CASE("VR text input rejects out-of-range text and cancels exactly once", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    TextInput input{backend};
    REQUIRE(input.Initialize());

    std::uint32_t callbackCount{};
    TextInput::Result result{TextInput::Result::Completed};
    REQUIRE_FALSE(input.RequestText("Chat", "", TextInput::kChatMaximumText + 1, [&](TextInput::Completion a_completion) {
        ++callbackCount;
        result = a_completion.Status;
    }));
    REQUIRE(callbackCount == 1);
    REQUIRE(result == TextInput::Result::InvalidRequest);

    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion a_completion) {
        ++callbackCount;
        result = a_completion.Status;
    }));
    backend.Events.push_back({TextInput::EventType::KeyboardClosed, backend.LastGeneration});
    backend.Events.push_back({TextInput::EventType::KeyboardDone, backend.LastGeneration});
    input.Pump();

    REQUIRE(callbackCount == 2);
    REQUIRE(result == TextInput::Result::Cancelled);
    REQUIRE(backend.GetTextCalls == 0);
}

TEST_CASE("VR text input ignores stale overlay events after lifecycle teardown", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    TextInput input{backend};
    REQUIRE(input.Initialize());

    std::uint32_t callbackCount{};
    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion) { ++callbackCount; }));
    const auto staleGeneration = backend.LastGeneration;
    input.Shutdown();
    REQUIRE(callbackCount == 1);
    REQUIRE(backend.DestroyCalls == 1);

    REQUIRE(input.Initialize());
    TextInput::Completion completion{};
    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion a_completion) {
        ++callbackCount;
        completion = std::move(a_completion);
    }));
    backend.Events.push_back({TextInput::EventType::KeyboardDone, staleGeneration});
    backend.Text = "fresh";
    input.Pump();
    REQUIRE(input.IsActive());
    REQUIRE(callbackCount == 1);

    backend.Events.push_back({TextInput::EventType::KeyboardDone, backend.LastGeneration});
    input.Pump();
    REQUIRE(callbackCount == 2);
    REQUIRE(completion.Status == TextInput::Result::Completed);
    REQUIRE(completion.Text == "fresh");
}

TEST_CASE("VR text input fails unsupported overlay keyboard APIs cleanly", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    backend.ShowResult = TextInput::BackendResult::Unsupported;
    TextInput input{backend};
    REQUIRE(input.Initialize());

    std::uint32_t callbackCount{};
    TextInput::Result result{TextInput::Result::Completed};
    REQUIRE_FALSE(input.RequestText("Chat", "", 16, [&](TextInput::Completion a_completion) {
        ++callbackCount;
        result = a_completion.Status;
    }));
    REQUIRE(callbackCount == 1);
    REQUIRE(result == TextInput::Result::Unsupported);
    REQUIRE_FALSE(input.IsActive());
}

TEST_CASE("VR text input times out an accepted keyboard request exactly once", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    FakeClock clock;
    TextInput input{backend, clock};
    REQUIRE(input.Initialize());

    std::uint32_t callbackCount{};
    TextInput::Result result{TextInput::Result::Completed};
    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion a_completion) {
        ++callbackCount;
        result = a_completion.Status;
    }));

    clock.Advance(TextInput::kRequestTimeoutMilliseconds);
    input.Pump();
    input.Pump();

    REQUIRE(callbackCount == 1);
    REQUIRE(result == TextInput::Result::Failed);
    REQUIRE_FALSE(input.IsActive());
}

TEST_CASE("VR text input rejects a late completion after timeout and accepts a later request", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    FakeClock clock;
    TextInput input{backend, clock};
    REQUIRE(input.Initialize());

    std::uint32_t firstCallbackCount{};
    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion) { ++firstCallbackCount; }));
    const auto timedOutGeneration = backend.LastGeneration;
    clock.Advance(TextInput::kRequestTimeoutMilliseconds);
    input.Pump();
    REQUIRE(firstCallbackCount == 1);

    TextInput::Completion completion{};
    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion a_completion) {
        completion = std::move(a_completion);
    }));
    const auto activeGeneration = backend.LastGeneration;
    REQUIRE(activeGeneration != timedOutGeneration);

    backend.Events.push_back({TextInput::EventType::KeyboardDone, timedOutGeneration});
    input.Pump();

    REQUIRE(firstCallbackCount == 1);
    REQUIRE(input.IsActive());

    backend.Text = "fresh";
    backend.Events.push_back({TextInput::EventType::KeyboardDone, activeGeneration});
    input.Pump();

    REQUIRE(completion.Status == TextInput::Result::Completed);
    REQUIRE(completion.Text == "fresh");
    REQUIRE_FALSE(input.IsActive());
}

TEST_CASE("VR text input rejects completed control characters", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    TextInput input{backend};
    REQUIRE(input.Initialize());

    TextInput::Result result{TextInput::Result::Completed};
    REQUIRE(input.RequestText("Chat", "", 16, [&](TextInput::Completion a_completion) {
        result = a_completion.Status;
    }));
    backend.Text = "not\nallowed";
    backend.Events.push_back({TextInput::EventType::KeyboardDone, backend.LastGeneration});
    input.Pump();

    REQUIRE(result == TextInput::Result::InvalidRequest);
    REQUIRE(backend.GetTextCalls == 1);
}

TEST_CASE("VR text input marshals off-thread requests through the owner pump and cancels once", "[skyrim-vr][text-input]")
{
    FakeBackend backend;
    TextInput input{backend};
    REQUIRE(input.Initialize());

    std::atomic_bool queued{};
    std::uint32_t callbackCount{};
    TextInput::Result result{TextInput::Result::Completed};
    std::thread worker([&] {
        queued = input.QueueText("Chat", "", 16, {91, 77}, [&](TextInput::Completion a_completion) {
            ++callbackCount;
            result = a_completion.Status;
        });
    });
    worker.join();

    REQUIRE(queued.load());
    REQUIRE(backend.CreateCalls == 0);
    input.CancelQueued({91, 77});
    input.Pump();
    REQUIRE(callbackCount == 1);
    REQUIRE(result == TextInput::Result::Cancelled);
    REQUIRE(backend.CreateCalls == 0);

    std::thread secondWorker([&] {
        queued = input.QueueText("Chat", "", 16, {92, 78}, [&](TextInput::Completion a_completion) {
            ++callbackCount;
            result = a_completion.Status;
        });
    });
    secondWorker.join();
    input.Pump();
    REQUIRE(queued.load());
    REQUIRE(backend.CreateCalls == 1);
    backend.Text = "fresh";
    backend.Events.push_back({TextInput::EventType::KeyboardDone, backend.LastGeneration});
    input.Pump();
    REQUIRE(callbackCount == 2);
    REQUIRE(result == TextInput::Result::Completed);
}
