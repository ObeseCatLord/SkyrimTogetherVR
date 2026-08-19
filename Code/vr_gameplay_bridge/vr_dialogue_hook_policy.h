#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy
{
struct HookAttachment
{
    bool Created{};
    bool Enabled{};
};

template <class Disable, class Remove> [[nodiscard]] bool TryDetachHook(HookAttachment& arAttachment, Disable&& aDisable, Remove&& aRemove) noexcept
{
    if (arAttachment.Enabled)
    {
        if (!aDisable())
            return false;
        arAttachment.Enabled = false;
    }
    if (arAttachment.Created)
    {
        if (!aRemove())
            return false;
        arAttachment.Created = false;
    }
    return true;
}

// MinHook patches the target only after it gives us a trampoline.  Treat the
// trampoline as a required part of the exact-hook contract: falling back to
// the target from an active detour would recurse and could suppress speech.
[[nodiscard]] constexpr bool CanEnableSpeakSoundHook(const std::uintptr_t a_target, const std::uintptr_t a_trampoline) noexcept
{
    return a_target != 0 && a_trampoline != 0 && a_target != a_trampoline;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy
