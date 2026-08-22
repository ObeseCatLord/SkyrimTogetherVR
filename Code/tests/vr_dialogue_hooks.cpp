#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/vr_dialogue_hook_policy.h>

namespace
{
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::CanEnableSpeakSoundHook;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::CaptureBeforeNativeIf;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::DecideSpeakerEvent;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::DecideSpeakerPresentation;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::HookAttachment;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::IsPinnedPlayDialogueOptionTarget;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::ShouldAdvanceDialogueBaseline;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::ShouldCaptureExactDialogueChoice;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::ShouldCaptureLocalNpcSpeaker;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::ShouldPublishPolledDialogue;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::SkyrimSubtitleTopicFormId;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::TryDetachHook;
} // namespace

TEST_CASE("Dialogue speaker event policy preserves remote dialogue ownership")
{
    struct Case
    {
        bool HasSpeaker;
        bool ManagedRemoteSpeaker;
        bool RemoteReplay;
        bool CallsNative;
        bool CapturesLocal;
    };
    constexpr std::array cases{
        Case{true, false, false, true, true},
        Case{true, true, false, false, false},
        Case{true, true, true, true, false},
        Case{false, false, false, true, false},
    };

    for (const auto& test : cases) {
        const auto disposition = DecideSpeakerEvent(
            test.HasSpeaker, test.ManagedRemoteSpeaker, test.RemoteReplay);
        REQUIRE(disposition.CallNative == test.CallsNative);
        REQUIRE(disposition.CaptureLocal == test.CapturesLocal);
    }
}

TEST_CASE("Dialogue presentation policy suppresses only when managed relay is available")
{
    struct Case
    {
        bool ManagedRemoteSpeaker;
        bool RemoteReplay;
        bool RelayAvailable;
        bool CallsNative;
        bool CapturesLocal;
        bool SuppressesManagedRemote;
        bool FallsBackToNative;
    };
    constexpr std::array cases{
        // Unmanaged native presentation remains the local capture source.
        Case{false, false, false, true, true, false, false},
        // A healthy managed relay preserves no-echo suppression.
        Case{true, false, true, false, false, true, false},
        // A managed actor must remain audible when no replay path exists.
        Case{true, false, false, true, false, false, true},
        // Explicit replay is native presentation, not local capture or fallback.
        Case{true, true, false, true, false, false, false},
    };

    for (const auto& test : cases) {
        const auto disposition = DecideSpeakerPresentation(
            true, test.ManagedRemoteSpeaker, test.RemoteReplay, test.RelayAvailable);
        REQUIRE(disposition.CallNative == test.CallsNative);
        REQUIRE(disposition.CaptureLocal == test.CapturesLocal);
        REQUIRE(disposition.SuppressManagedRemote == test.SuppressesManagedRemote);
        REQUIRE(disposition.FallbackToNative == test.FallsBackToNative);
    }
}

TEST_CASE("Dialogue capture admits only observed local non-player speakers")
{
    struct Case
    {
        bool HasSpeaker;
        bool HasLocalPlayer;
        bool IsLocalPlayer;
        bool ManagedRemoteSpeaker;
        bool HasValidFormId;
        bool IsObservedNpc;
        bool Captures;
    };
    constexpr std::array cases{
        Case{true, true, false, false, true, true, true},
        Case{false, true, false, false, true, true, false},
        Case{true, false, false, false, true, true, false},
        Case{true, true, true, false, true, true, false},
        Case{true, true, false, true, true, true, false},
        Case{true, true, false, false, false, true, false},
        Case{true, true, false, false, true, false, false},
    };

    for (const auto& test : cases) {
        REQUIRE(ShouldCaptureLocalNpcSpeaker(
                    test.HasSpeaker, test.HasLocalPlayer, test.IsLocalPlayer, test.ManagedRemoteSpeaker,
                    test.HasValidFormId, test.IsObservedNpc) == test.Captures);
    }
}

TEST_CASE("Skyrim subtitle transport omits the topic form ID")
{
    REQUIRE(SkyrimSubtitleTopicFormId() == 0);
}

TEST_CASE("Dialogue voice capture precedes native playback without a second emission")
{
    std::array<int, 2> trace{};
    std::size_t next{};
    const auto nativeResult = CaptureBeforeNativeIf(
        true,
        [&] { trace[next++] = 1; },
        [&] {
            trace[next++] = 2;
            return 42;
        });

    REQUIRE(nativeResult == 42);
    REQUIRE(next == 2);
    REQUIRE(trace == std::array{1, 2});
}

TEST_CASE("Dialogue hook detach preserves an enabled trampoline when disable fails")
{
    HookAttachment attachment{.Created = true, .Enabled = true};
    bool removeCalled = false;

    REQUIRE_FALSE(TryDetachHook(
        attachment, [] { return false; },
        [&]
        {
            removeCalled = true;
            return true;
        }));
    REQUIRE(attachment.Created);
    REQUIRE(attachment.Enabled);
    REQUIRE_FALSE(removeCalled);
}

TEST_CASE("Dialogue hook detach preserves created state after removal failure and supports retry")
{
    HookAttachment attachment{.Created = true, .Enabled = true};

    REQUIRE_FALSE(TryDetachHook(attachment, [] { return true; }, [] { return false; }));
    REQUIRE(attachment.Created);
    REQUIRE_FALSE(attachment.Enabled);

    bool disableCalled = false;
    REQUIRE(TryDetachHook(
        attachment,
        [&]
        {
            disableCalled = true;
            return true;
        },
        [] { return true; }));
    REQUIRE_FALSE(disableCalled);
    REQUIRE_FALSE(attachment.Created);
    REQUIRE_FALSE(attachment.Enabled);
}

TEST_CASE("Dialogue hook enables only with a distinct MinHook trampoline")
{
    REQUIRE(CanEnableSpeakSoundHook(0x1405F0E20, 0x7FF600001000));
    REQUIRE_FALSE(CanEnableSpeakSoundHook(0, 0x7FF600001000));
    REQUIRE_FALSE(CanEnableSpeakSoundHook(0x1405F0E20, 0));
    REQUIRE_FALSE(CanEnableSpeakSoundHook(0x1405F0E20, 0x1405F0E20));
}

TEST_CASE("Dialogue choice hook remains pinned to the verified VR body")
{
    using namespace SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy;

    REQUIRE(kPlayDialogueOptionVrRva == 0x0574BF0);
    REQUIRE(IsPinnedPlayDialogueOptionTarget(kPlayDialogueOptionVrRva));
    REQUIRE_FALSE(IsPinnedPlayDialogueOptionTarget(0x05A6DA0));
    const std::array<std::uint8_t, 16> expectedPrologue{
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7,
        0x44, 0x24, 0x30, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
    };
    REQUIRE(kPlayDialogueOptionVrPrologue == expectedPrologue);
}

TEST_CASE("Exact dialogue choice capture waits for VR native acceptance")
{
    REQUIRE(ShouldCaptureExactDialogueChoice(true, false, true, true));
    REQUIRE_FALSE(ShouldCaptureExactDialogueChoice(false, false, true, true));
    REQUIRE_FALSE(ShouldCaptureExactDialogueChoice(true, true, true, true));
    REQUIRE_FALSE(ShouldCaptureExactDialogueChoice(true, false, false, true));
    REQUIRE_FALSE(ShouldCaptureExactDialogueChoice(true, false, true, false));
}

TEST_CASE("Exact dialogue publication advances the polling baseline only after acceptance")
{
    const bool exactPublicationAccepted = true;
    const bool exactPublicationRejected = false;

    REQUIRE(ShouldAdvanceDialogueBaseline(exactPublicationAccepted));
    REQUIRE_FALSE(ShouldPublishPolledDialogue(ShouldAdvanceDialogueBaseline(exactPublicationAccepted)));
    REQUIRE_FALSE(ShouldAdvanceDialogueBaseline(exactPublicationRejected));
    REQUIRE(ShouldPublishPolledDialogue(ShouldAdvanceDialogueBaseline(exactPublicationRejected)));
}
