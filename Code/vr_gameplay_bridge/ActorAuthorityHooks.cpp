#include "ActorAuthorityHooks.h"

#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "VRInteractionManager.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>
#include <RE/V/ValueModifierEffect.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <type_traits>

namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks
{
namespace
{
using DoDamage = bool (*)(RE::Actor*, float, RE::Actor*, bool);
using AddDeathItems = void (*)(RE::Actor*);
using ApplyValueActiveEffect = void (*)(RE::ActiveEffect*, RE::Actor*, float, std::uint32_t);
using RestoreActorValue = void (*)(RE::Actor*, std::int32_t, float);
using PredictLethalDoDamage = float (*)(RE::Actor*, float, float);
using GenericSetPosition = void (*)(RE::TESObjectREFR*, const RE::NiPoint3&);
using ActorSetPosition = void (*)(RE::Actor*, const RE::NiPoint3&, bool);
using MoveToImpl = void (*)(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
using RootMotionControllerProcessor = void (*)(RE::Actor*, float);
using RotateAxis = void (*)(RE::TESObjectREFR*, float);
using DoDamageDisposition = ActorAuthorityHookPolicy::DoDamageDisposition;
using TargetedRemoteNpcHealthDelta = ActorAuthorityHookPolicy::TargetedRemoteNpcHealthDelta;

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
constexpr std::uint64_t kSuppressionLogInterval = 256;
constexpr std::uint64_t kRotationSuppressionLogInterval = 64;
constexpr auto kRetirementReaderDrainTimeout = std::chrono::milliseconds{250};
constexpr std::uintptr_t kRemotePlayerBit = 0x1;
constexpr std::uintptr_t kRetiringBit = 0x2;
constexpr std::uintptr_t kManagedRemoteActorStateBits = kRemotePlayerBit | kRetiringBit;
constexpr std::uint32_t kReaderGateClosedBit = 1u << 31;
constexpr std::uint32_t kReaderCountMask = ~kReaderGateClosedBit;

static_assert(alignof(RE::Actor) >= 4);
static_assert(std::is_base_of_v<RE::TESObjectREFR, RE::Actor>);
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

struct ManagedRemoteActorSlot
{
    std::atomic<std::uintptr_t> State{};
    std::atomic<std::uint32_t> Readers{};
    std::atomic<std::uint32_t> Generation{};
};

struct ManagedRemoteActorMembership
{
    bool Managed{};
    bool RemotePlayer{};
    bool Retiring{};
};

std::array<ManagedRemoteActorSlot, kManagedRemoteActorRegistryCapacity> g_managedRemoteActorRegistry{};

[[nodiscard]] std::uintptr_t ActorAddress(const RE::Actor* a_actor) noexcept
{
    return reinterpret_cast<std::uintptr_t>(a_actor);
}

[[nodiscard]] constexpr std::uintptr_t EntryActorAddress(const std::uintptr_t a_state) noexcept
{
    return a_state & ~kManagedRemoteActorStateBits;
}

[[nodiscard]] constexpr bool IsActiveEntry(const std::uintptr_t a_state, const std::uintptr_t a_actorAddress) noexcept
{
    return EntryActorAddress(a_state) == a_actorAddress && (a_state & kRetiringBit) == 0;
}

[[nodiscard]] std::size_t ManagedRemoteActorRegistryStartSlot(const std::uintptr_t a_actorAddress) noexcept
{
    auto hash = a_actorAddress >> 4;
    hash ^= hash >> 17;
    hash ^= hash >> 9;
    return hash % kManagedRemoteActorRegistryCapacity;
}

[[nodiscard]] bool TryAcquireReaderLease(ManagedRemoteActorSlot& ar_slot) noexcept
{
    auto readers = ar_slot.Readers.load(std::memory_order_acquire);
    while ((readers & kReaderGateClosedBit) == 0)
    {
        const auto readerCount = readers & kReaderCountMask;
        if (readerCount == kReaderCountMask)
            return false;
        if (ar_slot.Readers.compare_exchange_weak(
                readers, readers + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
    }
    return false;
}

void ReleaseReaderLease(ManagedRemoteActorSlot& ar_slot) noexcept
{
    ar_slot.Readers.fetch_sub(1, std::memory_order_release);
}

void RecordLeaseFailure() noexcept;

class ManagedRemoteActorRead final
{
public:
    ManagedRemoteActorRead() noexcept = default;

    ~ManagedRemoteActorRead() noexcept
    {
        while (_count != 0)
            ReleaseReaderLease(*_slots[--_count]);
    }

    ManagedRemoteActorRead(const ManagedRemoteActorRead&) = delete;
    ManagedRemoteActorRead& operator=(const ManagedRemoteActorRead&) = delete;

    [[nodiscard]] ManagedRemoteActorMembership Lookup(const RE::Actor* a_actor) noexcept
    {
        const auto actorAddress = ActorAddress(a_actor);
        if (actorAddress == 0 || (actorAddress & kManagedRemoteActorStateBits) != 0)
            return {};

        for (auto& slot : g_managedRemoteActorRegistry)
        {
            const auto state = slot.State.load(std::memory_order_acquire);
            if (EntryActorAddress(state) == actorAddress && (state & kRetiringBit) != 0)
                return {true, (state & kRemotePlayerBit) != 0, true};
            if (!IsActiveEntry(state, actorAddress))
                continue;

            const auto generation = slot.Generation.load(std::memory_order_acquire);
            if (!TryAcquireReaderLease(slot))
            {
                RecordLeaseFailure();
                const auto currentState = slot.State.load(std::memory_order_acquire);
                if (EntryActorAddress(currentState) == actorAddress && (currentState & kRetiringBit) != 0)
                    return {true, (currentState & kRemotePlayerBit) != 0, true};
                continue;
            }
            if (slot.State.load(std::memory_order_acquire) == state &&
                slot.Generation.load(std::memory_order_acquire) == generation && _count < _slots.size())
            {
                _slots[_count++] = &slot;
                return {true, (state & kRemotePlayerBit) != 0, false};
            }
            ReleaseReaderLease(slot);
            RecordLeaseFailure();
            const auto currentState = slot.State.load(std::memory_order_acquire);
            if (EntryActorAddress(currentState) == actorAddress && (currentState & kRetiringBit) != 0)
                return {true, (currentState & kRemotePlayerBit) != 0, true};
        }
        return {};
    }

private:
    std::array<ManagedRemoteActorSlot*, 2> _slots{};
    std::size_t _count{};
};

struct HookRecord
{
    const char* Name{};
    void* Target{};
    VrHookDetachPolicy::HookState State{};
};

DoDamage g_originalDoDamage{};
AddDeathItems g_originalAddDeathItems{};
ApplyValueActiveEffect g_originalApplyValueActiveEffect{};
RestoreActorValue g_originalRestoreActorValue{};
PredictLethalDoDamage g_predictLethalDoDamage{};
GenericSetPosition g_originalGenericSetPosition{};
ActorSetPosition g_originalActorSetPosition{};
MoveToImpl g_originalMoveToImpl{};
RootMotionControllerProcessor g_originalRootMotionControllerProcessor{};
RotateAxis g_originalRotateX{};
RotateAxis g_originalRotateY{};
RotateAxis g_originalRotateZ{};
HookRecord g_doDamageHook{"Actor::DoDamage"};
HookRecord g_addDeathItemsHook{"Actor::AddDeathItems"};
HookRecord g_applyValueActiveEffectHook{"ActiveEffect::ApplyValue"};
HookRecord g_restoreActorValueHook{"Actor::RestoreActorValue"};
HookRecord g_genericSetPositionHook{"TESObjectREFR::SetPosition"};
HookRecord g_actorSetPositionHook{"Actor::SetPosition"};
HookRecord g_moveToImplHook{"TESObjectREFR::MoveTo_Impl"};
HookRecord g_rootMotionControllerProcessorHook{"Actor::Process"};
HookRecord g_rotateXHook{"TESObjectREFR::RotateX"};
HookRecord g_rotateYHook{"TESObjectREFR::RotateY"};
HookRecord g_rotateZHook{"TESObjectREFR::RotateZ"};
std::atomic<bool> g_installAttempted{};
std::atomic<bool> g_installed{};
std::atomic<std::uint64_t> g_suppressedDamage{};
std::atomic<std::uint64_t> g_suppressedDeathItems{};
std::atomic<std::uint64_t> g_suppressedPositiveActiveEffectHealth{};
std::atomic<std::uint64_t> g_suppressedRestoreHealth{};
std::atomic<std::uint64_t> g_publishedRemoteNpcHealthDelta{};
std::atomic<std::uint64_t> g_failedRemoteNpcHealthDeltaPublication{};
std::atomic<std::uint64_t> g_suppressedGenericSetPosition{};
std::atomic<std::uint64_t> g_suppressedActorSetPosition{};
std::atomic<std::uint64_t> g_suppressedMoveToImpl{};
std::atomic<std::uint64_t> g_suppressedRootMotionControllerProcessor{};
std::atomic<std::uint64_t> g_suppressedRotateX{};
std::atomic<std::uint64_t> g_suppressedRotateY{};
std::atomic<std::uint64_t> g_suppressedRotateZ{};
std::atomic<std::uint64_t> g_leaseFailures{};
std::atomic<std::uint64_t> g_retirementFailures{};
std::atomic<std::uint64_t> g_retirementTimeouts{};
std::atomic<std::uint64_t> g_registryInconsistencies{};
std::atomic<bool> g_retirementEndpointFaulted{};
std::atomic<bool> g_missingDoDamageTrampolineLogged{};
std::atomic<bool> g_missingAddDeathItemsTrampolineLogged{};
std::atomic<bool> g_missingApplyValueActiveEffectTrampolineLogged{};
std::atomic<bool> g_missingRestoreActorValueTrampolineLogged{};
std::atomic<bool> g_missingGenericSetPositionTrampolineLogged{};
std::atomic<bool> g_missingActorSetPositionTrampolineLogged{};
std::atomic<bool> g_missingMoveToImplTrampolineLogged{};
std::atomic<bool> g_missingRootMotionControllerProcessorTrampolineLogged{};
std::atomic<bool> g_missingRotateXTrampolineLogged{};
std::atomic<bool> g_missingRotateYTrampolineLogged{};
std::atomic<bool> g_missingRotateZTrampolineLogged{};
thread_local std::uint32_t g_authoritativeReplayDepth{};

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kExpectedSkyrimVrRuntime;
}

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address, const std::size_t a_span) noexcept
{
    if (!IsExpectedVrRuntime())
        return false;

    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (text.size() < a_span || a_address < text.address() || a_address - text.address() > text.size() - a_span)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    const auto memoryBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (memory.RegionSize < a_span || a_address < memoryBase || a_address - memoryBase > memory.RegionSize - a_span)
        return false;

    constexpr DWORD kExecutableProtection = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0;
}

template <std::size_t N> [[nodiscard]] bool IsVerifiedTarget(const std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    return text.size() >= a_prologue.size() && a_address >= text.address() && a_address - text.address() <= text.size() - a_prologue.size() &&
           IsExecutableTarget(a_address, a_prologue.size()) &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
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

[[nodiscard]] bool DetachHook(HookRecord& ar_hook) noexcept
{
    return VrHookDetachPolicy::Detach(ar_hook.State, {DisableHook, RemoveHook, &ar_hook});
}

void ClearDetachedState() noexcept
{
    g_originalDoDamage = nullptr;
    g_originalAddDeathItems = nullptr;
    g_originalApplyValueActiveEffect = nullptr;
    g_originalRestoreActorValue = nullptr;
    g_predictLethalDoDamage = nullptr;
    g_originalGenericSetPosition = nullptr;
    g_originalActorSetPosition = nullptr;
    g_originalMoveToImpl = nullptr;
    g_originalRootMotionControllerProcessor = nullptr;
    g_originalRotateX = nullptr;
    g_originalRotateY = nullptr;
    g_originalRotateZ = nullptr;
    g_doDamageHook = {"Actor::DoDamage"};
    g_addDeathItemsHook = {"Actor::AddDeathItems"};
    g_applyValueActiveEffectHook = {"ActiveEffect::ApplyValue"};
    g_restoreActorValueHook = {"Actor::RestoreActorValue"};
    g_genericSetPositionHook = {"TESObjectREFR::SetPosition"};
    g_actorSetPositionHook = {"Actor::SetPosition"};
    g_moveToImplHook = {"TESObjectREFR::MoveTo_Impl"};
    g_rootMotionControllerProcessorHook = {"Actor::Process"};
    g_rotateXHook = {"TESObjectREFR::RotateX"};
    g_rotateYHook = {"TESObjectREFR::RotateY"};
    g_rotateZHook = {"TESObjectREFR::RotateZ"};
    g_installed.store(false, std::memory_order_release);
    g_missingDoDamageTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingAddDeathItemsTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingApplyValueActiveEffectTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingRestoreActorValueTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingGenericSetPositionTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingActorSetPositionTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingMoveToImplTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingRootMotionControllerProcessorTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingRotateXTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingRotateYTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingRotateZTrampolineLogged.store(false, std::memory_order_relaxed);
    g_suppressedDamage.store(0, std::memory_order_relaxed);
    g_suppressedDeathItems.store(0, std::memory_order_relaxed);
    g_suppressedPositiveActiveEffectHealth.store(0, std::memory_order_relaxed);
    g_suppressedRestoreHealth.store(0, std::memory_order_relaxed);
    g_publishedRemoteNpcHealthDelta.store(0, std::memory_order_relaxed);
    g_failedRemoteNpcHealthDeltaPublication.store(0, std::memory_order_relaxed);
    g_suppressedGenericSetPosition.store(0, std::memory_order_relaxed);
    g_suppressedActorSetPosition.store(0, std::memory_order_relaxed);
    g_suppressedMoveToImpl.store(0, std::memory_order_relaxed);
    g_suppressedRootMotionControllerProcessor.store(0, std::memory_order_relaxed);
    g_suppressedRotateX.store(0, std::memory_order_relaxed);
    g_suppressedRotateY.store(0, std::memory_order_relaxed);
    g_suppressedRotateZ.store(0, std::memory_order_relaxed);
    g_leaseFailures.store(0, std::memory_order_relaxed);
    g_retirementFailures.store(0, std::memory_order_relaxed);
    g_retirementTimeouts.store(0, std::memory_order_relaxed);
    g_registryInconsistencies.store(0, std::memory_order_relaxed);
    g_retirementEndpointFaulted.store(false, std::memory_order_relaxed);
    if (auto* const mapping = BridgeEndpoint::Get().Mapping())
    {
        auto& header = mapping->Header;
        header.AuthoritySuppressedDamageCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedDeathItemsCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedPositiveActiveEffectHealthCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedRestoreHealthCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedReferenceSetPositionCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedActorSetPositionCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedMoveToCount.store(0, std::memory_order_release);
        header.AuthoritySuppressedActorProcessCount.store(0, std::memory_order_release);
        header.AuthorityPublishedRemoteNpcHealthDeltaCount.store(0, std::memory_order_release);
        header.AuthorityFailedRemoteNpcHealthDeltaPublicationCount.store(0, std::memory_order_release);
        header.AuthorityLeaseFailureCount.store(0, std::memory_order_release);
        header.AuthorityRetirementFailureCount.store(0, std::memory_order_release);
        header.AuthorityRetirementTimeoutCount.store(0, std::memory_order_release);
        header.AuthorityRegistryInconsistencyCount.store(0, std::memory_order_release);
    }
}

using AuthorityCounterMember = GameplayBridge::AtomicU64 GameplayBridge::MappingHeader::*;

void MirrorAuthorityCounter(const AuthorityCounterMember a_member, const std::uint64_t a_total) noexcept
{
    if (auto* const mapping = BridgeEndpoint::Get().Mapping())
        (mapping->Header.*a_member).store(a_total, std::memory_order_release);
}

[[nodiscard]] std::uint64_t IncrementAuthorityCounter(
    std::atomic<std::uint64_t>& ar_counter,
    const AuthorityCounterMember a_member) noexcept
{
    const auto total = ar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    MirrorAuthorityCounter(a_member, total);
    return total;
}

void RecordAggregate(
    std::atomic<std::uint64_t>& ar_counter,
    const AuthorityCounterMember a_member,
    const char* a_name,
    const bool a_warning = false) noexcept
{
    const auto total = IncrementAuthorityCounter(ar_counter, a_member);
    if (total == 1 || total % kSuppressionLogInterval == 0)
    {
        if (a_warning)
            NoThrow::BestEffort([&] { SKSE::log::warn("SkyrimTogetherVRGameplayBridge: {} (aggregate={})", a_name, total); });
        else
            NoThrow::BestEffort([&] { SKSE::log::debug("SkyrimTogetherVRGameplayBridge: {} (aggregate={})", a_name, total); });
    }
}

void RecordLocalAggregate(std::atomic<std::uint64_t>& ar_counter, const char* a_name) noexcept
{
    const auto total = ar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (total == 1 || total % kRotationSuppressionLogInterval == 0)
        NoThrow::BestEffort([&] { SKSE::log::debug("SkyrimTogetherVRGameplayBridge: {} (aggregate={})", a_name, total); });
}

void RecordLeaseFailure() noexcept
{
    RecordAggregate(
        g_leaseFailures, &GameplayBridge::MappingHeader::AuthorityLeaseFailureCount,
        "actor authority reader lease rejected while a slot was retiring", true);
}

void RecordRetirementFailure(const ManagedRemoteActorRetirementResult a_result) noexcept
{
    const char* reason = "managed remote actor retirement registry inconsistency";
    if (a_result == ManagedRemoteActorRetirementResult::ReaderDrainTimedOut)
    {
        RecordAggregate(
            g_retirementTimeouts, &GameplayBridge::MappingHeader::AuthorityRetirementTimeoutCount,
            "managed remote actor retirement timed out waiting for authority readers", true);
        reason = "managed remote actor retirement timed out";
    }
    else if (a_result == ManagedRemoteActorRetirementResult::RegistryInconsistent ||
             a_result == ManagedRemoteActorRetirementResult::NotRegistered ||
             a_result == ManagedRemoteActorRetirementResult::AlreadyRetiring ||
             a_result == ManagedRemoteActorRetirementResult::InvalidActor)
    {
        RecordAggregate(
            g_registryInconsistencies, &GameplayBridge::MappingHeader::AuthorityRegistryInconsistencyCount,
            "managed remote actor retirement observed an inconsistent registry state", true);
    }
    else
        return;

    RecordAggregate(
        g_retirementFailures, &GameplayBridge::MappingHeader::AuthorityRetirementFailureCount,
        "managed remote actor retirement failed closed", true);
    if (!g_retirementEndpointFaulted.exchange(true, std::memory_order_acq_rel))
        NoThrow::BestEffort([&] { BridgeEndpoint::Get().Fault(reason); });
}

[[nodiscard]] DoDamageDisposition ClassifyDoDamage(
    ManagedRemoteActorRead& ar_managedActors,
    const RE::Actor* a_actor,
    const RE::Actor* a_source) noexcept
{
    const auto* player = RE::PlayerCharacter::GetSingleton();
    const auto target = ar_managedActors.Lookup(a_actor);
    const auto source = ar_managedActors.Lookup(a_source);
    ActorAuthorityHookPolicy::DoDamageContext context{};
    context.TargetManagedRemoteActor = target.Managed;
    context.TargetManagedRemotePlayerActor = target.RemotePlayer;
    context.TargetRetiring = target.Retiring;
    context.TargetIsLocalPlayer = player && a_actor == player;
    context.SourceIsLocalPlayer = player && a_source == player;
    context.SourceIsManagedRemotePlayerActor = source.RemotePlayer;
    context.SourceRetiring = source.Retiring;
    context.PvpEnabled = VRInteractionManager::IsPvpEnabled();
    return ActorAuthorityHookPolicy::ClassifyDoDamage(context);
}

[[nodiscard]] bool CanCallAddDeathItemsOriginal(ManagedRemoteActorRead& ar_managedActors, const RE::Actor* a_actor) noexcept
{
    return ActorAuthorityHookPolicy::ShouldCallAddDeathItemsOriginal(ar_managedActors.Lookup(a_actor).Managed);
}

[[nodiscard]] std::uint32_t ResolveActiveEffectActorValue(const RE::ActiveEffect* a_effect, const std::uint32_t a_actorValueOverride) noexcept
{
    if (a_actorValueOverride != ActorAuthorityHookPolicy::kUseActiveEffectActorValue)
        return a_actorValueOverride;

    // CommonLib's typed ValueModifierEffect layout places actorValue at +0x90.
    // Do not infer an actor value from an untyped ActiveEffect when its RTTI
    // does not prove this derived layout.
    const auto* valueModifierEffect = a_effect ? ::skyrim_cast<const RE::ValueModifierEffect*>(a_effect) : nullptr;
    return valueModifierEffect ? static_cast<std::uint32_t>(valueModifierEffect->actorValue) : ActorAuthorityHookPolicy::kUseActiveEffectActorValue;
}

[[nodiscard]] bool CanCallApplyValueActiveEffectOriginal(
    ManagedRemoteActorRead& ar_managedActors,
    const RE::Actor* a_target,
    const float a_value,
    const std::uint32_t a_effectiveActorValue) noexcept
{
    return ActorAuthorityHookPolicy::ShouldCallApplyValueActiveEffectOriginal(
        ar_managedActors.Lookup(a_target).Managed, a_value, a_effectiveActorValue);
}

[[nodiscard]] bool CanCallRestoreActorValueOriginal(
    ManagedRemoteActorRead& ar_managedActors,
    const RE::Actor* a_actor,
    const std::int32_t a_actorValue) noexcept
{
    return ActorAuthorityHookPolicy::ShouldCallRestoreActorValueOriginal(
        ar_managedActors.Lookup(a_actor).Managed, a_actorValue);
}

[[nodiscard]] bool CanCallRemoteRootMutationOriginal(
    ManagedRemoteActorRead& ar_managedActors,
    const RE::Actor* a_actor) noexcept
{
    return ActorAuthorityHookPolicy::ShouldCallRemoteRootMutationOriginal(
        ar_managedActors.Lookup(a_actor).Managed, g_authoritativeReplayDepth != 0);
}

[[nodiscard]] bool CanCallRemoteRotationOriginal(
    ManagedRemoteActorRead& ar_managedActors,
    const RE::TESObjectREFR* a_reference) noexcept
{
    // Do not inspect the reference before acquiring the registry lease. Actor
    // inherits TESObjectREFR, so the base address is the managed-actor key.
    const auto* actor = reinterpret_cast<const RE::Actor*>(a_reference);
    return ActorAuthorityHookPolicy::ShouldCallRemoteRotationOriginal(
        ar_managedActors.Lookup(actor).Managed, g_authoritativeReplayDepth != 0);
}

[[nodiscard]] bool CanCallRemoteRootMutationOriginal(
    ManagedRemoteActorRead& ar_managedActors,
    const RE::TESObjectREFR* a_reference) noexcept
{
    // Do not call RTTI or dereference a reference before the registry lease is
    // acquired. A generic SetPosition invocation can race actor retirement;
    // Actor derives directly from TESObjectREFR, so the base address is the
    // canonical managed-actor key without inspecting the object.
    const auto* actor = reinterpret_cast<const RE::Actor*>(a_reference);
    return CanCallRemoteRootMutationOriginal(ar_managedActors, actor);
}

[[nodiscard]] bool PredictSuppressedRemotePlayerDoDamageLethality(RE::Actor* a_actor, const float a_healthDamage) noexcept
{
    const auto helper = g_predictLethalDoDamage;
    if (!a_actor || !helper || !std::isfinite(a_healthDamage))
        return a_actor && a_actor->IsDead();

    const auto currentHealth = a_actor->GetActorValue(RE::ActorValue::kHealth);
    if (!std::isfinite(currentHealth))
        return a_actor->IsDead();
    // The desktop path always selects the helper's adjusted formula here.
    const auto signedDelta = helper(a_actor, -a_healthDamage, 0.0F);
    if (!std::isfinite(signedDelta) || !ActorAuthorityHookPolicy::IsPredictedDoDamageLethal(currentHealth, signedDelta))
        return a_actor->IsDead();
    return true;
}

bool HookDoDamage(RE::Actor* a_actor, const float a_healthDamage, RE::Actor* a_source, const bool a_dontAdjustDifficulty)
{
    try {
    ManagedRemoteActorRead managedActors;
    const auto disposition = ClassifyDoDamage(managedActors, a_actor, a_source);
    if (disposition == ActorAuthorityHookPolicy::DoDamageDisposition::SuppressWithoutOriginal)
    {
        RecordAggregate(
            g_suppressedDamage, &GameplayBridge::MappingHeader::AuthoritySuppressedDamageCount,
            "Actor::DoDamage from a remote player while PvP is disabled");
        return false;
    }
    if (disposition == ActorAuthorityHookPolicy::DoDamageDisposition::SuppressWithPredictedLethal)
    {
        RecordAggregate(
            g_suppressedDamage, &GameplayBridge::MappingHeader::AuthoritySuppressedDamageCount,
            "Actor::DoDamage with remote-player authority");
        return PredictSuppressedRemotePlayerDoDamageLethality(a_actor, a_healthDamage);
    }

    const auto original = g_originalDoDamage;
    if (!original)
    {
        if (!g_missingDoDamageTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled Actor::DoDamage detour has no trampoline; rejecting damage");
        }
        return false;
    }
    if (disposition != ActorAuthorityHookPolicy::DoDamageDisposition::CallOriginalAndPublishRemoteNpcHealthDelta)
        return original(a_actor, a_healthDamage, a_source, a_dontAdjustDifficulty);

    const auto healthBefore = a_actor->GetActorValue(RE::ActorValue::kHealth);
    const auto result = original(a_actor, a_healthDamage, a_source, a_dontAdjustDifficulty);
    const auto healthAfter = a_actor->GetActorValue(RE::ActorValue::kHealth);
    if (!std::isfinite(healthBefore) || !std::isfinite(healthAfter))
    {
        RecordAggregate(
            g_failedRemoteNpcHealthDeltaPublication,
            &GameplayBridge::MappingHeader::AuthorityFailedRemoteNpcHealthDeltaPublicationCount,
            "managed remote NPC health-delta publication with a non-finite sample", true);
        return result;
    }

    const auto delta = healthAfter - healthBefore;
    if (delta == 0.0F)
        return result;

    TargetedRemoteNpcHealthDelta healthDelta{};
    healthDelta.TargetLocalFormId = a_actor->GetFormID();
    healthDelta.ActorValue = ActorAuthorityHookPolicy::kHealthActorValue;
    healthDelta.Delta = delta;
    const bool validHealthDelta = ActorAuthorityHookPolicy::IsValidTargetedRemoteNpcHealthDelta(healthDelta);
    if (!validHealthDelta)
    {
        RecordAggregate(
            g_failedRemoteNpcHealthDeltaPublication,
            &GameplayBridge::MappingHeader::AuthorityFailedRemoteNpcHealthDeltaPublicationCount,
            "managed remote NPC health-delta publication", true);
        return result;
    }
    if (!LocalGameplayCapture::PublishTargetedRemoteNpcHealthDelta(a_actor->GetFormID(), delta))
    {
        RecordAggregate(
            g_failedRemoteNpcHealthDeltaPublication,
            &GameplayBridge::MappingHeader::AuthorityFailedRemoteNpcHealthDeltaPublicationCount,
            "managed remote NPC health-delta publication", true);
        return result;
    }

    RecordAggregate(
        g_publishedRemoteNpcHealthDelta, &GameplayBridge::MappingHeader::AuthorityPublishedRemoteNpcHealthDeltaCount,
        "managed remote NPC health-delta publication");
    return result;
    } catch (...) {
        return false;
    }
}

void HookAddDeathItems(RE::Actor* a_actor)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallAddDeathItemsOriginal(managedActors, a_actor))
    {
        RecordAggregate(
            g_suppressedDeathItems, &GameplayBridge::MappingHeader::AuthoritySuppressedDeathItemsCount,
            "Actor::AddDeathItems");
        return;
    }

    const auto original = g_originalAddDeathItems;
    if (!original)
    {
        if (!g_missingAddDeathItemsTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled Actor::AddDeathItems detour has no trampoline; rejecting loot generation");
        }
        return;
    }
    original(a_actor);
    } catch (...) {
    }
}

void HookApplyValueActiveEffect(
    RE::ActiveEffect* a_effect,
    RE::Actor* a_target,
    const float a_value,
    const std::uint32_t a_actorValueOverride)
{
    try {
    ManagedRemoteActorRead managedActors;
    const auto effectiveActorValue = ResolveActiveEffectActorValue(a_effect, a_actorValueOverride);
    if (!CanCallApplyValueActiveEffectOriginal(managedActors, a_target, a_value, effectiveActorValue))
    {
        RecordAggregate(
            g_suppressedPositiveActiveEffectHealth,
            &GameplayBridge::MappingHeader::AuthoritySuppressedPositiveActiveEffectHealthCount,
            "positive ActiveEffect health application on managed remote actor");
        return;
    }

    const auto original = g_originalApplyValueActiveEffect;
    if (!original)
    {
        if (!g_missingApplyValueActiveEffectTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled ActiveEffect::ApplyValue detour has no trampoline; rejecting application");
        }
        return;
    }
    original(a_effect, a_target, a_value, a_actorValueOverride);
    } catch (...) {
    }
}

void HookRestoreActorValue(RE::Actor* a_actor, const std::int32_t a_actorValue, const float a_value)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRestoreActorValueOriginal(managedActors, a_actor, a_actorValue))
    {
        RecordAggregate(
            g_suppressedRestoreHealth, &GameplayBridge::MappingHeader::AuthoritySuppressedRestoreHealthCount,
            "Actor::RestoreActorValue health restoration on managed remote actor");
        return;
    }

    const auto original = g_originalRestoreActorValue;
    if (!original)
    {
        if (!g_missingRestoreActorValueTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled Actor::RestoreActorValue detour has no trampoline; rejecting restoration");
        }
        return;
    }
    original(a_actor, a_actorValue, a_value);
    } catch (...) {
    }
}

void HookGenericSetPosition(RE::TESObjectREFR* a_reference, const RE::NiPoint3& a_position)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRootMutationOriginal(managedActors, a_reference))
    {
        RecordAggregate(
            g_suppressedGenericSetPosition,
            &GameplayBridge::MappingHeader::AuthoritySuppressedReferenceSetPositionCount,
            "TESObjectREFR::SetPosition on managed remote actor");
        return;
    }

    const auto original = g_originalGenericSetPosition;
    if (!original)
    {
        if (!g_missingGenericSetPositionTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled TESObjectREFR::SetPosition detour has no trampoline; rejecting mutation");
        return;
    }
    original(a_reference, a_position);
    } catch (...) {
    }
}

void HookActorSetPosition(RE::Actor* a_actor, const RE::NiPoint3& a_position, const bool a_syncHavok)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRootMutationOriginal(managedActors, a_actor))
    {
        RecordAggregate(
            g_suppressedActorSetPosition,
            &GameplayBridge::MappingHeader::AuthoritySuppressedActorSetPositionCount,
            "Actor::SetPosition on managed remote actor");
        return;
    }

    const auto original = g_originalActorSetPosition;
    if (!original)
    {
        if (!g_missingActorSetPositionTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled Actor::SetPosition detour has no trampoline; rejecting mutation");
        return;
    }
    original(a_actor, a_position, a_syncHavok);
    } catch (...) {
    }
}

void HookMoveToImpl(
    RE::TESObjectREFR* a_reference,
    const RE::ObjectRefHandle& a_targetHandle,
    RE::TESObjectCELL* a_cell,
    RE::TESWorldSpace* a_worldspace,
    const RE::NiPoint3& a_position,
    const RE::NiPoint3& a_angles)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRootMutationOriginal(managedActors, a_reference))
    {
        RecordAggregate(
            g_suppressedMoveToImpl, &GameplayBridge::MappingHeader::AuthoritySuppressedMoveToCount,
            "TESObjectREFR::MoveTo_Impl on managed remote actor");
        return;
    }

    const auto original = g_originalMoveToImpl;
    if (!original)
    {
        if (!g_missingMoveToImplTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled TESObjectREFR::MoveTo_Impl detour has no trampoline; rejecting mutation");
        return;
    }
    original(a_reference, a_targetHandle, a_cell, a_worldspace, a_position, a_angles);
    } catch (...) {
    }
}

void HookRootMotionControllerProcessor(RE::Actor* a_actor, const float a_deltaTime)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRootMutationOriginal(managedActors, a_actor))
    {
        RecordAggregate(
            g_suppressedRootMotionControllerProcessor,
            &GameplayBridge::MappingHeader::AuthoritySuppressedActorProcessCount,
            "Actor::Process on managed remote actor");
        return;
    }

    const auto original = g_originalRootMotionControllerProcessor;
    if (!original)
    {
        if (!g_missingRootMotionControllerProcessorTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled Actor::Process detour has no trampoline; rejecting processing");
        return;
    }
    original(a_actor, a_deltaTime);
    } catch (...) {
    }
}

void HookRotateX(RE::TESObjectREFR* a_reference, const float a_angle)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRotationOriginal(managedActors, a_reference))
    {
        RecordLocalAggregate(g_suppressedRotateX, "TESObjectREFR::RotateX on managed remote actor");
        return;
    }

    const auto original = g_originalRotateX;
    if (!original)
    {
        if (!g_missingRotateXTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled TESObjectREFR::RotateX detour has no trampoline; rejecting mutation");
        return;
    }
    original(a_reference, a_angle);
    } catch (...) {
    }
}

void HookRotateY(RE::TESObjectREFR* a_reference, const float a_angle)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRotationOriginal(managedActors, a_reference))
    {
        RecordLocalAggregate(g_suppressedRotateY, "TESObjectREFR::RotateY on managed remote actor");
        return;
    }

    const auto original = g_originalRotateY;
    if (!original)
    {
        if (!g_missingRotateYTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled TESObjectREFR::RotateY detour has no trampoline; rejecting mutation");
        return;
    }
    original(a_reference, a_angle);
    } catch (...) {
    }
}

void HookRotateZ(RE::TESObjectREFR* a_reference, const float a_angle)
{
    try {
    ManagedRemoteActorRead managedActors;
    if (!CanCallRemoteRotationOriginal(managedActors, a_reference))
    {
        RecordLocalAggregate(g_suppressedRotateZ, "TESObjectREFR::RotateZ on managed remote actor");
        return;
    }

    const auto original = g_originalRotateZ;
    if (!original)
    {
        if (!g_missingRotateZTrampolineLogged.exchange(true, std::memory_order_relaxed))
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: enabled TESObjectREFR::RotateZ detour has no trampoline; rejecting mutation");
        return;
    }
    original(a_reference, a_angle);
    } catch (...) {
    }
}

[[nodiscard]] bool RollbackInstall() noexcept
{
    const bool rotateZDetached = DetachHook(g_rotateZHook);
    const bool rotateYDetached = DetachHook(g_rotateYHook);
    const bool rotateXDetached = DetachHook(g_rotateXHook);
    const bool rootMotionControllerProcessorDetached = DetachHook(g_rootMotionControllerProcessorHook);
    const bool moveToImplDetached = DetachHook(g_moveToImplHook);
    const bool actorSetPositionDetached = DetachHook(g_actorSetPositionHook);
    const bool genericSetPositionDetached = DetachHook(g_genericSetPositionHook);
    const bool restoreActorValueDetached = DetachHook(g_restoreActorValueHook);
    const bool applyValueActiveEffectDetached = DetachHook(g_applyValueActiveEffectHook);
    const bool deathItemsDetached = DetachHook(g_addDeathItemsHook);
    const bool damageDetached = DetachHook(g_doDamageHook);
    if (rotateZDetached && rotateYDetached && rotateXDetached && rootMotionControllerProcessorDetached && moveToImplDetached && actorSetPositionDetached && genericSetPositionDetached && restoreActorValueDetached &&
        applyValueActiveEffectDetached && deathItemsDetached && damageDetached)
    {
        ClearDetachedState();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }

    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("actor authority hook rollback could not prove detachment"); });
    NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: actor authority hook rollback could not prove detachment; retaining callable trampolines"); });
    return false;
}

[[nodiscard]] bool CreateAndEnableHook(HookRecord& ar_hook, void* a_detour, void** a_original) noexcept
{
    const auto create = MH_CreateHook(ar_hook.Target, a_detour, a_original);
    if (create != MH_OK)
    {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook creation failed ({})", ar_hook.Name, static_cast<int>(create));
        return false;
    }
    ar_hook.State.Created = true;

    if (!*a_original || *a_original == ar_hook.Target)
    {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook returned an invalid trampoline", ar_hook.Name);
        return false;
    }

    // A failed enable does not prove MinHook left the target unchanged.
    ar_hook.State.Enabled = true;
    const auto enable = MH_EnableHook(ar_hook.Target);
    if (enable != MH_OK)
    {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: {} hook enable failed ({})", ar_hook.Name, static_cast<int>(enable));
        return false;
    }
    return true;
}
} // namespace

bool RegisterManagedRemoteActor(RE::Actor* a_actor, const bool a_remotePlayer) noexcept
{
    const auto actorAddress = ActorAddress(a_actor);
    if (actorAddress == 0 || (actorAddress & kManagedRemoteActorStateBits) != 0)
        return false;

    const auto entry = actorAddress | (a_remotePlayer ? kRemotePlayerBit : 0);
    const auto startSlot = ManagedRemoteActorRegistryStartSlot(actorAddress);
    for (std::size_t probe{}; probe < g_managedRemoteActorRegistry.size(); ++probe)
    {
        auto& slot = g_managedRemoteActorRegistry[(startSlot + probe) % g_managedRemoteActorRegistry.size()];
        auto state = slot.State.load(std::memory_order_acquire);
        while (state == 0)
        {
            if (slot.Readers.load(std::memory_order_acquire) != 0)
                break;

            auto generation = slot.Generation.load(std::memory_order_acquire);
            const auto nextGeneration = generation == std::numeric_limits<std::uint32_t>::max() ? 1u : generation + 1;
            if (!slot.Generation.compare_exchange_weak(generation, nextGeneration, std::memory_order_acq_rel, std::memory_order_acquire))
                continue;
            if (slot.State.compare_exchange_strong(state, entry, std::memory_order_release, std::memory_order_acquire))
                return true;
        }
        if (EntryActorAddress(state) == actorAddress)
            return state == entry;
    }
    return false;
}

ManagedRemoteActorRetirement BeginRetireManagedRemoteActor(RE::Actor* a_actor) noexcept
{
    ManagedRemoteActorRetirement retirement{};
    const auto actorAddress = ActorAddress(a_actor);
    if (actorAddress == 0 || (actorAddress & kManagedRemoteActorStateBits) != 0)
    {
        RecordRetirementFailure(retirement._result);
        return retirement;
    }

    ManagedRemoteActorSlot* matchedSlot{};
    std::size_t matchedSlotIndex{};
    for (std::size_t index{}; index < g_managedRemoteActorRegistry.size(); ++index)
    {
        auto& slot = g_managedRemoteActorRegistry[index];
        const auto state = slot.State.load(std::memory_order_acquire);
        if (EntryActorAddress(state) != actorAddress)
            continue;
        if (matchedSlot)
        {
            retirement._result = ManagedRemoteActorRetirementResult::RegistryInconsistent;
            RecordRetirementFailure(retirement._result);
            return retirement;
        }
        matchedSlot = &slot;
        matchedSlotIndex = index;
    }

    if (!matchedSlot)
    {
        retirement._result = ManagedRemoteActorRetirementResult::NotRegistered;
        RecordRetirementFailure(retirement._result);
        return retirement;
    }

    auto state = matchedSlot->State.load(std::memory_order_acquire);
    while (EntryActorAddress(state) == actorAddress)
    {
        if ((state & kRetiringBit) != 0)
        {
            retirement._result = ManagedRemoteActorRetirementResult::AlreadyRetiring;
            RecordRetirementFailure(retirement._result);
            return retirement;
        }
        if (matchedSlot->State.compare_exchange_weak(state, state | kRetiringBit, std::memory_order_acq_rel, std::memory_order_acquire))
            break;
    }
    if (EntryActorAddress(state) != actorAddress)
    {
        retirement._result = ManagedRemoteActorRetirementResult::RegistryInconsistent;
        RecordRetirementFailure(retirement._result);
        return retirement;
    }

    auto readers = matchedSlot->Readers.load(std::memory_order_acquire);
    while ((readers & kReaderGateClosedBit) == 0)
    {
        if (matchedSlot->Readers.compare_exchange_weak(
                readers, readers | kReaderGateClosedBit, std::memory_order_acq_rel, std::memory_order_acquire))
            break;
    }
    if ((readers & kReaderGateClosedBit) != 0)
    {
        retirement._result = ManagedRemoteActorRetirementResult::RegistryInconsistent;
        RecordRetirementFailure(retirement._result);
        return retirement;
    }

    const auto deadline = std::chrono::steady_clock::now() + kRetirementReaderDrainTimeout;
    while ((matchedSlot->Readers.load(std::memory_order_acquire) & kReaderCountMask) != 0)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            retirement._result = ManagedRemoteActorRetirementResult::ReaderDrainTimedOut;
            RecordRetirementFailure(retirement._result);
            return retirement;
        }
        std::this_thread::yield();
    }

    retirement._result = ManagedRemoteActorRetirementResult::Quiescent;
    retirement._slot = matchedSlotIndex;
    retirement._generation = matchedSlot->Generation.load(std::memory_order_acquire);
    retirement._state = state | kRetiringBit;
    retirement._active = true;
    return retirement;
}

ManagedRemoteActorRetirementResult FinishRetireManagedRemoteActor(ManagedRemoteActorRetirement& ar_retirement) noexcept
{
    if (!ar_retirement.IsQuiescent() || ar_retirement._slot >= g_managedRemoteActorRegistry.size())
        return ar_retirement.Result();

    auto& slot = g_managedRemoteActorRegistry[ar_retirement._slot];
    const auto state = slot.State.load(std::memory_order_acquire);
    const auto readers = slot.Readers.load(std::memory_order_acquire);
    if (slot.Generation.load(std::memory_order_acquire) != ar_retirement._generation || state != ar_retirement._state ||
        readers != kReaderGateClosedBit)
    {
        ar_retirement._active = false;
        ar_retirement._result = ManagedRemoteActorRetirementResult::RegistryInconsistent;
        RecordRetirementFailure(ar_retirement._result);
        return ar_retirement._result;
    }

    // The state remains retiring until actor/form mutation is complete. Clear
    // it before reopening the reader gate so a pre-retirement reader can never
    // acquire a lease for a recycled actor pointer.
    slot.State.store(0, std::memory_order_release);
    slot.Readers.store(0, std::memory_order_release);
    ar_retirement._active = false;
    return ManagedRemoteActorRetirementResult::Quiescent;
}

bool IsManagedRemoteActor(const RE::Actor* a_actor) noexcept
{
    ManagedRemoteActorRead managedActors;
    return managedActors.Lookup(a_actor).Managed;
}

bool IsManagedRemotePlayerActor(const RE::Actor* a_actor) noexcept
{
    ManagedRemoteActorRead managedActors;
    return managedActors.Lookup(a_actor).RemotePlayer;
}

bool WithManagedRemoteActorLease(
    RE::Actor* a_actor,
    const ManagedRemoteActorOperation a_operation,
    void* const a_context) noexcept
{
    if (!a_operation)
        return false;

    // ManagedRemoteActorRead retains its registry reader lease until after
    // the operation returns. Native callers therefore cannot race avatar
    // retirement between managed-state classification and trampoline use.
    ManagedRemoteActorRead managedActors;
    const auto membership = managedActors.Lookup(a_actor);
    const auto disposition = membership.Retiring ? ManagedRemoteActorOperationDisposition::Retiring :
                             membership.Managed ? ManagedRemoteActorOperationDisposition::ManagedRemote :
                                                  ManagedRemoteActorOperationDisposition::UnmanagedOrInvalid;
    a_operation(a_context, disposition);
    return true;
}

ManagedRemoteInvisibilityCorrectionResult CorrectManagedRemoteInvisibilityBeforeFinish(RE::Actor* a_actor) noexcept
{
    // Keep this reader in scope through SetActorValue. Retirement closes the
    // matching reader gate and waits for this lease before it may mutate or
    // destroy the actor, so splitting membership from the reset is unsafe.
    ManagedRemoteActorRead managedActors;
    const auto membership = managedActors.Lookup(a_actor);
    const auto* localPlayer = RE::PlayerCharacter::GetSingleton();
    switch (ActorAuthorityHookPolicy::ClassifyInvisibilityCorrection(
        a_actor != nullptr,
        a_actor != nullptr && a_actor == localPlayer,
        membership.Managed,
        membership.Retiring))
    {
    case ActorAuthorityHookPolicy::InvisibilityCorrectionDisposition::InvalidActor:
        return ManagedRemoteInvisibilityCorrectionResult::InvalidActor;
    case ActorAuthorityHookPolicy::InvisibilityCorrectionDisposition::LocalPlayer:
        return ManagedRemoteInvisibilityCorrectionResult::LocalPlayer;
    case ActorAuthorityHookPolicy::InvisibilityCorrectionDisposition::NotManagedRemote:
        return ManagedRemoteInvisibilityCorrectionResult::NotManagedRemote;
    case ActorAuthorityHookPolicy::InvisibilityCorrectionDisposition::Retiring:
        return ManagedRemoteInvisibilityCorrectionResult::Retiring;
    case ActorAuthorityHookPolicy::InvisibilityCorrectionDisposition::Correct:
        try
        {
            a_actor->SetActorValue(RE::ActorValue::kInvisibility, 0.0F);
            return ManagedRemoteInvisibilityCorrectionResult::Corrected;
        }
        catch (...)
        {
            return ManagedRemoteInvisibilityCorrectionResult::Failed;
        }
    }
    return ManagedRemoteInvisibilityCorrectionResult::Failed;
}

ScopedAuthoritativeReplay::ScopedAuthoritativeReplay() noexcept
{
    ++g_authoritativeReplayDepth;
}

ScopedAuthoritativeReplay::~ScopedAuthoritativeReplay() noexcept
{
    if (g_authoritativeReplayDepth != 0)
        --g_authoritativeReplayDepth;
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    try
    {
        if (!ActorAuthorityHookPolicy::HasPinnedTargetConfiguration() || !IsExpectedVrRuntime())
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: Actor authority hooks require exact Skyrim VR 1.4.15.0");
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        {
            SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for actor authority hooks ({})", static_cast<int>(initialize));
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        REL::Relocation<DoDamage> doDamage{REL::ID(ActorAuthorityHookPolicy::kDoDamageAddressLibraryId)};
        REL::Relocation<AddDeathItems> addDeathItems{REL::ID(ActorAuthorityHookPolicy::kAddDeathItemsAddressLibraryId)};
        REL::Relocation<ApplyValueActiveEffect> applyValueActiveEffect{REL::Offset(ActorAuthorityHookPolicy::kApplyValueActiveEffectVrRva)};
        REL::Relocation<RestoreActorValue> restoreActorValue{REL::ID(ActorAuthorityHookPolicy::kRestoreActorValueAddressLibraryId)};
        REL::Relocation<PredictLethalDoDamage> predictLethalDoDamage{REL::Offset(ActorAuthorityHookPolicy::kPredictLethalDoDamageVrRva)};
        REL::Relocation<GenericSetPosition> genericSetPosition{REL::Offset(ActorAuthorityHookPolicy::kGenericSetPositionVrRva)};
        REL::Relocation<ActorSetPosition> actorSetPosition{REL::Offset(ActorAuthorityHookPolicy::kActorSetPositionVrRva)};
        REL::Relocation<MoveToImpl> moveToImpl{REL::Offset(ActorAuthorityHookPolicy::kMoveToImplVrRva)};
        REL::Relocation<RootMotionControllerProcessor> rootMotionControllerProcessor{REL::Offset(ActorAuthorityHookPolicy::kRootMotionControllerProcessorVrRva)};
        REL::Relocation<RotateAxis> rotateX{REL::Offset(ActorAuthorityHookPolicy::kRotateXVrRva)};
        REL::Relocation<RotateAxis> rotateY{REL::Offset(ActorAuthorityHookPolicy::kRotateYVrRva)};
        REL::Relocation<RotateAxis> rotateZ{REL::Offset(ActorAuthorityHookPolicy::kRotateZVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (doDamage.offset() != ActorAuthorityHookPolicy::kDoDamageVrRva || doDamage.address() != moduleBase + ActorAuthorityHookPolicy::kDoDamageVrRva ||
            addDeathItems.offset() != ActorAuthorityHookPolicy::kAddDeathItemsVrRva || addDeathItems.address() != moduleBase + ActorAuthorityHookPolicy::kAddDeathItemsVrRva ||
            applyValueActiveEffect.offset() != ActorAuthorityHookPolicy::kApplyValueActiveEffectVrRva ||
            applyValueActiveEffect.address() != moduleBase + ActorAuthorityHookPolicy::kApplyValueActiveEffectVrRva ||
            restoreActorValue.offset() != ActorAuthorityHookPolicy::kRestoreActorValueVrRva ||
            restoreActorValue.address() != moduleBase + ActorAuthorityHookPolicy::kRestoreActorValueVrRva ||
            predictLethalDoDamage.offset() != ActorAuthorityHookPolicy::kPredictLethalDoDamageVrRva ||
            predictLethalDoDamage.address() != moduleBase + ActorAuthorityHookPolicy::kPredictLethalDoDamageVrRva ||
            genericSetPosition.offset() != ActorAuthorityHookPolicy::kGenericSetPositionVrRva ||
            genericSetPosition.address() != moduleBase + ActorAuthorityHookPolicy::kGenericSetPositionVrRva ||
            actorSetPosition.offset() != ActorAuthorityHookPolicy::kActorSetPositionVrRva ||
            actorSetPosition.address() != moduleBase + ActorAuthorityHookPolicy::kActorSetPositionVrRva ||
            moveToImpl.offset() != ActorAuthorityHookPolicy::kMoveToImplVrRva || moveToImpl.address() != moduleBase + ActorAuthorityHookPolicy::kMoveToImplVrRva ||
            rootMotionControllerProcessor.offset() != ActorAuthorityHookPolicy::kRootMotionControllerProcessorVrRva ||
            rootMotionControllerProcessor.address() != moduleBase + ActorAuthorityHookPolicy::kRootMotionControllerProcessorVrRva ||
            rotateX.offset() != ActorAuthorityHookPolicy::kRotateXVrRva || rotateX.address() != moduleBase + ActorAuthorityHookPolicy::kRotateXVrRva ||
            rotateY.offset() != ActorAuthorityHookPolicy::kRotateYVrRva || rotateY.address() != moduleBase + ActorAuthorityHookPolicy::kRotateYVrRva ||
            rotateZ.offset() != ActorAuthorityHookPolicy::kRotateZVrRva || rotateZ.address() != moduleBase + ActorAuthorityHookPolicy::kRotateZVrRva ||
            !IsVerifiedTarget(doDamage.address(), ActorAuthorityHookPolicy::kDoDamageVrPrologue) ||
            !IsVerifiedTarget(addDeathItems.address(), ActorAuthorityHookPolicy::kAddDeathItemsVrPrologue) ||
            !IsVerifiedTarget(applyValueActiveEffect.address(), ActorAuthorityHookPolicy::kApplyValueActiveEffectVrPrologue) ||
            !IsVerifiedTarget(restoreActorValue.address(), ActorAuthorityHookPolicy::kRestoreActorValueVrPrologue) ||
            !IsVerifiedTarget(predictLethalDoDamage.address(), ActorAuthorityHookPolicy::kPredictLethalDoDamageVrPrologue) ||
            !IsVerifiedTarget(genericSetPosition.address(), ActorAuthorityHookPolicy::kGenericSetPositionVrPrologue) ||
            !IsVerifiedTarget(actorSetPosition.address(), ActorAuthorityHookPolicy::kActorSetPositionVrPrologue) ||
            !IsVerifiedTarget(moveToImpl.address(), ActorAuthorityHookPolicy::kMoveToImplVrPrologue) ||
            !IsVerifiedTarget(rootMotionControllerProcessor.address(), ActorAuthorityHookPolicy::kRootMotionControllerProcessorVrPrologue) ||
            !IsVerifiedTarget(rotateX.address(), ActorAuthorityHookPolicy::kRotateXVrPrologue) ||
            !IsVerifiedTarget(rotateY.address(), ActorAuthorityHookPolicy::kRotateYVrPrologue) ||
            !IsVerifiedTarget(rotateZ.address(), ActorAuthorityHookPolicy::kRotateZVrPrologue))
        {
            SKSE::log::error(
                "SkyrimTogetherVRGameplayBridge: Actor authority targets failed verification (damage=0x{:X}, loot=0x{:X}, activeEffect=0x{:X}, restore=0x{:X}, lethal=0x{:X}, "
                "referenceSetPosition=0x{:X}, actorSetPosition=0x{:X}, moveTo=0x{:X}, actorProcess=0x{:X}, rotateX=0x{:X}, rotateY=0x{:X}, rotateZ=0x{:X})",
                doDamage.offset(), addDeathItems.offset(), applyValueActiveEffect.offset(), restoreActorValue.offset(), predictLethalDoDamage.offset(), genericSetPosition.offset(),
                actorSetPosition.offset(), moveToImpl.offset(), rootMotionControllerProcessor.offset(), rotateX.offset(), rotateY.offset(), rotateZ.offset());
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_doDamageHook.Target = reinterpret_cast<void*>(doDamage.address());
        g_addDeathItemsHook.Target = reinterpret_cast<void*>(addDeathItems.address());
        g_applyValueActiveEffectHook.Target = reinterpret_cast<void*>(applyValueActiveEffect.address());
        g_restoreActorValueHook.Target = reinterpret_cast<void*>(restoreActorValue.address());
        g_genericSetPositionHook.Target = reinterpret_cast<void*>(genericSetPosition.address());
        g_actorSetPositionHook.Target = reinterpret_cast<void*>(actorSetPosition.address());
        g_moveToImplHook.Target = reinterpret_cast<void*>(moveToImpl.address());
        g_rootMotionControllerProcessorHook.Target = reinterpret_cast<void*>(rootMotionControllerProcessor.address());
        g_rotateXHook.Target = reinterpret_cast<void*>(rotateX.address());
        g_rotateYHook.Target = reinterpret_cast<void*>(rotateY.address());
        g_rotateZHook.Target = reinterpret_cast<void*>(rotateZ.address());
        g_predictLethalDoDamage = predictLethalDoDamage.get();
        if (!CreateAndEnableHook(g_doDamageHook, reinterpret_cast<void*>(&HookDoDamage), reinterpret_cast<void**>(&g_originalDoDamage)) ||
            !CreateAndEnableHook(g_addDeathItemsHook, reinterpret_cast<void*>(&HookAddDeathItems), reinterpret_cast<void**>(&g_originalAddDeathItems)) ||
            !CreateAndEnableHook(g_applyValueActiveEffectHook, reinterpret_cast<void*>(&HookApplyValueActiveEffect), reinterpret_cast<void**>(&g_originalApplyValueActiveEffect)) ||
            !CreateAndEnableHook(g_restoreActorValueHook, reinterpret_cast<void*>(&HookRestoreActorValue), reinterpret_cast<void**>(&g_originalRestoreActorValue)) ||
            !CreateAndEnableHook(g_genericSetPositionHook, reinterpret_cast<void*>(&HookGenericSetPosition), reinterpret_cast<void**>(&g_originalGenericSetPosition)) ||
            !CreateAndEnableHook(g_actorSetPositionHook, reinterpret_cast<void*>(&HookActorSetPosition), reinterpret_cast<void**>(&g_originalActorSetPosition)) ||
            !CreateAndEnableHook(g_moveToImplHook, reinterpret_cast<void*>(&HookMoveToImpl), reinterpret_cast<void**>(&g_originalMoveToImpl)) ||
            !CreateAndEnableHook(
                g_rootMotionControllerProcessorHook,
                reinterpret_cast<void*>(&HookRootMotionControllerProcessor),
                reinterpret_cast<void**>(&g_originalRootMotionControllerProcessor)) ||
            !CreateAndEnableHook(g_rotateXHook, reinterpret_cast<void*>(&HookRotateX), reinterpret_cast<void**>(&g_originalRotateX)) ||
            !CreateAndEnableHook(g_rotateYHook, reinterpret_cast<void*>(&HookRotateY), reinterpret_cast<void**>(&g_originalRotateY)) ||
            !CreateAndEnableHook(g_rotateZHook, reinterpret_cast<void*>(&HookRotateZ), reinterpret_cast<void**>(&g_originalRotateZ)))
        {
            if (RollbackInstall())
                return false;
            // A retained detour keeps the faulted module resident, but must
            // never let the caller continue as though installation succeeded.
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: installed verified actor authority hooks (DoDamage, AddDeathItems, ActiveEffect::ApplyValue, RestoreActorValue, "
            "TESObjectREFR::SetPosition, Actor::SetPosition, TESObjectREFR::MoveTo_Impl, Actor::Process, TESObjectREFR::RotateX, "
            "TESObjectREFR::RotateY, TESObjectREFR::RotateZ)");
        return true;
    }
    catch (...)
    {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("actor authority hook target resolution threw"); });
        NoThrow::BestEffort([] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: actor authority hook installation rejected an exception"); });
        if (RollbackInstall())
            return false;
        return false;
    }
}

void* GetVerifiedMoveToImplTrampoline() noexcept
{
    if (!g_installed.load(std::memory_order_acquire))
        return nullptr;
    return reinterpret_cast<void*>(g_originalMoveToImpl);
}

bool Uninstall() noexcept
{
    try {
    const bool rotateZDetached = DetachHook(g_rotateZHook);
    const bool rotateYDetached = DetachHook(g_rotateYHook);
    const bool rotateXDetached = DetachHook(g_rotateXHook);
    const bool rootMotionControllerProcessorDetached = DetachHook(g_rootMotionControllerProcessorHook);
    const bool moveToImplDetached = DetachHook(g_moveToImplHook);
    const bool actorSetPositionDetached = DetachHook(g_actorSetPositionHook);
    const bool genericSetPositionDetached = DetachHook(g_genericSetPositionHook);
    const bool restoreActorValueDetached = DetachHook(g_restoreActorValueHook);
    const bool applyValueActiveEffectDetached = DetachHook(g_applyValueActiveEffectHook);
    const bool deathItemsDetached = DetachHook(g_addDeathItemsHook);
    const bool damageDetached = DetachHook(g_doDamageHook);
    if (!rotateZDetached || !rotateYDetached || !rotateXDetached || !rootMotionControllerProcessorDetached || !moveToImplDetached || !actorSetPositionDetached || !genericSetPositionDetached || !restoreActorValueDetached ||
        !applyValueActiveEffectDetached || !deathItemsDetached || !damageDetached)
    {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("actor authority hook uninstall could not prove detachment"); });
        NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: actor authority hook uninstall incomplete; preserving callable trampolines"); });
        return false;
    }

    const auto suppressedDamage = g_suppressedDamage.load(std::memory_order_relaxed);
    const auto suppressedDeathItems = g_suppressedDeathItems.load(std::memory_order_relaxed);
    const auto suppressedPositiveActiveEffectHealth = g_suppressedPositiveActiveEffectHealth.load(std::memory_order_relaxed);
    const auto suppressedRestoreHealth = g_suppressedRestoreHealth.load(std::memory_order_relaxed);
    const auto publishedRemoteNpcHealthDelta = g_publishedRemoteNpcHealthDelta.load(std::memory_order_relaxed);
    const auto failedRemoteNpcHealthDeltaPublication = g_failedRemoteNpcHealthDeltaPublication.load(std::memory_order_relaxed);
    const auto suppressedGenericSetPosition = g_suppressedGenericSetPosition.load(std::memory_order_relaxed);
    const auto suppressedActorSetPosition = g_suppressedActorSetPosition.load(std::memory_order_relaxed);
    const auto suppressedMoveToImpl = g_suppressedMoveToImpl.load(std::memory_order_relaxed);
    const auto suppressedRootMotionControllerProcessor = g_suppressedRootMotionControllerProcessor.load(std::memory_order_relaxed);
    const auto suppressedRotateX = g_suppressedRotateX.load(std::memory_order_relaxed);
    const auto suppressedRotateY = g_suppressedRotateY.load(std::memory_order_relaxed);
    const auto suppressedRotateZ = g_suppressedRotateZ.load(std::memory_order_relaxed);
    if (g_doDamageHook.Target || g_addDeathItemsHook.Target || g_applyValueActiveEffectHook.Target || g_restoreActorValueHook.Target || g_genericSetPositionHook.Target ||
        g_actorSetPositionHook.Target || g_moveToImplHook.Target || g_rootMotionControllerProcessorHook.Target || g_rotateXHook.Target || g_rotateYHook.Target ||
        g_rotateZHook.Target)
    {
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: removed actor authority hooks (suppressed damage={}, "
            "deathItems={}, positiveActiveEffectHealth={}, restoreHealth={}, referenceSetPosition={}, actorSetPosition={}, moveTo={}, actorProcess={}, rotateX={}, rotateY={}, rotateZ={}, "
            "published remoteNpcHealthDelta={}, failed remoteNpcHealthDelta={})",
            suppressedDamage, suppressedDeathItems, suppressedPositiveActiveEffectHealth, suppressedRestoreHealth,
            suppressedGenericSetPosition, suppressedActorSetPosition, suppressedMoveToImpl, suppressedRootMotionControllerProcessor, suppressedRotateX, suppressedRotateY,
            suppressedRotateZ, publishedRemoteNpcHealthDelta,
            failedRemoteNpcHealthDeltaPublication);
    }
    ClearDetachedState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
    } catch (...) {
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks
