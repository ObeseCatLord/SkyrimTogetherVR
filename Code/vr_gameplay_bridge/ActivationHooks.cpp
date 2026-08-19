#include "ActivationHooks.h"

#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>
#include <RE/T/TESObjectREFR.h>

#include <atomic>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::ActivationHooks
{
namespace
{
using ActivateRef = bool (*)(
    RE::TESObjectREFR*,
    RE::TESObjectREFR*,
    std::uint8_t,
    RE::TESBoundObject*,
    std::int32_t,
    bool) noexcept;

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};

ActivateRef g_originalActivateRef{};
void* g_hookTarget{};
VrHookDetachPolicy::HookState g_hookState{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_installed{};
std::atomic_bool g_missingTrampolineLogged{};
std::atomic<std::uint64_t> g_published{};
std::atomic<std::uint64_t> g_publicationRejections{};

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kExpectedSkyrimVrRuntime;
}

[[nodiscard]] bool IsSpanWithinSegment(
    const std::uintptr_t a_segmentAddress,
    const std::uintptr_t a_segmentSize,
    const std::uintptr_t a_spanAddress,
    const std::uintptr_t a_spanSize) noexcept
{
    if (a_spanSize == 0 || a_spanAddress < a_segmentAddress ||
        a_segmentAddress > std::numeric_limits<std::uintptr_t>::max() - a_segmentSize)
        return false;

    const auto offset = a_spanAddress - a_segmentAddress;
    return offset <= a_segmentSize && a_spanSize <= a_segmentSize - offset;
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedExecutableTarget(
    const std::uintptr_t a_address,
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithinSegment(
            static_cast<std::uintptr_t>(text.address()),
            static_cast<std::uintptr_t>(text.size()),
            a_address,
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    if (!IsSpanWithinSegment(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress),
            static_cast<std::uintptr_t>(memory.RegionSize),
            a_address,
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0 &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
}

void RecordPublicationRejection(const char* a_reason) noexcept
{
    const auto count = g_publicationRejections.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ActivationHookPolicy::ShouldLogAggregate(count)) {
        NoThrow::BestEffort([&] { SKSE::log::warn(
            "SkyrimTogetherVRGameplayBridge: pre-activation capture {} (count={})",
            a_reason,
            count); });
    }
}

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void*) noexcept
{
    const auto status = MH_DisableHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error(
        "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef hook disable failed ({})",
        static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void*) noexcept
{
    const auto status = MH_RemoveHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error(
        "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef hook remove failed ({})",
        static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook() noexcept
{
    return VrHookDetachPolicy::Detach(g_hookState, {DisableHook, RemoveHook, nullptr});
}

void ForgetDetachedHook() noexcept
{
    g_hookTarget = nullptr;
    g_originalActivateRef = nullptr;
    g_hookState = {};
    g_installed.store(false, std::memory_order_release);
    g_missingTrampolineLogged.store(false, std::memory_order_relaxed);
    g_published.store(0, std::memory_order_relaxed);
    g_publicationRejections.store(0, std::memory_order_relaxed);
}

void RetainFaultedHook(const char* a_operation) noexcept
{
    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("TESObjectREFR::ActivateRef hook rollback could not prove detachment"); });
    NoThrow::BestEffort([&] { SKSE::log::critical(
        "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef {} could not prove detachment; retaining the "
        "trampoline so a possible live detour remains callable while the bridge is faulted",
        a_operation); });
}

bool HookActivateRef(
    RE::TESObjectREFR* a_target,
    RE::TESObjectREFR* a_activator,
    const std::uint8_t a_arg2,
    RE::TESBoundObject* a_objectToGet,
    const std::int32_t a_count,
    const bool a_defaultProcessingOnly) noexcept
{
    try {
    const auto original = g_originalActivateRef;
    if (!original) {
        if (!g_missingTrampolineLogged.exchange(true, std::memory_order_relaxed)) {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled TESObjectREFR::ActivateRef detour has no trampoline; "
                "the bridge remains faulted until the hook can be removed");
        }
        return false;
    }

    // Capture uses the target's state before the original routine can mutate
    // doors, containers, or scripted activators. It owns no native pointer
    // after this synchronous call returns and never blocks local activation.
    if (g_installed.load(std::memory_order_acquire) && a_target && a_activator) {
        const auto capture = LocalGameplayCapture::CapturePreActivation(
            *a_target, *a_activator);
        if (capture == LocalGameplayCapture::PreActivationCaptureResult::Published)
            g_published.fetch_add(1, std::memory_order_relaxed);
        else if (capture == LocalGameplayCapture::PreActivationCaptureResult::PublicationRejected)
            RecordPublicationRejection("publication was rejected");
    }

    return original(a_target, a_activator, a_arg2, a_objectToGet, a_count, a_defaultProcessingOnly);
    } catch (...) {
        return false;
    }
}
} // namespace

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    try {
        if (!ActivationHookPolicy::HasPinnedTargetConfiguration() || !IsExpectedVrRuntime()) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef hook requires exact Skyrim VR 1.4.15.0");
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: MinHook initialization failed for TESObjectREFR::ActivateRef ({})",
                static_cast<int>(initialize));
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<ActivateRef> target{REL::Offset(ActivationHookPolicy::kActivateRefVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (target.offset() != ActivationHookPolicy::kActivateRefVrRva ||
            target.address() != moduleBase + ActivationHookPolicy::kActivateRefVrRva ||
            !IsVerifiedExecutableTarget(target.address(), ActivationHookPolicy::kActivateRefVrPrologue)) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef target validation failed at RVA 0x{:X}",
                target.offset());
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_hookTarget = reinterpret_cast<void*>(target.address());
        void* trampoline{};
        const auto create = MH_CreateHook(g_hookTarget, reinterpret_cast<void*>(&HookActivateRef), &trampoline);
        if (create != MH_OK || !trampoline || trampoline == g_hookTarget) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef hook creation or trampoline validation failed ({})",
                static_cast<int>(create));
            if (create == MH_OK) {
                g_hookState.Created = true;
                if (!DetachHook()) {
                    RetainFaultedHook("creation rollback");
                    return false;
                }
            }
            ForgetDetachedHook();
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_originalActivateRef = reinterpret_cast<ActivateRef>(trampoline);
        g_hookState.Created = true;

        // A failed enable may still have modified the target. Keep the
        // trampoline callable until removal proves the target was restored.
        g_hookState.Enabled = true;
        const auto enable = MH_EnableHook(g_hookTarget);
        if (enable != MH_OK) {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef hook enable failed ({})",
                static_cast<int>(enable));
            if (DetachHook()) {
                ForgetDetachedHook();
                g_installAttempted.store(false, std::memory_order_release);
                return false;
            }
            RetainFaultedHook("install rollback");
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: installed exact pre-activation TESObjectREFR::ActivateRef capture at RVA 0x{:X}",
            ActivationHookPolicy::kActivateRefVrRva);
        return true;
    } catch (...) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("TESObjectREFR::ActivateRef hook target resolution threw"); });
        NoThrow::BestEffort([] { SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: TESObjectREFR::ActivateRef target resolution threw; refusing hook"); });
        if (g_hookState.Created && !DetachHook()) {
            RetainFaultedHook("exception rollback");
            return false;
        }
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
}

bool Uninstall() noexcept
{
    try {
    g_installed.store(false, std::memory_order_release);
    if (!g_hookState.Created) {
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }
    if (!DetachHook()) {
        RetainFaultedHook("uninstall");
        return false;
    }

    SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: removed pre-activation TESObjectREFR::ActivateRef capture (published={}, rejected={})",
        g_published.load(std::memory_order_relaxed),
        g_publicationRejections.load(std::memory_order_relaxed));
    ForgetDetachedHook();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
    } catch (...) {
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActivationHooks
