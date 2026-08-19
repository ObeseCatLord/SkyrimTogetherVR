#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHookPolicy
{
// These are direct Skyrim VR Address Library IDs.  They are intentionally not
// desktop IDs translated through an automated alias table.
inline constexpr std::uint64_t kDoDamageAddressLibraryId = 36345;
inline constexpr std::uint64_t kDoDamageVrRva = 0x05DE930;
inline constexpr std::array<std::uint8_t, 30> kDoDamageVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x50,
    0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x7C, 0x24, 0x30, 0x48, 0x81, 0xC1, 0x98, 0x00, 0x00, 0x00,
};

inline constexpr std::uint64_t kAddDeathItemsAddressLibraryId = 36218;
inline constexpr std::uint64_t kAddDeathItemsVrRva = 0x05D80A0;
inline constexpr std::array<std::uint8_t, 19> kAddDeathItemsVrPrologue{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70,
};

// This entry has no trusted VR Address Library ID. Resolve it directly from
// the SkyrimVR.exe image only after the exact runtime, RVA, executable span,
// and prologue checks succeed.
inline constexpr std::uint64_t kApplyValueActiveEffectVrRva = 0x056E070;
inline constexpr std::array<std::uint8_t, 32> kApplyValueActiveEffectVrPrologue{
    0x48, 0x85, 0xD2, 0x0F, 0x84, 0x3D, 0x03, 0x00, 0x00, 0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0xC7, 0x40, 0xA0, 0xFE, 0xFF, 0xFF,
};

inline constexpr std::uint64_t kRestoreActorValueAddressLibraryId = 37513;
inline constexpr std::uint64_t kRestoreActorValueVrRva = 0x06296B0;
// The supplied byte sequence is 32 bytes (including its final 0xDA), despite
// the accompanying length label saying 31; validate the complete sequence.
inline constexpr std::array<std::uint8_t, 32> kRestoreActorValueVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x0F, 0x29, 0x74, 0x24, 0x30, 0x48,
    0x8B, 0xF9, 0x0F, 0x29, 0x7C, 0x24, 0x20, 0x0F, 0x28, 0xF2, 0x0F, 0x57, 0xFF, 0x48, 0x63, 0xDA,
};

inline constexpr std::uint64_t kPredictLethalDoDamageVrRva = 0x05ECEA0;
inline constexpr std::array<std::uint8_t, 32> kPredictLethalDoDamageVrPrologue{
    0x48, 0x83, 0xEC, 0x48, 0x48, 0x3B, 0x0D, 0xFD, 0x77, 0x9D, 0x02, 0xBA, 0x18, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x05, 0x39, 0xEB, 0x9F, 0x02, 0x0F, 0x29, 0x74, 0x24, 0x30, 0x41, 0x0F, 0x94, 0xC0,
};

// Remote-root authority is pinned to the exact Skyrim VR 1.4.15.0 entry
// points. These targets have no trusted VR Address Library IDs and are
// resolved directly from the verified executable RVAs.
inline constexpr std::uint64_t kGenericSetPositionVrRva = 0x002A8010;
inline constexpr std::array<std::uint8_t, 21> kGenericSetPositionVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x20,
};

inline constexpr std::uint64_t kActorSetPositionVrRva = 0x005DC380;
inline constexpr std::array<std::uint8_t, 20> kActorSetPositionVrPrologue{
    0x40, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
};

inline constexpr std::uint64_t kMoveToImplVrRva = 0x009E90E0;
inline constexpr std::array<std::uint8_t, 16> kMoveToImplVrPrologue{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};

inline constexpr std::uint64_t kRootMotionControllerProcessorVrRva = 0x005E0E20;
inline constexpr std::array<std::uint8_t, 28> kRootMotionControllerProcessorVrPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0xA8, 0xFE, 0xFF,
    0xFF, 0x48, 0x81, 0xEC, 0x20, 0x02, 0x00, 0x00,
};

// These three direct entry points are TESObjectREFR::RotateX/Y/Z in Skyrim VR
// 1.4.15.0. The generated VR address-library numeric IDs in this range collide
// with unrelated rows, so they must never be resolved through REL::ID.
inline constexpr std::uint64_t kRotateXVrRva = 0x002A7D80;
inline constexpr std::array<std::uint8_t, 21> kRotateXVrPrologue{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0x0F, 0x2E, 0x71, 0x48,
};

inline constexpr std::uint64_t kRotateYVrRva = 0x002A7E40;
inline constexpr std::array<std::uint8_t, 21> kRotateYVrPrologue{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0x0F, 0x2E, 0x71, 0x4C,
};

inline constexpr std::uint64_t kRotateZVrRva = 0x002A7F00;
inline constexpr std::array<std::uint8_t, 21> kRotateZVrPrologue{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28, 0xF1, 0x0F, 0x2E, 0x71, 0x50,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kDoDamageAddressLibraryId == 36345 && kDoDamageVrRva == 0x05DE930 && kDoDamageVrPrologue.size() == 30 && kAddDeathItemsAddressLibraryId == 36218 &&
           kAddDeathItemsVrRva == 0x05D80A0 && kAddDeathItemsVrPrologue.size() == 19 && kApplyValueActiveEffectVrRva == 0x056E070 &&
           kApplyValueActiveEffectVrPrologue.size() == 32 && kRestoreActorValueAddressLibraryId == 37513 && kRestoreActorValueVrRva == 0x06296B0 &&
           kRestoreActorValueVrPrologue.size() == 32 && kPredictLethalDoDamageVrRva == 0x05ECEA0 && kPredictLethalDoDamageVrPrologue.size() == 32 &&
           kGenericSetPositionVrRva == 0x002A8010 && kGenericSetPositionVrPrologue.size() == 21 && kActorSetPositionVrRva == 0x005DC380 &&
           kActorSetPositionVrPrologue.size() == 20 && kMoveToImplVrRva == 0x009E90E0 && kMoveToImplVrPrologue.size() == 16 &&
           kRootMotionControllerProcessorVrRva == 0x005E0E20 &&
           kRootMotionControllerProcessorVrPrologue.size() == 28 && kRotateXVrRva == 0x002A7D80 && kRotateXVrPrologue.size() == 21 &&
           kRotateYVrRva == 0x002A7E40 && kRotateYVrPrologue.size() == 21 && kRotateZVrRva == 0x002A7F00 && kRotateZVrPrologue.size() == 21;
}

inline constexpr std::uint32_t kHealthActorValue = 24;
inline constexpr std::uint32_t kUseActiveEffectActorValue = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t kPlayerReferenceFormId = 0x14;
inline constexpr float kMaximumHealthDeltaMagnitude = 1'000'000.0F;

enum class DoDamageDisposition : std::uint8_t
{
    CallOriginal,
    CallOriginalAndPublishRemoteNpcHealthDelta,
    SuppressWithoutOriginal,
    SuppressWithPredictedLethal,
};

struct DoDamageContext
{
    bool TargetManagedRemoteActor{};
    bool TargetManagedRemotePlayerActor{};
    bool TargetRetiring{};
    bool TargetIsLocalPlayer{};
    bool SourceIsLocalPlayer{};
    bool SourceIsManagedRemotePlayerActor{};
    bool SourceRetiring{};
    bool PvpEnabled{};
};

// Match the desktop source/target ordering. A remote player cannot damage the
// local player while PvP is disabled, a remote player never locally damages an
// NPC, and a local player's hit against a managed remote NPC is the only
// attacker-originated health-delta publication path.
[[nodiscard]] constexpr DoDamageDisposition ClassifyDoDamage(const DoDamageContext a_context) noexcept
{
    // Teardown has exclusive actor ownership. Never dereference or mutate an
    // actor after its registry entry has begun retiring.
    if (a_context.TargetRetiring || a_context.SourceRetiring)
        return DoDamageDisposition::SuppressWithoutOriginal;
    if (a_context.TargetIsLocalPlayer)
    {
        if (a_context.SourceIsManagedRemotePlayerActor && !a_context.PvpEnabled)
            return DoDamageDisposition::SuppressWithoutOriginal;
        return DoDamageDisposition::CallOriginal;
    }
    if (a_context.TargetManagedRemotePlayerActor)
        return DoDamageDisposition::SuppressWithPredictedLethal;
    if (a_context.TargetManagedRemoteActor && a_context.SourceIsLocalPlayer)
        return DoDamageDisposition::CallOriginalAndPublishRemoteNpcHealthDelta;
    if (a_context.TargetManagedRemoteActor || a_context.SourceIsManagedRemotePlayerActor)
        return DoDamageDisposition::SuppressWithPredictedLethal;
    return DoDamageDisposition::CallOriginal;
}

struct TargetedRemoteNpcHealthDelta
{
    std::uint64_t TargetHandle{};
    std::uint32_t TargetLocalFormId{};
    std::uint32_t ActorValue{};
    float Delta{};
};

// The only bridge event allowed to target a non-player actor is the exact
// remote-NPC health delta emitted by the verified DoDamage hook. Comparisons
// reject NaN and infinities without relying on a runtime math helper.
[[nodiscard]] constexpr bool IsValidTargetedRemoteNpcHealthDelta(const TargetedRemoteNpcHealthDelta a_delta) noexcept
{
    if (a_delta.TargetHandle != 0)
        return false;
    if (a_delta.TargetLocalFormId == 0)
        return false;
    if (a_delta.TargetLocalFormId == kPlayerReferenceFormId)
        return false;
    if (a_delta.TargetLocalFormId == std::numeric_limits<std::uint32_t>::max())
        return false;
    if (a_delta.ActorValue != kHealthActorValue || a_delta.Delta == 0.0F)
        return false;
    return a_delta.Delta >= -kMaximumHealthDeltaMagnitude && a_delta.Delta <= kMaximumHealthDeltaMagnitude;
}

// Generated death inventory is local side-effect state. It remains blocked
// for every bridge-managed actor until server inventory replay owns it.
[[nodiscard]] constexpr bool ShouldCallAddDeathItemsOriginal(const bool a_managedRemoteActor) noexcept
{
    return !a_managedRemoteActor;
}

[[nodiscard]] constexpr std::uint32_t ResolveActiveEffectActorValue(
    const std::uint32_t a_effectActorValue,
    const std::uint32_t a_actorValueOverride) noexcept
{
    return a_actorValueOverride == kUseActiveEffectActorValue ? a_effectActorValue : a_actorValueOverride;
}

[[nodiscard]] constexpr bool ShouldCallApplyValueActiveEffectOriginal(
    const bool a_managedRemoteActor,
    const float a_value,
    const std::uint32_t a_effectiveActorValue) noexcept
{
    return !a_managedRemoteActor || a_value <= 0.0F || a_effectiveActorValue != kHealthActorValue;
}

[[nodiscard]] constexpr bool ShouldCallRestoreActorValueOriginal(
    const bool a_managedRemoteActor,
    const std::int32_t a_actorValue) noexcept
{
    return !a_managedRemoteActor || a_actorValue != static_cast<std::int32_t>(kHealthActorValue);
}

// Root authority is strictly local to AvatarManager's owner-thread replay.
// Any engine-originated root mutation against a managed remote actor is
// rejected, while generic references and unmanaged actors remain untouched.
[[nodiscard]] constexpr bool ShouldCallRemoteRootMutationOriginal(
    const bool a_managedRemoteActor,
    const bool a_authoritativeReplayAllowance) noexcept
{
    return !a_managedRemoteActor || a_authoritativeReplayAllowance;
}

// Rotation writes use the same ownership boundary as root position writes.
// The target is a TESObjectREFR so non-actors and unmanaged actors preserve
// their native behavior; managed remote actors accept only bridge replay.
[[nodiscard]] constexpr bool ShouldCallRemoteRotationOriginal(
    const bool a_managedRemoteActor,
    const bool a_authoritativeReplayAllowance) noexcept
{
    return !a_managedRemoteActor || a_authoritativeReplayAllowance;
}

enum class InvisibilityCorrectionDisposition : std::uint8_t
{
    Correct,
    InvalidActor,
    LocalPlayer,
    NotManagedRemote,
    Retiring,
};

// Classify before mutation so the runtime operation can retain its registry
// reader lease over the only actor dereference in this correction.
[[nodiscard]] constexpr InvisibilityCorrectionDisposition ClassifyInvisibilityCorrection(
    const bool a_actorPresent,
    const bool a_isLocalPlayer,
    const bool a_isManagedRemoteActor,
    const bool a_isRetiring) noexcept
{
    if (!a_actorPresent)
        return InvisibilityCorrectionDisposition::InvalidActor;
    if (a_isLocalPlayer)
        return InvisibilityCorrectionDisposition::LocalPlayer;
    if (a_isRetiring)
        return InvisibilityCorrectionDisposition::Retiring;
    if (!a_isManagedRemoteActor)
        return InvisibilityCorrectionDisposition::NotManagedRemote;
    return InvisibilityCorrectionDisposition::Correct;
}

// The helper's delta is signed and its inputs must be finite before the
// detour treats its prediction as meaningful.
[[nodiscard]] constexpr bool IsPredictedDoDamageLethal(const float a_currentHealth, const float a_signedDelta) noexcept
{
    if (a_currentHealth != a_currentHealth || a_signedDelta != a_signedDelta ||
        a_currentHealth == std::numeric_limits<float>::infinity() || a_currentHealth == -std::numeric_limits<float>::infinity() ||
        a_signedDelta == std::numeric_limits<float>::infinity() || a_signedDelta == -std::numeric_limits<float>::infinity())
        return false;

    if (a_signedDelta < 0.0F)
    {
        // Reject a negative overflow without evaluating the predicted sum.
        if (a_currentHealth < std::numeric_limits<float>::lowest() - a_signedDelta)
            return false;
    }

    return a_currentHealth <= -a_signedDelta;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHookPolicy

namespace RE
{
class Actor;
}

namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks
{
inline constexpr std::size_t kManagedRemoteActorRegistryCapacity = 64;

enum class ManagedRemoteActorRetirementResult : std::uint8_t
{
    Quiescent,
    InvalidActor,
    NotRegistered,
    AlreadyRetiring,
    ReaderDrainTimedOut,
    RegistryInconsistent,
};

// The only public operation that combines managed-actor membership and the
// pre-Finish invisibility correction. It keeps the private reader lease alive
// until the actor value has been reset or the operation fails closed.
enum class ManagedRemoteInvisibilityCorrectionResult : std::uint8_t
{
    Corrected,
    InvalidActor,
    LocalPlayer,
    NotManagedRemote,
    Retiring,
    Failed,
};

// A registry operation holds the private reader lease for the complete
// callback. Callers may invoke native code with the supplied actor only from
// inside this callback; this prevents actor retirement from invalidating the
// object between classification and its authoritative trampoline.
enum class ManagedRemoteActorOperationDisposition : std::uint8_t
{
    UnmanagedOrInvalid,
    ManagedRemote,
    Retiring,
};

using ManagedRemoteActorOperation = void (*)(void* a_context, ManagedRemoteActorOperationDisposition a_disposition) noexcept;

// A successful BeginRetire call returns a unique, quiescent registry lease.
// The caller must keep the actor registered and retiring while it restores or
// deletes that actor, then release the lease with FinishRetire. Any other
// result is fail-closed: no actor or form mutation is permitted.
class ManagedRemoteActorRetirement final
{
public:
    ManagedRemoteActorRetirement() noexcept = default;
    ManagedRemoteActorRetirement(const ManagedRemoteActorRetirement&) = delete;
    ManagedRemoteActorRetirement& operator=(const ManagedRemoteActorRetirement&) = delete;

    ManagedRemoteActorRetirement(ManagedRemoteActorRetirement&& a_other) noexcept
        : _result(a_other._result)
        , _slot(a_other._slot)
        , _generation(a_other._generation)
        , _state(a_other._state)
        , _active(a_other._active)
    {
        a_other._active = false;
        a_other._result = ManagedRemoteActorRetirementResult::RegistryInconsistent;
    }

    ManagedRemoteActorRetirement& operator=(ManagedRemoteActorRetirement&&) = delete;

    [[nodiscard]] ManagedRemoteActorRetirementResult Result() const noexcept { return _result; }
    [[nodiscard]] bool IsQuiescent() const noexcept { return _active && _result == ManagedRemoteActorRetirementResult::Quiescent; }

private:
    friend ManagedRemoteActorRetirement BeginRetireManagedRemoteActor(RE::Actor* a_actor) noexcept;
    friend ManagedRemoteActorRetirementResult FinishRetireManagedRemoteActor(ManagedRemoteActorRetirement& ar_retirement) noexcept;

    ManagedRemoteActorRetirementResult _result{ManagedRemoteActorRetirementResult::InvalidActor};
    std::size_t _slot{kManagedRemoteActorRegistryCapacity};
    std::uint32_t _generation{};
    std::uintptr_t _state{};
    bool _active{};
};

// This scope authorizes only AvatarManager's verified root-transform writes.
// Combat, loot, and actor-value authority remain fail-closed while it is held.
class ScopedAuthoritativeReplay final
{
public:
    ScopedAuthoritativeReplay() noexcept;
    ~ScopedAuthoritativeReplay() noexcept;

    ScopedAuthoritativeReplay(const ScopedAuthoritativeReplay&) = delete;
    ScopedAuthoritativeReplay& operator=(const ScopedAuthoritativeReplay&) = delete;
};

// Registration is restricted to AvatarManager's command-pump lifecycle.
// Retirement closes the reader gate, waits for active detours using this exact
// slot to leave, and returns a quiescent token before an actor can be mutated.
[[nodiscard]] bool RegisterManagedRemoteActor(RE::Actor* a_actor, bool a_remotePlayer) noexcept;
[[nodiscard]] ManagedRemoteActorRetirement BeginRetireManagedRemoteActor(RE::Actor* a_actor) noexcept;
[[nodiscard]] ManagedRemoteActorRetirementResult FinishRetireManagedRemoteActor(ManagedRemoteActorRetirement& ar_retirement) noexcept;
[[nodiscard]] bool IsManagedRemoteActor(const RE::Actor* a_actor) noexcept;
[[nodiscard]] bool IsManagedRemotePlayerActor(const RE::Actor* a_actor) noexcept;
[[nodiscard]] ManagedRemoteInvisibilityCorrectionResult CorrectManagedRemoteInvisibilityBeforeFinish(RE::Actor* a_actor) noexcept;
[[nodiscard]] bool WithManagedRemoteActorLease(
    RE::Actor* a_actor,
    ManagedRemoteActorOperation a_operation,
    void* a_context) noexcept;

// Respawn must call the verified pre-detour implementation: the live Skyrim
// entry point is patched by this module after installation and therefore no
// longer has the executable prologue that was validated before hooking.
[[nodiscard]] void* GetVerifiedMoveToImplTrampoline() noexcept;

[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorAuthorityHooks
