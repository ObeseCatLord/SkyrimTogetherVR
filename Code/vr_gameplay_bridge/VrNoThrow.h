#pragma once

#include <utility>

namespace SkyrimTogetherVR::GameplayAdapter::NoThrow
{
// Native engine callbacks and loader admission paths must not let diagnostics
// turn a recoverable rejection into an ABI exception or noexcept termination.
template <class F>
void BestEffort(F&& a_action) noexcept
{
    try {
        std::forward<F>(a_action)();
    }
    catch (...) {
    }
}

template <class R, class F>
[[nodiscard]] R FailClosed(F&& a_action, const R a_fallback) noexcept
{
    try {
        return std::forward<F>(a_action)();
    }
    catch (...) {
        return a_fallback;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::NoThrow
