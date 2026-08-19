#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/VrHookDetachPolicy.h>

namespace
{
using namespace SkyrimTogetherVR::GameplayAdapter::VrHookDetachPolicy;

struct FailureInjection
{
    OperationResult Disable{OperationResult::Complete};
    OperationResult Remove{OperationResult::Complete};
    unsigned DisableCalls{};
    unsigned RemoveCalls{};
};

OperationResult Disable(void* a_context) noexcept
{
    auto& injection = *static_cast<FailureInjection*>(a_context);
    ++injection.DisableCalls;
    return injection.Disable;
}

OperationResult Remove(void* a_context) noexcept
{
    auto& injection = *static_cast<FailureInjection*>(a_context);
    ++injection.RemoveCalls;
    return injection.Remove;
}

[[nodiscard]] Callbacks MakeCallbacks(FailureInjection& a_injection) noexcept
{
    return {Disable, Remove, &a_injection};
}
} // namespace

TEST_CASE("Hook detach retains state when disable fails and succeeds on retry")
{
    HookState state{true, true};
    FailureInjection injection{OperationResult::Failed, OperationResult::Complete};

    REQUIRE_FALSE(Detach(state, MakeCallbacks(injection)));
    REQUIRE(state.Created);
    REQUIRE(state.Enabled);
    REQUIRE(injection.DisableCalls == 1);
    REQUIRE(injection.RemoveCalls == 0);

    injection.Disable = OperationResult::Complete;
    REQUIRE(Detach(state, MakeCallbacks(injection)));
    REQUIRE_FALSE(state.Created);
    REQUIRE_FALSE(state.Enabled);
    REQUIRE(injection.DisableCalls == 2);
    REQUIRE(injection.RemoveCalls == 1);
}

TEST_CASE("Hook detach retains trampoline state when remove fails and retries")
{
    HookState state{true, true};
    FailureInjection injection{OperationResult::Complete, OperationResult::Failed};

    REQUIRE_FALSE(Detach(state, MakeCallbacks(injection)));
    REQUIRE(state.Created);
    REQUIRE_FALSE(state.Enabled);
    REQUIRE(injection.DisableCalls == 1);
    REQUIRE(injection.RemoveCalls == 1);

    injection.Remove = OperationResult::Complete;
    REQUIRE(Detach(state, MakeCallbacks(injection)));
    REQUIRE_FALSE(state.Created);
    REQUIRE_FALSE(state.Enabled);
    REQUIRE(injection.DisableCalls == 1);
    REQUIRE(injection.RemoveCalls == 2);
}
