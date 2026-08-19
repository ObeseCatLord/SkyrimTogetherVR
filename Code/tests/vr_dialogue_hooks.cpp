#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/vr_dialogue_hook_policy.h>

namespace
{
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::CanEnableSpeakSoundHook;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::HookAttachment;
using SkyrimTogetherVR::GameplayAdapter::DialogueHookPolicy::TryDetachHook;
} // namespace

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
