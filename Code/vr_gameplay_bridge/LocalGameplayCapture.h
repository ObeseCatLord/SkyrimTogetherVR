#pragma once

#include <cstdint>

#include <vr_common/VRGameplayBridge.h>

namespace RE
{
class NiPoint3;
class PlayerCharacter;
class TESQuest;
class TESObjectREFR;
class TESWorldSpace;
}

namespace SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture
{
namespace ActivationPolicy
{
inline constexpr std::uint32_t kPlayerReferenceFormId = 0x14;
inline constexpr std::int32_t kMaximumOpenState = 4;

struct EncodedActivator
{
    GameplayBridge::AdapterHandle TargetHandle{};
    std::uint32_t TargetLocalFormId{};
};

// Activation capture is deliberately local-player-only. ActivateRef also runs
// on engine worker threads for NPC/scripted activations, where touching native
// target state from this detour is not safe.
[[nodiscard]] constexpr bool IsBoundedLocalReferenceFormId(const std::uint32_t a_formId) noexcept
{
    return a_formId != 0 && a_formId != 0xFFFFFFFFu;
}

[[nodiscard]] constexpr bool ShouldCapturePreActivation(
    const bool a_activatorIsActor,
    const std::uint32_t a_activatorFormId,
    const bool a_activatorIsLocalPlayer,
    const bool a_validTarget,
    const bool a_validBase,
    const bool a_validCell,
    const bool a_isBook) noexcept
{
    return a_activatorIsActor && a_activatorIsLocalPlayer &&
           a_activatorFormId == kPlayerReferenceFormId &&
           a_validTarget && a_validBase && a_validCell && !a_isBook;
}

[[nodiscard]] constexpr EncodedActivator EncodeActivator(
    const bool a_activatorIsLocalPlayer,
    const std::uint32_t a_activatorFormId) noexcept
{
    return {
        a_activatorIsLocalPlayer ? GameplayBridge::kLocalPlayerHandle : GameplayBridge::AdapterHandle{},
        a_activatorFormId,
    };
}

[[nodiscard]] constexpr bool IsValidEncodedActivator(
    const GameplayBridge::AdapterHandle a_targetHandle,
    const std::uint32_t a_targetLocalFormId) noexcept
{
    if (!IsBoundedLocalReferenceFormId(a_targetLocalFormId))
        return false;
    if (a_targetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value)
        return a_targetLocalFormId == kPlayerReferenceFormId;
    return a_targetHandle.Value == 0 && a_targetLocalFormId != kPlayerReferenceFormId;
}
} // namespace ActivationPolicy

// These entry points are owned by the bridge's game-thread integration.  They
// never expose game pointers or variable-size game data through the bridge.
void Initialize() noexcept;
// Local gameplay capture remains dormant until the mapped client explicitly
// arms it for the current session.
void Arm() noexcept;
// Scope engine-driven remote inventory application so the synchronous
// TESContainerChangedEvent it produces cannot echo back through capture.
class ScopedRemoteInventorySuppression
{
public:
    ScopedRemoteInventorySuppression() noexcept;
    ~ScopedRemoteInventorySuppression() noexcept;

    ScopedRemoteInventorySuppression(const ScopedRemoteInventorySuppression&) = delete;
    ScopedRemoteInventorySuppression& operator=(const ScopedRemoteInventorySuppression&) = delete;
};
[[nodiscard]] bool IsRemoteInventorySuppressed() noexcept;
// Refreshes only the local semantic baseline after an inbound inventory
// application has succeeded on the game thread.
void RefreshInventoryBaseline(std::uint32_t a_ownerFormId) noexcept;
[[nodiscard]] GameplayBridge::CommandStatus CaptureAssignmentBootstrap(
    const GameplayBridge::CommandRecord& acCommand) noexcept;
void CapturePeriodic() noexcept;
enum class PreActivationCaptureResult : std::uint8_t
{
    Captured,
    Ineligible,
};
struct PendingActivationCapture
{
    GameplayBridge::GameplayActionPayload Payload{};
    bool Valid{};
};
enum class PostActivationCaptureResult : std::uint8_t
{
    Published,
    Ineligible,
    PublicationRejected,
};
// The exact TESObjectREFR::ActivateRef detour retains its pre-state in this
// stack-owned value before calling the original engine body. It contains only
// copied wire data and never retains a game pointer.
[[nodiscard]] PreActivationCaptureResult CapturePreActivation(
    RE::TESObjectREFR& a_target,
    RE::TESObjectREFR& a_activator,
    PendingActivationCapture& ar_capture) noexcept;
// Called only when the original ActivateRef body accepted the activation. The
// post-state is sampled immediately from the still-synchronous target and then
// published together with the retained pre-state.
[[nodiscard]] PostActivationCaptureResult CapturePostActivation(
    const PendingActivationCapture& ac_capture,
    RE::TESObjectREFR& a_target) noexcept;
enum class ExactWaypointCaptureResult : std::uint8_t
{
    Published,
    Duplicate,
    Rejected,
};
// The exact waypoint detours call these only after the native body has
// established its marker postcondition. Each function updates the matching
// periodic baseline only after the bridge accepts the event.
[[nodiscard]] ExactWaypointCaptureResult CaptureExactWaypointSet(
    const RE::PlayerCharacter& a_player,
    const RE::TESWorldSpace& a_worldspace,
    const RE::NiPoint3& a_position) noexcept;
[[nodiscard]] ExactWaypointCaptureResult CaptureExactWaypointRemove(
    const RE::PlayerCharacter& a_player) noexcept;
// A confirmed inbound native mutation advances only the local repair baseline;
// it deliberately emits no bridge event and therefore cannot echo remotely.
void AcknowledgeRemoteWaypointSet(
    const RE::TESWorldSpace& a_worldspace,
    const RE::NiPoint3& a_position) noexcept;
void AcknowledgeRemoteWaypointRemove() noexcept;
// Records the actual locally observed quest status and stage after an inbound
// mutation has partially committed. It first tries the event ring and then
// durably coalesces the latest state in a bounded session-keyed backlog.
// False means neither path accepted the authoritative reconciliation.
[[nodiscard]] bool PublishQuestReconciliation(RE::TESQuest& a_quest) noexcept;
// These tokens suppress the exact CommonLib quest event generated by an
// inbound mutation. They are short-lived and must be cancelled if that
// mutation is rejected synchronously.
using QuestSuppressionToken = std::uint64_t;
[[nodiscard]] QuestSuppressionToken ArmQuestStartStopSuppression(
    std::uint32_t a_questLocalFormId,
    bool a_started) noexcept;
[[nodiscard]] QuestSuppressionToken ArmQuestStageSuppression(
    std::uint32_t a_questLocalFormId,
    std::uint16_t a_stage) noexcept;
void CancelQuestSuppression(QuestSuppressionToken a_token) noexcept;
using LockSuppressionToken = std::uint64_t;
[[nodiscard]] LockSuppressionToken ArmLockSuppression(
    std::uint32_t a_referenceLocalFormId, bool a_locked, std::uint8_t a_lockLevel) noexcept;
void CancelLockSuppression(LockSuppressionToken a_token) noexcept;
// Inbound party XP is applied through the same engine API observed by the
// polling path. Suppress the next change for that skill so it is not echoed
// back to the server; the suppression expires if the engine rejects it.
[[nodiscard]] bool ArmExperienceSuppression(std::uint32_t a_actorValue) noexcept;
// Called by the verified AddSkillExperience detour after the original engine
// mutation. It consumes an inbound suppression when present and advances the
// polling baseline only when the exact event was intentionally suppressed or
// durably accepted, leaving a rejected local event for polling recovery.
[[nodiscard]] bool CaptureExactExperience(
    RE::PlayerCharacter& a_player,
    std::uint32_t a_actorValue,
    float a_previousExperience,
    float a_currentExperience,
    bool a_remoteApplication) noexcept;
// Publishes only the observed post-minus-pre health delta from the verified
// local-player DoDamage hook against a managed remote NPC. This deliberately
// uses no player target handle and has no acknowledgement shortcut.
[[nodiscard]] bool PublishTargetedRemoteNpcHealthDelta(
    std::uint32_t a_targetLocalFormId, float a_delta) noexcept;
// Observation is keyed only by stable local reference form IDs.  The capture
// module resolves each ID afresh on the game thread; no native pointer crosses
// the mapped ABI or survives a lifecycle reset.
bool StartNpcObservation(std::uint32_t a_localReferenceFormId) noexcept;
void StopNpcObservation(std::uint32_t a_localReferenceFormId) noexcept;
[[nodiscard]] bool IsNpcObserved(std::uint32_t a_localReferenceFormId) noexcept;
// Called by the exact VR Actor::SpeakSound hook before native presentation.
// Only a locally observed NPC can have a mapped server actor ID; no game
// pointer crosses this boundary.
bool CaptureDialogueVoice(std::uint32_t a_localActorFormId, const char* a_resourcePath) noexcept;
// Called after the verified MenuTopicManager::PlayDialogueOption body accepts
// a local index. The text and opaque baseline identity are captured before
// that engine call; a successful publication advances the polling baseline
// without dereferencing the identity after engine mutation.
bool CaptureExactDialogueChoice(
    const void* a_baselineDialogue,
    const char* a_text) noexcept;
void Reset() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture
