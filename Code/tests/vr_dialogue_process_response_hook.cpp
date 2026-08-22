#include <catch2/catch.hpp>

#include <vr_gameplay_bridge/DialogueProcessResponseHook.h>

#include <memory>

namespace
{
namespace Policy = SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHookPolicy;
using SkyrimTogetherVR::GameplayAdapter::VrHookDetachPolicy::Detach;
using SkyrimTogetherVR::GameplayAdapter::VrHookDetachPolicy::HookState;
using SkyrimTogetherVR::GameplayAdapter::VrHookDetachPolicy::OperationResult;
} // namespace

TEST_CASE("AIProcess ProcessResponse remains pinned to the verified Skyrim VR body")
{
    const std::array<std::uint8_t, 19> expectedPrologue{
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x56, 0x57,
        0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
    };

    REQUIRE(Policy::kProcessResponseVrRva == 0x066DD50);
    REQUIRE(Policy::kProcessResponseVrFunctionSpan == 0x27A);
    REQUIRE(Policy::kProcessResponseVrPrologue == expectedPrologue);
    REQUIRE(Policy::IsPinnedTarget(Policy::kProcessResponseVrRva));
    REQUIRE_FALSE(Policy::IsPinnedTarget(Policy::kRejectedGeneratedProcessResponseVrRva));
    REQUIRE_FALSE(Policy::IsPinnedTarget(Policy::kRejectedProcessResponseHelperVrRva));
}

TEST_CASE("AIProcess ProcessResponse verifies its complete verified function span")
{
    constexpr std::uintptr_t textStart = 0x140000000;
    constexpr std::uintptr_t functionStart = textStart + Policy::kProcessResponseVrRva;

    REQUIRE(Policy::IsSpanWithin(textStart, Policy::kProcessResponseVrRva + Policy::kProcessResponseVrFunctionSpan,
                                 functionStart, Policy::kProcessResponseVrFunctionSpan));
    REQUIRE_FALSE(Policy::IsSpanWithin(textStart, Policy::kProcessResponseVrRva + Policy::kProcessResponseVrFunctionSpan - 1,
                                       functionStart, Policy::kProcessResponseVrFunctionSpan));
}

TEST_CASE("AIProcess ProcessResponse ABI shim preserves indirect by-value smart-pointer storage")
{
    REQUIRE(Policy::kDialogueItemArgumentIsIndirectStorage);
    REQUIRE(Policy::kByValueSmartPointerStorageBytes == 8);
    REQUIRE(Policy::kFifthArgumentStackOffset == 0x28);
    REQUIRE(Policy::kShimShadowAndSaveBytes == 0x68);
}

TEST_CASE("AIProcess ProcessResponse suppresses only managed replay-external remote speakers")
{
    struct Case
    {
        bool HasTalkingActor;
        bool ManagedRemote;
        bool AuthoritativeReplay;
        Policy::Disposition Expected;
    };
    constexpr std::array cases{
        Case{false, true, false, Policy::Disposition::ForwardToOriginal},
        Case{true, false, false, Policy::Disposition::ForwardToOriginal},
        Case{true, true, true, Policy::Disposition::ForwardToOriginal},
        Case{true, true, false, Policy::Disposition::SuppressAndDestroyDialogueItemStorage},
    };

    for (const auto& test : cases)
        REQUIRE(Policy::Decide(test.HasTalkingActor, test.ManagedRemote, test.AuthoritativeReplay) == test.Expected);
}

TEST_CASE("AIProcess ProcessResponse destroys indirect by-value storage exactly once")
{
    for (const auto disposition : {Policy::Disposition::ForwardToOriginal,
                                   Policy::Disposition::SuppressAndDestroyDialogueItemStorage}) {
        REQUIRE(Policy::DetourOwnedDestructionCount(disposition) +
                Policy::NativeOwnedDestructionCount(disposition) == 1);
    }
}

TEST_CASE("AIProcess ProcessResponse destruction dereferences RDX storage")
{
    struct DestructionProbe
    {
        int* Count{};
        ~DestructionProbe() { ++*Count; }
    };

    int destructionCount{};
    alignas(DestructionProbe) std::array<std::byte, sizeof(DestructionProbe)> rawStorage{};
    auto* const storedArgument = std::construct_at(
        reinterpret_cast<DestructionProbe*>(rawStorage.data()), &destructionCount);

    Policy::DestroyIndirectStorage(storedArgument);
    REQUIRE(destructionCount == 1);
}

TEST_CASE("AIProcess ProcessResponse detach keeps the ABI shim when MinHook cannot disable")
{
    struct Fixture
    {
        bool RemoveCalled{};

        static OperationResult Disable(void*) noexcept { return OperationResult::Failed; }
        static OperationResult Remove(void* a_context) noexcept
        {
            static_cast<Fixture*>(a_context)->RemoveCalled = true;
            return OperationResult::Complete;
        }
    } fixture;
    HookState state{.Created = true, .Enabled = true};

    REQUIRE_FALSE(Detach(state, {Fixture::Disable, Fixture::Remove, &fixture}));
    REQUIRE(state.Created);
    REQUIRE(state.Enabled);
    REQUIRE_FALSE(fixture.RemoveCalled);
}

TEST_CASE("AIProcess ProcessResponse detach removes the shim only after disable succeeds")
{
    struct Fixture
    {
        bool Disabled{};
        bool Removed{};

        static OperationResult Disable(void* a_context) noexcept
        {
            static_cast<Fixture*>(a_context)->Disabled = true;
            return OperationResult::Complete;
        }
        static OperationResult Remove(void* a_context) noexcept
        {
            auto& fixture = *static_cast<Fixture*>(a_context);
            fixture.Removed = fixture.Disabled;
            return fixture.Removed ? OperationResult::Complete : OperationResult::Failed;
        }
    } fixture;
    HookState state{.Created = true, .Enabled = true};

    REQUIRE(Detach(state, {Fixture::Disable, Fixture::Remove, &fixture}));
    REQUIRE(fixture.Disabled);
    REQUIRE(fixture.Removed);
    REQUIRE_FALSE(state.Created);
    REQUIRE_FALSE(state.Enabled);
}
