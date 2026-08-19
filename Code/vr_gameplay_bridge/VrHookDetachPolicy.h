#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::VrHookDetachPolicy
{
// Keep this independent of MinHook so cleanup behavior can be failure-tested
// without loading the game or MinHook itself.
enum class OperationResult : std::uint8_t
{
    Complete,
    AlreadyDisabled,
    NotCreated,
    Failed,
};

struct HookState
{
    bool Created{};
    bool Enabled{};
};

using Operation = OperationResult (*)(void*) noexcept;

struct Callbacks
{
    Operation Disable{};
    Operation Remove{};
    void* Context{};
};

// Returns false without clearing state when MinHook cannot prove that the
// detour was disabled and removed.  Callers must retain their trampoline in
// that case because the target may still branch into the module.
[[nodiscard]] inline bool Detach(HookState& a_state, const Callbacks a_callbacks) noexcept
{
    if (!a_state.Created)
    {
        a_state.Enabled = false;
        return true;
    }

    if (a_state.Enabled)
    {
        const auto disabled = a_callbacks.Disable ? a_callbacks.Disable(a_callbacks.Context) : OperationResult::Failed;
        switch (disabled)
        {
        case OperationResult::Complete:
        case OperationResult::AlreadyDisabled: a_state.Enabled = false; break;
        case OperationResult::NotCreated: a_state = {}; return true;
        case OperationResult::Failed: return false;
        }
    }

    const auto removed = a_callbacks.Remove ? a_callbacks.Remove(a_callbacks.Context) : OperationResult::Failed;
    switch (removed)
    {
    case OperationResult::Complete:
    case OperationResult::NotCreated: a_state = {}; return true;
    case OperationResult::AlreadyDisabled:
    case OperationResult::Failed: return false;
    }
    return false;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::VrHookDetachPolicy
