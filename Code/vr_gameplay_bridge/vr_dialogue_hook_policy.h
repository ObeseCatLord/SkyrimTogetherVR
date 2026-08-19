#pragma once

#include <array>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy
{
// This body was verified directly in SkyrimVR.exe 1.4.15.0. The generated
// desktop alias 35269 resolves to a different function and must not be used.
inline constexpr std::uintptr_t kPlayDialogueOptionVrRva = 0x0574BF0;
inline constexpr std::array<std::uint8_t, 16> kPlayDialogueOptionVrPrologue{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7,
    0x44, 0x24, 0x30, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
};

struct HookAttachment
{
    bool Created{};
    bool Enabled{};
};

struct SpeakerEventDisposition
{
    bool CallNative{};
    bool CaptureLocal{};
};

// A managed remote actor owns its native dialogue presentation.  Outside an
// explicit replay, reject the engine event before it can produce duplicate
// audio or subtitles.  A replay may present remotely supplied dialogue, but
// must never become a local capture source.
[[nodiscard]] constexpr SpeakerEventDisposition DecideSpeakerEvent(
    const bool a_hasSpeaker,
    const bool a_managedRemoteSpeaker,
    const bool a_remoteReplay) noexcept
{
    if (a_hasSpeaker && a_managedRemoteSpeaker && !a_remoteReplay)
        return {};
    return {
        .CallNative = true,
        .CaptureLocal = a_hasSpeaker && !a_managedRemoteSpeaker && !a_remoteReplay,
    };
}

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

[[nodiscard]] constexpr bool IsPinnedPlayDialogueOptionTarget(const std::uintptr_t a_rva) noexcept
{
    return a_rva == kPlayDialogueOptionVrRva;
}

[[nodiscard]] constexpr bool ShouldCaptureExactDialogueChoice(
    const bool a_engineAccepted,
    const bool a_remoteReplay,
    const bool a_validSelection,
    const bool a_captureReady) noexcept
{
    return a_engineAccepted && !a_remoteReplay && a_validSelection && a_captureReady;
}

[[nodiscard]] constexpr bool ShouldAdvanceDialogueBaseline(const bool a_publicationAccepted) noexcept
{
    return a_publicationAccepted;
}

[[nodiscard]] constexpr bool ShouldPublishPolledDialogue(const bool a_matchesBaseline) noexcept
{
    return !a_matchesBaseline;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy
