#include "ProgressionHooks.h"

#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>

#include <atomic>
#include <cstring>

namespace SkyrimTogetherVR::GameplayAdapter::ProgressionHooks
{
namespace
{
using AddSkillExperience = void (*)(RE::PlayerCharacter*, RE::ActorValue, float) noexcept;
using CalculateExperience = bool (*)(std::uint32_t, float*, float*, float*, float*) noexcept;

struct HookRecord
{
    const char* Name{};
    void* Target{};
    VrHookDetachPolicy::HookState State{};
};

AddSkillExperience g_originalAddSkillExperience{};
CalculateExperience g_originalCalculateExperience{};
HookRecord g_addSkillExperienceHook{"PlayerCharacter::AddSkillExperience"};
HookRecord g_calculateExperienceHook{"CalculateExperience"};
std::atomic_bool g_installing{};
std::atomic_bool g_installed{};
std::atomic_bool g_missingAddSkillExperienceTrampolineLogged{};
std::atomic_bool g_invalidSkillExperienceLogged{};
thread_local std::uint32_t g_remoteExperienceApplicationDepth{};

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (a_address < text.address() || a_address >= text.address() + text.size())
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & executable) != 0;
}

template <std::size_t N> [[nodiscard]] bool HasExpectedPrologue(const std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_expected) noexcept
{
    return IsExecutableTarget(a_address) && std::memcmp(reinterpret_cast<const void*>(a_address), a_expected.data(), a_expected.size()) == 0;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void* a_context) noexcept
{
    const auto& hook = *static_cast<const HookRecord*>(a_context);
    const auto status = MH_DisableHook(hook.Target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook disable failed ({})", hook.Name, static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void* a_context) noexcept
{
    const auto& hook = *static_cast<const HookRecord*>(a_context);
    const auto status = MH_RemoveHook(hook.Target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook removal failed ({})", hook.Name, static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook(HookRecord& a_hook) noexcept
{
    return VrHookDetachPolicy::Detach(a_hook.State, {DisableHook, RemoveHook, &a_hook});
}

[[nodiscard]] bool DetachHooks() noexcept
{
    const bool calculateDetached = DetachHook(g_calculateExperienceHook);
    const bool addSkillDetached = DetachHook(g_addSkillExperienceHook);
    return calculateDetached && addSkillDetached;
}

void ForgetDetachedHooks() noexcept
{
    g_addSkillExperienceHook = {"PlayerCharacter::AddSkillExperience"};
    g_calculateExperienceHook = {"CalculateExperience"};
    g_originalAddSkillExperience = nullptr;
    g_originalCalculateExperience = nullptr;
    g_missingAddSkillExperienceTrampolineLogged.store(false, std::memory_order_relaxed);
    g_invalidSkillExperienceLogged.store(false, std::memory_order_relaxed);
}

[[nodiscard]] bool RollbackFailedInstall(const char* a_stage) noexcept
{
    if (DetachHooks())
    {
        ForgetDetachedHooks();
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    g_installed.store(true, std::memory_order_release);
    BridgeEndpoint::Get().Fault("progression hook rollback could not prove detachment");
    SKSE::log::critical(
        "SkyrimTogetherVRGameplayBridge: {} rollback could not prove progression detours detached; "
        "retaining their targets and trampolines so a possible live detour remains callable",
        a_stage);
    return false;
}

[[nodiscard]] bool ReadSkillExperience(const RE::PlayerCharacter& a_player, const std::uint32_t a_actorValue, float& ar_experience) noexcept
{
    if (a_actorValue < kFirstSkillActorValue || a_actorValue > kLastSkillActorValue)
        return false;

    const auto* skills = a_player.GetInfoRuntimeData().skills;
    if (!skills || !skills->data)
        return false;

    const auto experience = skills->data->skills[a_actorValue - kFirstSkillActorValue].xp;
    if (!std::isfinite(experience) || experience < 0.0F)
        return false;
    ar_experience = experience;
    return true;
}

void HookAddSkillExperience(RE::PlayerCharacter* a_player, const RE::ActorValue a_actorValue, const float a_experience) noexcept
{
    try {
    const auto original = g_originalAddSkillExperience;
    if (!original)
    {
        if (!g_missingAddSkillExperienceTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled AddSkillExperience detour has no trampoline; "
                "the original call cannot be safely forwarded");
        }
        return;
    }

    const auto actorValue = static_cast<std::uint32_t>(a_actorValue);
    const auto* player = RE::PlayerCharacter::GetSingleton();
    const bool capture = a_player && a_player == player && IsCombatSkillActorValue(actorValue);
    float previousExperience{};
    const bool hasPreviousExperience = capture && ReadSkillExperience(*a_player, actorValue, previousExperience);

    original(a_player, a_actorValue, a_experience);

    if (!capture || !hasPreviousExperience)
        return;

    float currentExperience{};
    if (!ReadSkillExperience(*a_player, actorValue, currentExperience))
    {
        if (!g_invalidSkillExperienceLogged.exchange(true, std::memory_order_relaxed))
        {
            SKSE::log::warn(
                "SkyrimTogetherVRGameplayBridge: AddSkillExperience completed without a readable combat-skill XP value; "
                "the polling recovery path remains active");
        }
        return;
    }

    static_cast<void>(LocalGameplayCapture::CaptureExactExperience(*a_player, actorValue, previousExperience, currentExperience, IsRemoteExperienceApplication()));
    } catch (...) {
        // Capture is optional; do not retry the native body after it ran.
    }
}

bool HookCalculateExperience(const std::uint32_t a_actorValue, float* a_factor, float* a_bonus, float* a_unknown1, float* a_unknown2) noexcept
{
    try {
    const auto original = g_originalCalculateExperience;
    if (!original)
        return false;

    const auto succeeded = original(a_actorValue, a_factor, a_bonus, a_unknown1, a_unknown2);
    if (succeeded && IsRemoteExperienceApplication() && a_factor && a_bonus)
    {
        *a_factor = 1.0F;
        *a_bonus = 0.0F;
    }
    return succeeded;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool CreateHook(HookRecord& ar_hook, void* a_detour, void** a_original) noexcept
{
    const auto status = MH_CreateHook(ar_hook.Target, a_detour, a_original);
    if (status != MH_OK)
    {
        NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook creation failed ({})", ar_hook.Name, static_cast<int>(status)); });
        return false;
    }
    ar_hook.State.Created = true;
    if (!*a_original || *a_original == ar_hook.Target)
    {
        NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook refused an invalid MinHook trampoline", ar_hook.Name); });
        return false;
    }
    return true;
}

[[nodiscard]] bool EnableHook(HookRecord& ar_hook) noexcept
{
    // A failed enable does not prove the target remained unchanged. Mark it
    // enabled before attempting rollback so detach always tries disable first.
    ar_hook.State.Enabled = true;
    const auto status = MH_EnableHook(ar_hook.Target);
    if (status == MH_OK)
        return true;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook enable failed ({})", ar_hook.Name, static_cast<int>(status)); });
    return false;
}
} // namespace

ScopedRemoteExperienceApplication::ScopedRemoteExperienceApplication() noexcept
{
    ++g_remoteExperienceApplicationDepth;
}

ScopedRemoteExperienceApplication::~ScopedRemoteExperienceApplication() noexcept
{
    if (g_remoteExperienceApplicationDepth != 0)
        --g_remoteExperienceApplicationDepth;
}

bool IsRemoteExperienceApplication() noexcept
{
    return g_remoteExperienceApplicationDepth != 0;
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    const auto finish = [](const bool a_result) noexcept
    {
        g_installing.store(false, std::memory_order_release);
        return a_result;
    };

    try
    {
        REL::Relocation<AddSkillExperience> addSkillExperience{REL::ID(kAddSkillExperienceVrAddressLibraryId)};
        REL::Relocation<CalculateExperience> calculateExperience{REL::Offset(kCalculateExperienceVrRva)};
        if (addSkillExperience.offset() != kAddSkillExperienceVrRva || !HasExpectedPrologue(addSkillExperience.address(), kAddSkillExperienceVrPrologue) ||
            calculateExperience.offset() != kCalculateExperienceVrRva || !HasExpectedPrologue(calculateExperience.address(), kCalculateExperienceVrPrologue))
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: verified VR progression target address or prologue mismatch");
            return finish(false);
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for progression hooks ({})", static_cast<int>(initialize));
            return finish(false);
        }

        g_addSkillExperienceHook.Target = reinterpret_cast<void*>(addSkillExperience.address());
        g_calculateExperienceHook.Target = reinterpret_cast<void*>(calculateExperience.address());
        if (!CreateHook(g_addSkillExperienceHook, reinterpret_cast<void*>(&HookAddSkillExperience), reinterpret_cast<void**>(&g_originalAddSkillExperience)) ||
            !CreateHook(g_calculateExperienceHook, reinterpret_cast<void*>(&HookCalculateExperience), reinterpret_cast<void**>(&g_originalCalculateExperience)) ||
            !EnableHook(g_calculateExperienceHook) || !EnableHook(g_addSkillExperienceHook))
            return finish(RollbackFailedInstall("progression hook installation"));

        g_installed.store(true, std::memory_order_release);
        SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed verified XP hooks at VR RVAs 0x{:X} and 0x{:X}", kAddSkillExperienceVrRva, kCalculateExperienceVrRva);
        return finish(true);
    }
    catch (...)
    {
        return finish(RollbackFailedInstall("progression hook exception"));
    }
}

bool Uninstall() noexcept
{
    try {
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    const bool detached = DetachHooks();
    if (!detached)
    {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("progression hook uninstall could not prove detachment"); });
        NoThrow::BestEffort([] {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: progression hook uninstall incomplete; preserving callable trampolines");
        });
        g_installing.store(false, std::memory_order_release);
        return false;
    }

    ForgetDetachedHooks();
    g_installed.store(false, std::memory_order_release);
    g_installing.store(false, std::memory_order_release);
    return true;
    } catch (...) {
        g_installing.store(false, std::memory_order_release);
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ProgressionHooks
