#include <catch2/catch.hpp>

#include "../higgs_bridge/HiggsMutationDedup.h"
#include "../vr_common/VRGameplayBridge.h"
#include "../vr_gameplay_bridge/QuestDialogueManager.h"
#include "../vr_gameplay_bridge/QuestNativeAccess.h"
#include <Structs/MovementOrdering.h>

#include <atomic>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>

namespace
{
using namespace SkyrimTogetherVR::GameplayBridge;

EventRecord MakeEvent(const std::uint64_t a_sequence) noexcept
{
    EventRecord event{};
    event.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalPlayerState);
    event.Header.PayloadSize = kFixedPayloadBytes;
    event.Header.Identity.ServerInstanceNonce = 0x1122334455667788ull;
    event.Header.Identity.ConnectionGeneration = 7;
    event.Header.Identity.LifecycleEpoch = 9;
    event.Header.Identity.EntityId = 42;
    event.Header.Identity.EntityGeneration = 3;
    event.Header.Identity.SequenceId = a_sequence;
    event.Payload.LocalPlayerState.LocalPlayerHandle = kLocalPlayerHandle;
    event.Payload.LocalPlayerState.LocalCellFormId = 0x1234;
    event.Payload.LocalPlayerState.LocalWorldspaceFormId = 0x5678;
    event.Payload.LocalPlayerState.Root.PositionX = 1.0f;
    event.Payload.LocalPlayerState.Root.RotationW = 1.0f;
    event.Payload.LocalPlayerState.Root.Scale = 1.0f;
    event.Payload.LocalPlayerState.SnapshotFlags = 1;
    event.Payload.LocalPlayerState.LocalActorBaseFormId = 0x7;
    return event;
}

CommandRecord MakeGameplayCommand(
    const CommandKind a_kind,
    const GameplayDomain a_domain,
    const GameplayAction a_action) noexcept
{
    CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(a_kind);
    command.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(a_domain);
    command.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(a_action);
    return command;
}

std::string ReadRepositorySource(const std::filesystem::path& a_relativePath)
{
    auto directory = std::filesystem::current_path();
    while (true) {
        const auto candidate = directory / a_relativePath;
        if (std::filesystem::is_regular_file(candidate)) {
            std::ifstream file(candidate, std::ios::binary);
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }

        const auto parent = directory.parent_path();
        if (parent == directory)
            return {};
        directory = parent;
    }
}

std::size_t CountOccurrences(const std::string& a_text, const std::string& a_needle)
{
    std::size_t count{};
    for (std::size_t offset{}; (offset = a_text.find(a_needle, offset)) != std::string::npos;
         offset += a_needle.size())
        ++count;
    return count;
}
} // namespace

TEST_CASE("VR gameplay bridge ABI constants and layout", "[skyrim-vr][gameplay-bridge]")
{
    REQUIRE(kMappingMagic == 0x42564753);
    REQUIRE(kMappingAbiVersion == 23);
    REQUIRE(kCapabilityRevision == 34);
    REQUIRE(kSkyrimVrRuntimeVersion == 0x010400F0);
    REQUIRE(kSkseVrInterfaceRuntimeVersion == 0x010400F1);
    REQUIRE(kSkseVrInterfaceRuntimeVersion != kSkyrimVrRuntimeVersion);
    REQUIRE(kMinimumSkseVrVersion == 0x020000C0);
    REQUIRE(kMinimumSkseVrReleaseIndex == 60);
    const std::array<std::uint32_t, 3> expectedEssentialActorValues{24, 25, 26};
    REQUIRE(kEssentialAssignmentActorValues == expectedEssentialActorValues);
    REQUIRE(kMinimumAssignmentBootstrapRecords == 8);
    REQUIRE(IsEssentialAssignmentActorValue(24));
    REQUIRE_FALSE(IsEssentialAssignmentActorValue(23));
    REQUIRE(static_cast<std::int32_t>(AssignmentBootstrapFailureReason::EssentialActorValues) == 2);
    REQUIRE(static_cast<std::int32_t>(AssignmentBootstrapFailureReason::Exception) == 9);
    REQUIRE(IsKnownAssignmentBootstrapFailureReason(
        static_cast<std::int32_t>(AssignmentBootstrapFailureReason::Tint)));
    REQUIRE_FALSE(IsKnownAssignmentBootstrapFailureReason(0));
    REQUIRE(static_cast<std::uint32_t>(CommandStatus::Degraded) == 13);
    REQUIRE(SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess::kSetStageSeId == 24482);
    REQUIRE(SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess::kSetStageAeId == 25004);
    REQUIRE(SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess::kSetStageVrRva == 0x03803D0);
    REQUIRE(kFixedPayloadBytes == 80);
    REQUIRE(kDefaultEventRingCapacity == 2048);
    REQUIRE(kDefaultCommandRingCapacity == 2048);
    REQUIRE(kLocalPlayerHandle.Value == 1);
    REQUIRE(kFirstRemoteAvatarHandle == 2);
    REQUIRE(sizeof(MessageHeader) == 0x40);
    REQUIRE(sizeof(EventRecord) == 0x90);
    REQUIRE(sizeof(CommandRecord) == 0x90);
    REQUIRE(sizeof(ApplyProjectileLaunchPayload) == kFixedPayloadBytes);
    REQUIRE(sizeof(ActorActionPayload) == kFixedPayloadBytes);
    REQUIRE(sizeof(ActorActionGraphChunkPayload) == kFixedPayloadBytes);
    REQUIRE(offsetof(RemoteAvatarStatePayload, LocalActorReferenceFormId) == 0x18);
    REQUIRE(offsetof(RemoteAvatarStatePayload, Root) == 0x1C);
    REQUIRE(offsetof(ApplyProjectileLaunchPayload, LocalParentCellFormId) == 0x18);
    REQUIRE(offsetof(ApplyProjectileLaunchPayload, LaunchFlags) == 0x40);
    REQUIRE(offsetof(ActorActionPayload, SnapshotId) == 0x28);
    REQUIRE(offsetof(ActorActionPayload, TextId) == 0x30);
    REQUIRE(offsetof(ActorActionGraphChunkPayload, Values) == 0x2C);
    REQUIRE(kProjectileLaunchKnownFlags == 0x3F);
    REQUIRE(sizeof(MappingHeader) == 0x100);
    REQUIRE(offsetof(MappingHeader, AuthoritySuppressedDamageCount) == 0x90);
    REQUIRE(offsetof(MappingHeader, AuthorityRegistryInconsistencyCount) == 0xF8);
    REQUIRE(sizeof(EventRing) == 0x4C020);
    REQUIRE(sizeof(CommandRing) == 0x4C020);
    REQUIRE(sizeof(GameplayBridgeMapping) == 0x98140);
    REQUIRE(offsetof(GameplayBridgeMapping, Events) == 0x100);
    REQUIRE(offsetof(GameplayBridgeMapping, Commands) == 0x4C120);
    REQUIRE(HasCapability(kInitialCapabilities, Capability::Lifecycle));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalPlayerDiscovery));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalPlayerSnapshot));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteAvatarLifecycle));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteRootTransform));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteSpatialTransfer));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalAnimationGraphSnapshot));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::RemoteAnimationGraphSnapshot));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::QuestAndDialogue));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::QuestMutation));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::ExactAnimationActions));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalEventSinks));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::LocalCaptureSinks));
    REQUIRE(HasCapability(kInitialCapabilities, Capability::InventoryStackTransactions));
    REQUIRE(static_cast<CapabilityMask>(Capability::QuestMutation) == (1ull << 23));
    REQUIRE(static_cast<CapabilityMask>(Capability::LocalCaptureSinks) == (1ull << 25));
    REQUIRE(HasCapability(kMandatoryNativeParityCapabilities, Capability::LocalCaptureSinks));
    REQUIRE_FALSE(HasCapability(kMandatoryNativeParityCapabilities, Capability::LocalEventSinks));
    REQUIRE(CapabilityForDomain(GameplayDomain::Quest) == Capability::QuestMutation);
    REQUIRE(CapabilityForDomain(GameplayDomain::Dialogue) == Capability::QuestAndDialogue);
    REQUIRE(CapabilityForDomain(GameplayDomain::Party) == Capability::QuestAndDialogue);
    REQUIRE(IsActionInDomain(GameplayDomain::ActorState, GameplayAction::ArmLocalCapture));
    REQUIRE_FALSE(IsActionInDomain(GameplayDomain::Animation, GameplayAction::ArmLocalCapture));
    REQUIRE(IsActionInDomain(GameplayDomain::VrBodyPose, GameplayAction::VrPoseCommit));
    REQUIRE(static_cast<std::uint16_t>(GameplayAction::ConfigureDeathSystem) == 88);
    REQUIRE(IsActionInDomain(GameplayDomain::ActorState, GameplayAction::ConfigureDeathSystem));
    REQUIRE_FALSE(IsActionInDomain(GameplayDomain::WorldState, GameplayAction::ConfigureDeathSystem));
    REQUIRE(kPoseCommitNodeMask == 0xFFFFFu);
    REQUIRE(kVrikJointRotationCount == 30u);
    REQUIRE(kVrikJointCommitMask == 0x3FFFFFFFu);
    REQUIRE(kNpcSnapshotActionIdMarker == (1ull << 63));
    REQUIRE(kNpcSnapshotGraphChunkCount ==
            1 + (SkyrimTogetherVR::AnimationGraphProtocol::kMaximumFloatCount +
                    SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk - 1) /
                    SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk +
                (SkyrimTogetherVR::AnimationGraphProtocol::kMaximumIntegerCount +
                    SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk - 1) /
                    SkyrimTogetherVR::AnimationGraphProtocol::kValuesPerChunk);
    REQUIRE(kMaximumInventoryTransactionRecords == 1538);
    REQUIRE(kMaximumInventoryTransactionRecords <= kDefaultCommandRingCapacity);
    REQUIRE(kMaximumNpcSnapshotRecords == 928);
    REQUIRE(kMaximumNpcSnapshotRecords ==
            2 + kSkyrimActorValueCount + kNpcSnapshotGraphChunkCount + kMaximumNpcSnapshotItems * 2 +
                kMaximumInventoryTransactionEffects + kMaximumNpcSnapshotFactions +
                kMaximumNpcSnapshotAppearanceRecords);
    REQUIRE(kMaximumAppearanceHeadParts == 7);
    REQUIRE(kNpcSnapshotNameChunkBytes == 24);
    REQUIRE(IsActionInDomain(GameplayDomain::NpcOwnership, GameplayAction::NpcSnapshotAppearance));
    REQUIRE(IsActionInDomain(GameplayDomain::NpcOwnership, GameplayAction::NpcSnapshotNameChunk));
    REQUIRE(static_cast<std::uint16_t>(GameplayAction::NpcSnapshotItemExtra) == 94);
    REQUIRE(static_cast<std::uint16_t>(GameplayAction::NpcSnapshotItemEffect) == 95);
    REQUIRE(static_cast<std::uint16_t>(GameplayAction::InventoryTransactionEnd) == 100);
    REQUIRE(IsInventoryTransactionAction(GameplayAction::InventoryTransactionBegin));
    REQUIRE(IsActionInDomain(GameplayDomain::Inventory, GameplayAction::InventoryTransactionItemEffect));
    REQUIRE(kInventoryTransactionDynamicEnchantmentFormId == std::numeric_limits<std::uint32_t>::max());
    REQUIRE(kInventoryTransactionBeginKnownFlags == kInventoryTransactionReset);
}

TEST_CASE("HIGGS physical mutations have one callback producer", "[skyrim-vr][higgs][grab]")
{
    using SkyrimTogetherVR::HiggsBridge::MutationDeduplicator;
    using SkyrimTogetherVR::HiggsBridge::MutationKind;

    MutationDeduplicator deduplicator;
    constexpr std::uint32_t kFormId = 0x1234;
    constexpr std::uint64_t kNow = 1000;

    REQUIRE(deduplicator.Accept(MutationKind::Grabbed, true, kFormId, kNow));
    REQUIRE_FALSE(deduplicator.Accept(MutationKind::Grabbed, true, kFormId,
                                      kNow + MutationDeduplicator::kWindowMilliseconds));
    REQUIRE(deduplicator.Accept(MutationKind::Grabbed, false, kFormId, kNow));
    REQUIRE(deduplicator.Accept(MutationKind::Dropped, true, kFormId, kNow));
    REQUIRE(deduplicator.Accept(MutationKind::Grabbed, true, kFormId,
                                kNow + MutationDeduplicator::kWindowMilliseconds + 1));
    REQUIRE(deduplicator.Accept(MutationKind::Grabbed, true, 0, kNow));
    REQUIRE(deduplicator.Accept(MutationKind::Grabbed, true, 0, kNow));

    const auto localCapture = ReadRepositorySource("Code/vr_gameplay_bridge/LocalGameplayCapture.cpp");
    const auto higgsService = ReadRepositorySource("Code/client/Services/Generic/VRHiggsService.cpp");
    REQUIRE_FALSE(localCapture.empty());
    REQUIRE_FALSE(higgsService.empty());

    const auto genericGrab = localCapture.find("void OnGrabReleaseEvent(");
    const auto genericGrabEnd = localCapture.find("// TESMagicEffectApplyEvent", genericGrab);
    REQUIRE(genericGrab != std::string::npos);
    REQUIRE(genericGrabEnd != std::string::npos);
    REQUIRE(localCapture.substr(genericGrab, genericGrabEnd - genericGrab).find("Publish(") == std::string::npos);

    const auto mutationFilter = higgsService.find("bool IsMutationEvent(");
    const auto mutationFilterEnd = higgsService.find("bool IsNewerSequence(", mutationFilter);
    REQUIRE(mutationFilter != std::string::npos);
    REQUIRE(mutationFilterEnd != std::string::npos);
    const auto filter = higgsService.substr(mutationFilter, mutationFilterEnd - mutationFilter);
    REQUIRE(filter.find("kPulled") != std::string::npos);
    REQUIRE(filter.find("kGrabbed") != std::string::npos);
    REQUIRE(filter.find("kDropped") != std::string::npos);
    REQUIRE(filter.find("kStashed") == std::string::npos);
    REQUIRE(filter.find("kConsumed") == std::string::npos);
}

TEST_CASE("VR gameplay bridge classifies degraded appearance commits", "[skyrim-vr][gameplay-bridge]")
{
    const auto appearanceCommit = MakeGameplayCommand(
        CommandKind::ApplyGameplayAction, GameplayDomain::Appearance, GameplayAction::CommitAppearance);
    REQUIRE(IsSuccessfulCommandResult(CommandStatus::Success, appearanceCommit));
    REQUIRE(IsSuccessfulCommandResult(CommandStatus::Degraded, appearanceCommit));
    REQUIRE_FALSE(IsSuccessfulCommandResult(CommandStatus::Unsupported, appearanceCommit));

    const auto wrongKind = MakeGameplayCommand(
        CommandKind::ApplyGameplayTextChunk, GameplayDomain::Appearance, GameplayAction::CommitAppearance);
    REQUIRE_FALSE(IsSuccessfulCommandResult(CommandStatus::Degraded, wrongKind));

    const auto wrongDomain = MakeGameplayCommand(
        CommandKind::ApplyGameplayAction, GameplayDomain::Equipment, GameplayAction::CommitAppearance);
    REQUIRE_FALSE(IsSuccessfulCommandResult(CommandStatus::Degraded, wrongDomain));

    const auto wrongAction = MakeGameplayCommand(
        CommandKind::ApplyGameplayAction, GameplayDomain::Appearance, GameplayAction::SetTint);
    REQUIRE_FALSE(IsSuccessfulCommandResult(CommandStatus::Degraded, wrongAction));

    const auto questState = MakeGameplayCommand(
        CommandKind::ApplyGameplayAction, GameplayDomain::Quest, GameplayAction::SetQuestState);
    const auto questStage = MakeGameplayCommand(
        CommandKind::ApplyGameplayAction, GameplayDomain::Quest, GameplayAction::SetQuestStage);
    REQUIRE(IsSuccessfulCommandResult(CommandStatus::Degraded, questState));
    REQUIRE(IsSuccessfulCommandResult(CommandStatus::Degraded, questStage));
}

TEST_CASE("VR quest synchronization uses synchronous native mutation", "[skyrim-vr][gameplay-bridge][quest]")
{
    const auto manager = ReadRepositorySource("Code/vr_gameplay_bridge/QuestDialogueManager.cpp");
    const auto executor = ReadRepositorySource("Code/vr_gameplay_bridge/CommandExecutor.cpp");
    const auto questNativeAccess = ReadRepositorySource("Code/vr_gameplay_bridge/QuestNativeAccess.cpp");
    REQUIRE_FALSE(manager.empty());
    REQUIRE_FALSE(executor.empty());
    REQUIRE_FALSE(questNativeAccess.empty());
    REQUIRE(manager.find("QuestNativeAccess::SetStage(a_quest, a_stage)") != std::string::npos);
    REQUIRE(manager.find("a_quest.SetStage(a_stage)") == std::string::npos);
    REQUIRE(questNativeAccess.find("REL::Offset(kSetStageVrRva)") != std::string::npos);
    REQUIRE(questNativeAccess.find("bool ValidateTarget() noexcept") != std::string::npos);
    REQUIRE(questNativeAccess.find("!REL::Module::IsVR()") != std::string::npos);
    REQUIRE(questNativeAccess.find("kExpectedSkyrimVrRuntime") != std::string::npos);
    REQUIRE(questNativeAccess.find("kSetStageVrPrologue") != std::string::npos);
    REQUIRE(questNativeAccess.find("NoThrow::FailClosed<bool>") != std::string::npos);
    REQUIRE(questNativeAccess.find("return ValidateTarget() && setStage") != std::string::npos);
    REQUIRE(questNativeAccess.find("using SetStageFn = bool(__fastcall*)(RE::TESQuest*, std::uint16_t);") != std::string::npos);
    REQUIRE(manager.find("ArmQuestStageSuppression") != std::string::npos);
    REQUIRE(manager.find("ArmQuestStartStopSuppression") != std::string::npos);
    REQUIRE(manager.find("CommandStatus::Degraded") != std::string::npos);
    REQUIRE(manager.find("CommandStatus::QueueOverflow") != std::string::npos);
    REQUIRE(manager.find("CancelQuestSuppressions(stageResult)") != std::string::npos);
    REQUIRE(manager.find("ReconcilePartialQuestMutation") != std::string::npos);
    REQUIRE(manager.find("QuestNativeAccess::SetActive(*quest, false);") < manager.find("quest->Stop();"));
    REQUIRE(manager.find("SetQuestStage(*quest") < manager.find("QuestNativeAccess::SetActive(*quest, true);"));
    REQUIRE(executor.find("QuestSynchronizationStatus") == std::string::npos);

    const auto capture = ReadRepositorySource("Code/vr_gameplay_bridge/LocalGameplayCapture.cpp");
    REQUIRE_FALSE(capture.empty());
    REQUIRE(capture.find("Capability::LocalCaptureSinks") != std::string::npos);
    REQUIRE(capture.find("ScriptSinkRegistration") != std::string::npos);
    REQUIRE(capture.find("AnimationSinkRegistration") != std::string::npos);
    REQUIRE(capture.find("ProbeAnimationSink") != std::string::npos);
    REQUIRE(capture.find("g_scriptSinksRegistered") == std::string::npos);
    REQUIRE(capture.find("g_animationSinkRegistered") == std::string::npos);
    REQUIRE(capture.find("PendingQuestReconciliation") != std::string::npos);
    REQUIRE(capture.find("QueueQuestReconciliation") != std::string::npos);
    REQUIRE(capture.find("FlushPendingQuestReconciliations") != std::string::npos);
    REQUIRE(capture.find("pending = a_pending;") != std::string::npos);
    REQUIRE(capture.find("TryPushEvents(records.data(), recordCount)") != std::string::npos);
    REQUIRE(capture.find("PublishQuestReconciliation") != std::string::npos);
}

TEST_CASE("VR animation sink readiness requires current graph evidence", "[skyrim-vr][gameplay-bridge][capture]")
{
    const auto capture = ReadRepositorySource("Code/vr_gameplay_bridge/LocalGameplayCapture.cpp");
    REQUIRE_FALSE(capture.empty());
    REQUIRE(capture.find("RetainedUnverified") != std::string::npos);

    const auto registerBegin = capture.find("[[nodiscard]] bool RegisterAnimationSink(");
    const auto registerEnd = capture.find("[[nodiscard]] bool InitializeLocalCaptureSinksUnlocked()", registerBegin);
    REQUIRE(registerBegin != std::string::npos);
    REQUIRE(registerEnd != std::string::npos);
    const auto registration = capture.substr(registerBegin, registerEnd - registerBegin);

    const auto unavailableBegin = registration.find("presence == AnimationSinkPresence::Unavailable");
    const auto presentBegin = registration.find("presence == AnimationSinkPresence::Present", unavailableBegin);
    REQUIRE(unavailableBegin != std::string::npos);
    REQUIRE(presentBegin != std::string::npos);
    const auto unavailable = registration.substr(unavailableBegin, presentBegin - unavailableBegin);
    REQUIRE(unavailable.find("RetainAnimationSinkOwnershipUnverified();") != std::string::npos);
    REQUIRE(unavailable.find("return false;") != std::string::npos);
    REQUIRE(CountOccurrences(registration, "SinkRegistrationState::Registered") == 2);

    const auto resetBegin = capture.find("void Reset() noexcept", registerEnd);
    REQUIRE(resetBegin != std::string::npos);
    REQUIRE(capture.substr(resetBegin).find("RetainAnimationSinkOwnershipUnverified();") != std::string::npos);
}

TEST_CASE("VR root-transform results are distinct from avatar lifecycle", "[skyrim-vr][gameplay-bridge][movement]")
{
    const auto executor = ReadRepositorySource("Code/vr_gameplay_bridge/CommandExecutor.cpp");
    const auto diagnostics = ReadRepositorySource("Code/client/Services/Generic/VRGameplayDiagnosticsService.cpp");
    REQUIRE_FALSE(executor.empty());
    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE(executor.find("PublishAvatarCommandResult(ar_resultReservation, a_command, result)") != std::string::npos);
    REQUIRE(diagnostics.find("record.Header.Identity.SequenceId == 0") != std::string::npos);
    const auto resultHandler = diagnostics.find("void VRGameplayDiagnosticsService::OnGameplayResult");
    const auto refreshIdentity = diagnostics.find("void VRGameplayDiagnosticsService::RefreshSessionIdentity", resultHandler);
    REQUIRE(resultHandler != std::string::npos);
    REQUIRE(refreshIdentity != std::string::npos);
    REQUIRE(diagnostics.substr(resultHandler, refreshIdentity - resultHandler).find("RemoteSpatialTransferState") == std::string::npos);
}

TEST_CASE("VR combat and projectile ownership source audit", "[skyrim-vr][gameplay-bridge][source-audit]")
{
    const auto projectileHooks = ReadRepositorySource("Code/vr_gameplay_bridge/ProjectileHooks.cpp");
    const auto combatMagic = ReadRepositorySource("Code/vr_gameplay_bridge/CombatMagicManager.cpp");
    const auto magicHooks = ReadRepositorySource("Code/vr_gameplay_bridge/MagicHooks.cpp");
    const auto localGameplay = ReadRepositorySource("Code/client/Services/Generic/VRLocalGameplayService.cpp");
    const auto localCapture = ReadRepositorySource("Code/vr_gameplay_bridge/LocalGameplayCapture.cpp");
    REQUIRE_FALSE(projectileHooks.empty());
    REQUIRE_FALSE(combatMagic.empty());
    REQUIRE_FALSE(magicHooks.empty());
    REQUIRE_FALSE(localGameplay.empty());
    REQUIRE_FALSE(localCapture.empty());

    const auto launchHook = projectileHooks.find("RE::ProjectileHandle* HookLaunch(");
    const auto concentrationBypass = projectileHooks.find(
        "a_data.spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration", launchHook);
    const auto remoteShooter = projectileHooks.find("if (remoteShooter) {", launchHook);
    const auto unallowedRemoteLaunch = projectileHooks.find("if (g_remoteLaunchAllowance == 0)", remoteShooter);
    const auto suppressRemoteLaunch = projectileHooks.find("a_result->reset();", unallowedRemoteLaunch);
    const auto allowRemoteLaunch = projectileHooks.find(
        "return g_originalLaunch ? g_originalLaunch(a_result, a_data) : a_result;", suppressRemoteLaunch);
    const auto preparePayload = projectileHooks.find("const bool publish = PreparePayload", launchHook);
    REQUIRE(launchHook != std::string::npos);
    REQUIRE(concentrationBypass != std::string::npos);
    REQUIRE(remoteShooter != std::string::npos);
    REQUIRE(unallowedRemoteLaunch != std::string::npos);
    REQUIRE(suppressRemoteLaunch != std::string::npos);
    REQUIRE(allowRemoteLaunch != std::string::npos);
    REQUIRE(preparePayload != std::string::npos);
    REQUIRE(concentrationBypass < preparePayload);
    REQUIRE(remoteShooter < preparePayload);
    REQUIRE(unallowedRemoteLaunch < suppressRemoteLaunch);
    REQUIRE(suppressRemoteLaunch < allowRemoteLaunch);
    REQUIRE(allowRemoteLaunch < preparePayload);
    REQUIRE(CountOccurrences(projectileHooks, "endpoint.TryPushEvent(record);") == 1);

    const auto remoteMagicScope = combatMagic.find("MagicHooks::ScopedRemoteMagicApplication suppressEcho");
    const auto remoteCast = combatMagic.find("caster->CastSpellImmediate", remoteMagicScope);
    const auto remoteLaunchScope = combatMagic.find("ProjectileHooks::ScopedRemoteLaunch allowRemoteLaunch");
    const auto remoteLaunch = combatMagic.find("RE::Projectile::Launch(&handle, launch);", remoteLaunchScope);
    REQUIRE(remoteMagicScope != std::string::npos);
    REQUIRE(remoteCast != std::string::npos);
    REQUIRE(remoteMagicScope < remoteCast);
    REQUIRE(remoteLaunchScope != std::string::npos);
    REQUIRE(remoteLaunch != std::string::npos);
    REQUIRE(CountOccurrences(combatMagic, "RE::Projectile::Launch(&handle, launch);") == 1);
    REQUIRE(combatMagic.find("GetCastingType()") == std::string::npos);
    REQUIRE(magicHooks.find("g_remoteMagicApplicationDepth == 0 && a_doCast && a_caster && actor") != std::string::npos);

    const auto localTargetRejection = localGameplay.find("IsUnsupportedLocalGameplayAction(domain, action)");
    const auto targetConsumer = combatMagic.find(
        "return action == GameplayAction::MeleeHit || action == GameplayAction::SetCombatTarget ?");
    REQUIRE(localTargetRejection != std::string::npos);
    REQUIRE(targetConsumer != std::string::npos);
    REQUIRE(combatMagic.find("ExecuteCombatTarget") == std::string::npos);
    REQUIRE(combatMagic.find("ExecuteMeleeHit") == std::string::npos);
    REQUIRE(combatMagic.find("ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage") == std::string::npos);
    REQUIRE(localCapture.find("GameplayAction::MeleeHit") == std::string::npos);
}

TEST_CASE("VR pose snapshot diagnostics use a bounded log cadence", "[skyrim-vr][pose][source-audit]")
{
    const auto poseService = ReadRepositorySource("Code/client/Services/Generic/VRPoseService.cpp");
    REQUIRE_FALSE(poseService.empty());
    REQUIRE(poseService.find("constexpr double kPoseSnapshotLogInterval = 30.0;") != std::string::npos);
    REQUIRE(poseService.find("if (m_logTimer < kPoseSnapshotLogInterval)") != std::string::npos);
}

TEST_CASE("VR gameplay bridge admits only deferred appearance text shapes", "[skyrim-vr][gameplay-bridge]")
{
    REQUIRE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetName, 0, kGameplayTextAppearanceDeferred));
    REQUIRE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetTint, 1, kGameplayTextAppearanceDeferred));
    REQUIRE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetTint, kMaximumAppearanceTints,
        kGameplayTextAppearanceDeferred));

    REQUIRE_FALSE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetName, 1, kGameplayTextAppearanceDeferred));
    REQUIRE_FALSE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetTint, 0, kGameplayTextAppearanceDeferred));
    REQUIRE_FALSE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetTint, kMaximumAppearanceTints + 1,
        kGameplayTextAppearanceDeferred));
    REQUIRE_FALSE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetRace, 0, kGameplayTextAppearanceDeferred));
    REQUIRE_FALSE(IsDeferredAppearanceGameplayText(
        GameplayDomain::Appearance, GameplayAction::SetName, 0, 0));
}

TEST_CASE("VR gameplay bridge records preserve identity and payloads", "[skyrim-vr][gameplay-bridge]")
{
    const auto event = MakeEvent(55);
    const auto eventRoundTrip = event;

    REQUIRE(eventRoundTrip.Header.Kind == static_cast<std::uint16_t>(EventKind::LocalPlayerState));
    REQUIRE(eventRoundTrip.Header.PayloadSize == kFixedPayloadBytes);
    REQUIRE(eventRoundTrip.Header.Identity.ServerInstanceNonce == 0x1122334455667788ull);
    REQUIRE(eventRoundTrip.Header.Identity.ConnectionGeneration == 7);
    REQUIRE(eventRoundTrip.Header.Identity.LifecycleEpoch == 9);
    REQUIRE(eventRoundTrip.Header.Identity.EntityId == 42);
    REQUIRE(eventRoundTrip.Header.Identity.EntityGeneration == 3);
    REQUIRE(eventRoundTrip.Header.Identity.SequenceId == 55);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalPlayerHandle.Value == kLocalPlayerHandle.Value);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalCellFormId == 0x1234);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalWorldspaceFormId == 0x5678);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.Root.PositionX == 1.0f);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.Root.RotationW == 1.0f);
    REQUIRE(eventRoundTrip.Payload.LocalPlayerState.LocalActorBaseFormId == 0x7);

    CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(CommandKind::UpdateRemoteRootTransform);
    command.Header.PayloadSize = kFixedPayloadBytes;
    command.Header.Identity.ServerInstanceNonce = 0xAABBCCDDEEFF0011ull;
    command.Header.Identity.ConnectionGeneration = 8;
    command.Header.Identity.LifecycleEpoch = 10;
    command.Header.Identity.EntityId = 84;
    command.Header.Identity.EntityGeneration = 4;
    command.Header.Identity.SequenceId = 56;
    command.Header.Identity.ActionId = 57;
    command.Payload.UpdateRemoteRootTransform.AvatarHandle.Value = 0xDEADBEEF;
    command.Payload.UpdateRemoteRootTransform.Root.PositionZ = 33.0f;
    command.Payload.UpdateRemoteRootTransform.Root.RotationW = 1.0f;
    command.Payload.UpdateRemoteRootTransform.Root.Scale = 1.0f;
    command.Payload.UpdateRemoteRootTransform.UpdateFlags = 2;
    command.Payload.UpdateRemoteRootTransform.LocalCellFormId = 0x1234;
    command.Payload.UpdateRemoteRootTransform.LocalWorldspaceFormId = 0x5678;
    const auto commandRoundTrip = command;

    REQUIRE(commandRoundTrip.Header.Kind == static_cast<std::uint16_t>(CommandKind::UpdateRemoteRootTransform));
    REQUIRE(commandRoundTrip.Header.Identity.ActionId == 57);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.AvatarHandle.Value == 0xDEADBEEF);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.Root.PositionZ == 33.0f);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.UpdateFlags == 2);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.LocalCellFormId == 0x1234);
    REQUIRE(commandRoundTrip.Payload.UpdateRemoteRootTransform.LocalWorldspaceFormId == 0x5678);

    EventRecord avatarCreated{};
    avatarCreated.Header.Kind = static_cast<std::uint16_t>(EventKind::RemoteAvatarState);
    avatarCreated.Header.PayloadSize = kFixedPayloadBytes;
    avatarCreated.Payload.RemoteAvatarState.AvatarHandle.Value = 2;
    avatarCreated.Payload.RemoteAvatarState.State = static_cast<std::uint32_t>(RemoteAvatarState::Created);
    avatarCreated.Payload.RemoteAvatarState.Status = static_cast<std::uint32_t>(CommandStatus::Success);
    avatarCreated.Payload.RemoteAvatarState.LocalActorReferenceFormId = 0xFF001234;
    avatarCreated.Payload.RemoteAvatarState.Root.RotationW = 1.0F;
    avatarCreated.Payload.RemoteAvatarState.Root.Scale = 1.0F;
    const auto avatarCreatedRoundTrip = avatarCreated;
    REQUIRE(avatarCreatedRoundTrip.Payload.RemoteAvatarState.LocalActorReferenceFormId == 0xFF001234);

    CommandRecord projectileCommand{};
    projectileCommand.Header.Kind = static_cast<std::uint16_t>(CommandKind::ApplyProjectileLaunch);
    projectileCommand.Header.PayloadSize = kFixedPayloadBytes;
    projectileCommand.Header.Identity.EntityId = 84;
    projectileCommand.Header.Identity.EntityGeneration = 4;
    projectileCommand.Header.Identity.ActionId = 58;
    projectileCommand.Payload.ApplyProjectileLaunch.TargetHandle.Value = 0xDEADBEEF;
    projectileCommand.Payload.ApplyProjectileLaunch.LocalProjectileBaseFormId = 0x101;
    projectileCommand.Payload.ApplyProjectileLaunch.LocalWeaponFormId = 0x102;
    projectileCommand.Payload.ApplyProjectileLaunch.LocalAmmoFormId = 0x103;
    projectileCommand.Payload.ApplyProjectileLaunch.LocalParentCellFormId = 0x104;
    projectileCommand.Payload.ApplyProjectileLaunch.OriginX = 1.0F;
    projectileCommand.Payload.ApplyProjectileLaunch.OriginY = 2.0F;
    projectileCommand.Payload.ApplyProjectileLaunch.OriginZ = 3.0F;
    projectileCommand.Payload.ApplyProjectileLaunch.AngleX = 0.25F;
    projectileCommand.Payload.ApplyProjectileLaunch.AngleZ = 0.5F;
    projectileCommand.Payload.ApplyProjectileLaunch.Power = 4.0F;
    projectileCommand.Payload.ApplyProjectileLaunch.Scale = 1.0F;
    projectileCommand.Payload.ApplyProjectileLaunch.CastingSource = 1;
    projectileCommand.Payload.ApplyProjectileLaunch.Area = 5;
    projectileCommand.Payload.ApplyProjectileLaunch.LaunchFlags = ProjectileAlwaysHit | ProjectileNoDamageOutsideCombat |
                                                                  ProjectileAutoAim | ProjectileChainShatter |
                                                                  ProjectileDeferInitialization | ProjectileForceConeOfFire;
    const auto projectileRoundTrip = projectileCommand;

    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.LocalProjectileBaseFormId == 0x101);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.LocalWeaponFormId == 0x102);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.LocalAmmoFormId == 0x103);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.LocalSpellFormId == 0);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.LocalParentCellFormId == 0x104);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.AngleX == 0.25F);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.AngleZ == 0.5F);
    REQUIRE(projectileRoundTrip.Payload.ApplyProjectileLaunch.LaunchFlags == kProjectileLaunchKnownFlags);

    EventRecord npcProjectile{};
    npcProjectile.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalProjectileLaunch);
    npcProjectile.Header.PayloadSize = kFixedPayloadBytes;
    npcProjectile.Header.Identity.SequenceId = 59;
    npcProjectile.Payload.LocalProjectileLaunch.LocalShooterFormId = 0x205;
    npcProjectile.Payload.LocalProjectileLaunch.LocalProjectileBaseFormId = 0x101;
    npcProjectile.Payload.LocalProjectileLaunch.LocalParentCellFormId = 0x104;
    npcProjectile.Payload.LocalProjectileLaunch.Scale = 1.0F;
    const auto npcProjectileRoundTrip = npcProjectile;
    REQUIRE(npcProjectileRoundTrip.Payload.LocalProjectileLaunch.TargetHandle.Value == 0);
    REQUIRE(npcProjectileRoundTrip.Payload.LocalProjectileLaunch.LocalShooterFormId == 0x205);
}

TEST_CASE("VR animation graph chunks are bounded and preserve fixed-width values", "[skyrim-vr][gameplay-bridge]")
{
    namespace Animation = SkyrimTogetherVR::AnimationGraphProtocol;
    REQUIRE(sizeof(AnimationGraphChunkPayload) == kFixedPayloadBytes);
    REQUIRE(sizeof(RemoteAnimationGraphStatePayload) == kFixedPayloadBytes);
    REQUIRE(sizeof(RemoteSpatialTransferStatePayload) == kFixedPayloadBytes);
    REQUIRE(Animation::kMaximumBooleanCount == 64);
    REQUIRE(Animation::kMaximumFloatCount == 64);
    REQUIRE(Animation::kMaximumIntegerCount == 64);
    REQUIRE(Animation::kValuesPerChunk == 7);
    REQUIRE(Animation::kKnownDescriptorShapeCount == 25);
    const auto* humanoid = Animation::FindKnownShape(60, 13, 14);
    REQUIRE(humanoid != nullptr);
    REQUIRE(humanoid->DirectionFloatIndex == 1);
    REQUIRE(humanoid->DirectionFloatIndex < humanoid->FloatCount);
    REQUIRE_FALSE(Animation::IsKnownShape(60, 13, 13));
    REQUIRE(Animation::ExpectedChunkMask(Animation::ValueType::BooleanBits, 60) == 1);
    REQUIRE(Animation::ExpectedChunkMask(Animation::ValueType::Float, 13) == 3);
    REQUIRE(Animation::ExpectedChunkMask(Animation::ValueType::Integer, 14) == 3);
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::BooleanBits, 0, 60, 60));
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::Float, 0, 7, 13));
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::Float, 7, 6, 13));
    REQUIRE(Animation::IsValidChunk(Animation::ValueType::Integer, 7, 7, 14));
    REQUIRE_FALSE(Animation::IsValidChunk(Animation::ValueType::Float, 1, 7, 13));
    REQUIRE_FALSE(Animation::IsValidChunk(Animation::ValueType::Integer, 7, 6, 14));

    CommandRecord command{};
    command.Header.Kind = static_cast<std::uint16_t>(CommandKind::ApplyRemoteAnimationGraphChunk);
    command.Payload.ApplyRemoteAnimationGraphChunk.AvatarHandle.Value = 99;
    command.Payload.ApplyRemoteAnimationGraphChunk.SnapshotId = 12;
    command.Payload.ApplyRemoteAnimationGraphChunk.DescriptorVersion = Animation::kDescriptorVersion;
    command.Payload.ApplyRemoteAnimationGraphChunk.ValueType = static_cast<std::uint16_t>(Animation::ValueType::Float);
    command.Payload.ApplyRemoteAnimationGraphChunk.StartIndex = 7;
    command.Payload.ApplyRemoteAnimationGraphChunk.ValueCount = 6;
    command.Payload.ApplyRemoteAnimationGraphChunk.TotalCount = 13;
    command.Payload.ApplyRemoteAnimationGraphChunk.ChunkFlags = Animation::FullSnapshot;
    command.Payload.ApplyRemoteAnimationGraphChunk.Values[0] = 0x3F800000;
    const auto roundTrip = command;
    REQUIRE(roundTrip.Payload.ApplyRemoteAnimationGraphChunk.SnapshotId == 12);
    REQUIRE(roundTrip.Payload.ApplyRemoteAnimationGraphChunk.Values[0] == 0x3F800000);
}

TEST_CASE("VR animation graph protocol rejects unknown complete shapes", "[skyrim-vr][gameplay-bridge]")
{
    namespace Animation = SkyrimTogetherVR::AnimationGraphProtocol;
    Animation::SnapshotBuffer snapshot{};
    std::uint32_t booleanValues[Animation::kValuesPerChunk]{};
    std::uint32_t floatValues0[Animation::kValuesPerChunk]{};
    std::uint32_t floatValues1[Animation::kValuesPerChunk]{};
    std::uint32_t integerValues[Animation::kValuesPerChunk]{};

    REQUIRE(Animation::AcceptChunk(snapshot, 1, Animation::ValueType::BooleanBits, 0, 60, 60, 0.0F,
                                   booleanValues) == Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 1, Animation::ValueType::Float, 0, 7, 13, 0.0F,
                                   floatValues0) == Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 1, Animation::ValueType::Float, 7, 6, 13, 0.0F,
                                   floatValues1) == Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 1, Animation::ValueType::Integer, 0, 7, 13, 0.0F,
                                   integerValues) == Animation::ChunkAcceptResult::Malformed);
    REQUIRE_FALSE(snapshot.IsComplete());
}

TEST_CASE("movement tick ordering accepts an initial zero and rejects stale updates", "[skyrim-vr][movement]")
{
    using SkyrimTogether::Protocol::IsNewerMovementTick;
    REQUIRE(IsNewerMovementTick(false, 0, 0));
    REQUIRE(IsNewerMovementTick(false, 100, 1));
    REQUIRE(IsNewerMovementTick(true, 0, 1));
    REQUIRE(IsNewerMovementTick(true, 41, 42));
    REQUIRE_FALSE(IsNewerMovementTick(true, 42, 42));
    REQUIRE_FALSE(IsNewerMovementTick(true, 42, 41));
    REQUIRE_FALSE(IsNewerMovementTick(true, 42, std::numeric_limits<std::uint64_t>::max()));
    REQUIRE_FALSE(IsNewerMovementTick(true, 0, std::uint64_t{1} << 63));
    REQUIRE(IsNewerMovementTick(true, std::numeric_limits<std::uint64_t>::max() - 1, 0));
}

TEST_CASE("animation graph assembly commits only complete current snapshots", "[skyrim-vr][animation]")
{
    namespace Animation = SkyrimTogetherVR::AnimationGraphProtocol;
    Animation::SnapshotBuffer snapshot{};
    std::uint32_t booleanValues[Animation::kValuesPerChunk]{};
    booleanValues[0] = 1u << 3;
    std::uint32_t floatValues0[Animation::kValuesPerChunk]{};
    std::uint32_t floatValues1[Animation::kValuesPerChunk]{};
    std::uint32_t integerValues0[Animation::kValuesPerChunk]{};
    std::uint32_t integerValues1[Animation::kValuesPerChunk]{};
    for (std::size_t index = 0; index < Animation::kValuesPerChunk; ++index)
    {
        floatValues0[index] = std::bit_cast<std::uint32_t>(static_cast<float>(index));
        integerValues0[index] = static_cast<std::uint32_t>(index + 10);
        integerValues1[index] = static_cast<std::uint32_t>(index + 20);
    }
    for (std::size_t index = 0; index < 13 - Animation::kValuesPerChunk; ++index)
        floatValues1[index] = std::bit_cast<std::uint32_t>(static_cast<float>(index + 7));

    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Float, 7, 6, 13, 1.0f, floatValues1) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::BooleanBits, 0, 60, 60, 1.0f, booleanValues) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Integer, 7, 7, 14, 1.0f, integerValues1) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Float, 0, 7, 13, 1.0f, floatValues0) ==
            Animation::ChunkAcceptResult::Accepted);
    REQUIRE(Animation::AcceptChunk(snapshot, 4, Animation::ValueType::Integer, 0, 7, 14, 1.0f, integerValues0) ==
            Animation::ChunkAcceptResult::Complete);
    REQUIRE(snapshot.IsComplete());
    REQUIRE(snapshot.Booleans[3]);
    REQUIRE(snapshot.Floats[12] == 12.0f);
    REQUIRE(snapshot.Integers[13] == 26);

    REQUIRE(Animation::AcceptChunk(snapshot, 3, Animation::ValueType::Float, 0, 7, 13, 1.0f, floatValues0) ==
            Animation::ChunkAcceptResult::Stale);
    floatValues0[0] = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(Animation::AcceptChunk(snapshot, 5, Animation::ValueType::Float, 0, 7, 13, 1.0f, floatValues0) ==
            Animation::ChunkAcceptResult::Malformed);
    REQUIRE(snapshot.IsComplete());
    REQUIRE(snapshot.SnapshotId == 4);
}

TEST_CASE("VR gameplay bridge ring rejects full pushes and wraps", "[skyrim-vr][gameplay-bridge]")
{
    BoundedMpmcRing<EventRecord, 4> ring{};
    InitializeRing(ring);

    EventRecord output{};
    REQUIRE_FALSE(TryPop(ring, output));
    REQUIRE(ring.EmptyPopCount.load() == 1);

    for (std::uint64_t sequence = 0; sequence < 4; ++sequence)
        REQUIRE(TryPush(ring, MakeEvent(sequence)));

    REQUIRE_FALSE(TryPush(ring, MakeEvent(4)));
    REQUIRE(ring.DroppedPushCount.load() == 1);

    for (std::uint64_t sequence = 0; sequence < 4; ++sequence)
    {
        REQUIRE(TryPop(ring, output));
        REQUIRE(output.Header.Identity.SequenceId == sequence);
    }

    REQUIRE(TryPush(ring, MakeEvent(4)));
    REQUIRE(TryPop(ring, output));
    REQUIRE(output.Header.Identity.SequenceId == 4);
}

TEST_CASE("VR gameplay bridge ring batch publication is all-or-nothing", "[skyrim-vr][gameplay-bridge]")
{
    BoundedMpmcRing<EventRecord, 4> ring{};
    InitializeRing(ring);

    const EventRecord firstBatch[] = {MakeEvent(0), MakeEvent(1), MakeEvent(2)};
    REQUIRE(TryPushBatch(ring, firstBatch, std::size(firstBatch)));

    const EventRecord rejectedBatch[] = {MakeEvent(3), MakeEvent(4)};
    REQUIRE_FALSE(TryPushBatch(ring, rejectedBatch, std::size(rejectedBatch)));
    REQUIRE(ring.DroppedPushCount.load() == 1);

    EventRecord output{};
    for (std::uint64_t sequence = 0; sequence < 3; ++sequence)
    {
        REQUIRE(TryPop(ring, output));
        REQUIRE(output.Header.Identity.SequenceId == sequence);
    }
    REQUIRE_FALSE(TryPop(ring, output));

    REQUIRE(TryPushBatch(ring, rejectedBatch, std::size(rejectedBatch)));
    for (std::uint64_t sequence = 3; sequence < 5; ++sequence)
    {
        REQUIRE(TryPop(ring, output));
        REQUIRE(output.Header.Identity.SequenceId == sequence);
    }
}

TEST_CASE("VR gameplay bridge deferred backlog preserves non-coalescible overflow", "[skyrim-vr][gameplay-bridge]")
{
    constexpr std::size_t kRingCapacity = 4;
    constexpr std::uint64_t kRecordCount = 12;
    static_assert(kRecordCount > kRingCapacity);
    BoundedMpmcRing<EventRecord, kRingCapacity> ring{};
    BoundedRecordBacklog<EventRecord, 16> backlog{};
    InitializeRing(ring);

    for (std::uint64_t sequence{}; sequence < kRecordCount; ++sequence) {
        auto event = MakeEvent(sequence);
        event.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalActorActionMetadata);
        event.Header.Identity.ActionId = sequence + 1;
        REQUIRE(backlog.TryAppend(event));
    }

    std::array<std::uint32_t, kRecordCount> seen{};
    EventRecord output{};
    while (!backlog.Empty()) {
        static_cast<void>(backlog.FlushTo(ring));
        while (TryPop(ring, output)) {
            REQUIRE(output.Header.Identity.SequenceId < kRecordCount);
            ++seen[output.Header.Identity.SequenceId];
        }
    }
    while (TryPop(ring, output)) {
        REQUIRE(output.Header.Identity.SequenceId < kRecordCount);
        ++seen[output.Header.Identity.SequenceId];
    }

    REQUIRE(ring.DroppedPushCount.load() == 0);
    for (const auto count : seen)
        REQUIRE(count == 1);
}

TEST_CASE("VR command results retain exact FIFO under mapped-ring pressure", "[skyrim-vr][gameplay-bridge]")
{
    BoundedMpmcRing<EventRecord, 2> ring{};
    BoundedReservedRecordQueue<EventRecord, 4> results{};
    InitializeRing(ring);

    REQUIRE(TryPush(ring, MakeEvent(100)));
    REQUIRE(TryPush(ring, MakeEvent(101)));
    REQUIRE(results.TryReserve(2));
    REQUIRE(results.TryCommitReserved(MakeEvent(10)));
    REQUIRE(results.TryCommitReserved(MakeEvent(11)));
    REQUIRE(results.Reserved() == 0);
    REQUIRE(results.Size() == 2);

    REQUIRE_FALSE(results.FlushTo(ring));
    REQUIRE(results.Size() == 2);
    REQUIRE(ring.DroppedPushCount.load() == 0);

    EventRecord event{};
    REQUIRE(TryPop(ring, event));
    REQUIRE(event.Header.Identity.SequenceId == 100);
    REQUIRE(TryPop(ring, event));
    REQUIRE(event.Header.Identity.SequenceId == 101);
    REQUIRE(results.FlushTo(ring));
    REQUIRE(results.Empty());
    REQUIRE(TryPop(ring, event));
    REQUIRE(event.Header.Identity.SequenceId == 10);
    REQUIRE(TryPop(ring, event));
    REQUIRE(event.Header.Identity.SequenceId == 11);
    REQUIRE(ring.DroppedPushCount.load() == 0);
}

TEST_CASE("VR command result reservation is atomic before mutation", "[skyrim-vr][gameplay-bridge]")
{
    BoundedReservedRecordQueue<EventRecord, 3> results{};
    REQUIRE(results.TryReserve(2));
    REQUIRE(results.TryCommitReserved(MakeEvent(1)));
    REQUIRE(results.TryCommitReserved(MakeEvent(2)));
    REQUIRE(results.FreeCapacity() == 1);

    bool mutated{};
    if (results.TryReserve(2))
        mutated = true;
    REQUIRE_FALSE(mutated);
    REQUIRE(results.Size() == 2);
    REQUIRE(results.Reserved() == 0);
}

TEST_CASE("VR event pressure diagnostics distinguish local backlog overflow", "[skyrim-vr][gameplay-bridge]")
{
    BoundedMpmcRing<EventRecord, 2> ring{};
    BoundedRecordBacklog<EventRecord, 2> backlog{};
    InitializeRing(ring);
    REQUIRE(backlog.TryAppend(MakeEvent(1)));
    REQUIRE(backlog.TryAppend(MakeEvent(2)));
    REQUIRE_FALSE(backlog.TryAppend(MakeEvent(3)));
    REQUIRE(ring.DroppedPushCount.load() == 0);

    const auto endpoint = ReadRepositorySource("Code/vr_gameplay_bridge/BridgeEndpoint.cpp");
    const auto overflow = endpoint.find("_pendingEventBacklogOverflowCount.fetch_add");
    const auto diagnostic = endpoint.find("std::uint64_t BridgeEndpoint::PendingEventBacklogOverflowCount()", overflow);
    REQUIRE(overflow != std::string::npos);
    REQUIRE(diagnostic != std::string::npos);
    REQUIRE(endpoint.find("return _pendingEventBacklogOverflowCount.load(std::memory_order_relaxed);", diagnostic) != std::string::npos);
    REQUIRE(endpoint.find("DroppedPushCount.fetch_add") == std::string::npos);
}

TEST_CASE("VR command pump gates mutation on reservations and endpoint health", "[skyrim-vr][gameplay-bridge]")
{
    REQUIRE(IsOperationalEndpointState(EndpointState::Ready));
    REQUIRE_FALSE(IsOperationalEndpointState(EndpointState::Faulted));

    std::array states{EndpointState::Ready, EndpointState::Faulted, EndpointState::Ready};
    std::size_t mutations{};
    for (const auto state : states) {
        if (!IsOperationalEndpointState(state))
            break;
        ++mutations;
    }
    REQUIRE(mutations == 1);

    const auto executor = ReadRepositorySource("Code/vr_gameplay_bridge/CommandExecutor.cpp");
    const auto pump = executor.find("CommandPumpResult ProcessCommands(");
    const auto reserve = executor.find("TryReserveCommandResultEvents(2, resultReservation)", pump);
    const auto prePopHealth = executor.find("if (!endpoint.IsOperational())", reserve);
    const auto pop = executor.find("TryPop(commands, command)", reserve);
    const auto postPopHealth = executor.find("if (!endpoint.IsOperational())", pop);
    const auto execute = executor.find("ExecuteCommand(endpoint, resultReservation, command)", postPopHealth);
    const auto postExecuteHealth = executor.find("if (!endpoint.IsOperational())", execute);
    REQUIRE(pump != std::string::npos);
    REQUIRE(reserve != std::string::npos);
    REQUIRE(prePopHealth != std::string::npos);
    REQUIRE(pop != std::string::npos);
    REQUIRE(postPopHealth != std::string::npos);
    REQUIRE(execute != std::string::npos);
    REQUIRE(postExecuteHealth != std::string::npos);
    REQUIRE(reserve < prePopHealth);
    REQUIRE(prePopHealth < pop);
    REQUIRE(pop < postPopHealth);
    REQUIRE(postPopHealth < execute);
    REQUIRE(execute < postExecuteHealth);
    REQUIRE(executor.find("DiscardCommandResultEvents") == std::string::npos);
}

TEST_CASE("VR gameplay bridge loss attribution separates pre-ready and retired work", "[skyrim-vr][gameplay-bridge]")
{
    const SessionIdentitySnapshot session{0x1122334455667788ull, 7};
    BridgeIdentity current{};
    current.ServerInstanceNonce = session.ServerInstanceNonce;
    current.ConnectionGeneration = session.ConnectionGeneration;
    current.LifecycleEpoch = 9;

    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::Current);
    REQUIRE(ClassifyWorkAttribution(EndpointState::Prepared, session, 9, current) == WorkAttribution::PreReady);
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, {}, 9, current) == WorkAttribution::PreReady);
    current = {};
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::Current);
    current.ServerInstanceNonce = session.ServerInstanceNonce;
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::Current);
    current.ConnectionGeneration = session.ConnectionGeneration;
    current.LifecycleEpoch = 10;
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::Current);
    current.ServerInstanceNonce ^= 1;
    current.LifecycleEpoch = 9;
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::Current);
    current.ServerInstanceNonce = session.ServerInstanceNonce;
    current.LifecycleEpoch = 8;
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::LifecycleRetired);
    current.ConnectionGeneration = session.ConnectionGeneration - 1;
    current.LifecycleEpoch = 9;
    REQUIRE(ClassifyWorkAttribution(EndpointState::Ready, session, 9, current) == WorkAttribution::LifecycleRetired);
    REQUIRE(ClassifyWorkAttribution(EndpointState::Retired, session, 9, current) == WorkAttribution::LifecycleRetired);

    const auto client = ReadRepositorySource("Code/client/VRGameplayBridge.cpp");
    const auto discardRecorder = client.find("void RecordDiscardedEvent(");
    const auto currentFailure = client.find("RecordDiscardedEvent(WorkAttribution::Current);", discardRecorder);
    const auto otherIncrement = client.find("s_discardedEventOtherCount.fetch_add", discardRecorder);
    REQUIRE(discardRecorder != std::string::npos);
    REQUIRE(currentFailure != std::string::npos);
    REQUIRE(otherIncrement != std::string::npos);
    REQUIRE(otherIncrement < currentFailure);

    REQUIRE(ReconciledAttributionTotal(3, 4, 5) == 12);
    REQUIRE(AreAttributionCountersReconciled(12, 3, 4, 5));
    REQUIRE_FALSE(AreAttributionCountersReconciled(12, 3, 4, 4));
}

TEST_CASE("VR avatar status emits reconciled bridge loss attribution", "[skyrim-vr][gameplay-bridge]")
{
    const auto source = ReadRepositorySource("Code/client/Services/Generic/VRAvatarService.cpp");
    REQUIRE_FALSE(source.empty());
    REQUIRE(source.find("bridge.discardedEvents=") != std::string::npos);
    REQUIRE(source.find("bridge.discardedEvents.preReady=") != std::string::npos);
    REQUIRE(source.find("bridge.discardedEvents.lifecycleRetired=") != std::string::npos);
    REQUIRE(source.find("bridge.discardedEvents.other=") != std::string::npos);
    REQUIRE(source.find("bridge.rejectedSubmissions=") != std::string::npos);
    REQUIRE(source.find("bridge.rejectedSubmissions.preReady=") != std::string::npos);
    REQUIRE(source.find("bridge.rejectedSubmissions.lifecycleRetired=") != std::string::npos);
    REQUIRE(source.find("bridge.rejectedSubmissions.other=") != std::string::npos);
    REQUIRE(source.find("bridge.eventRingDroppedPushes=") != std::string::npos);
}

TEST_CASE("VR respawn uses the authority hook's verified MoveTo trampoline", "[skyrim-vr][gameplay-bridge][respawn]")
{
    const auto deathSource = ReadRepositorySource("Code/vr_gameplay_bridge/VerifiedVrDeath.cpp");
    const auto authoritySource = ReadRepositorySource("Code/vr_gameplay_bridge/ActorAuthorityHooks.cpp");
    REQUIRE_FALSE(deathSource.empty());
    REQUIRE_FALSE(authoritySource.empty());
    REQUIRE(deathSource.find("ActorAuthorityHooks::GetVerifiedMoveToImplTrampoline()") != std::string::npos);
    REQUIRE(deathSource.find("kMoveToVrRva") == std::string::npos);
    REQUIRE(deathSource.find("kMoveToVrPrologue") == std::string::npos);
    REQUIRE(authoritySource.find("return reinterpret_cast<void*>(g_originalMoveToImpl);") != std::string::npos);
}

TEST_CASE("VR gameplay bridge ring retains each MPMC record once", "[skyrim-vr][gameplay-bridge]")
{
    constexpr std::uint64_t kProducerCount = 2;
    constexpr std::uint64_t kConsumerCount = 2;
    constexpr std::uint64_t kRecordsPerProducer = 64;
    constexpr std::uint64_t kTotalRecords = kProducerCount * kRecordsPerProducer;

    BoundedMpmcRing<EventRecord, 8> ring{};
    InitializeRing(ring);

    std::atomic<std::uint32_t> seen[kTotalRecords]{};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> invalid{0};

    std::thread producers[kProducerCount];
    for (std::uint64_t producer = 0; producer < kProducerCount; ++producer)
    {
        producers[producer] = std::thread([&, producer] {
            const auto first = producer * kRecordsPerProducer;
            for (std::uint64_t index = 0; index < kRecordsPerProducer; ++index)
            {
                const auto sequence = first + index;
                while (!TryPush(ring, MakeEvent(sequence)))
                    std::this_thread::yield();
            }
        });
    }

    std::thread consumers[kConsumerCount];
    for (auto& consumer : consumers)
    {
        consumer = std::thread([&] {
            EventRecord output{};
            while (consumed.load(std::memory_order_acquire) < kTotalRecords)
            {
                if (!TryPop(ring, output))
                {
                    std::this_thread::yield();
                    continue;
                }

                const auto sequence = output.Header.Identity.SequenceId;
                if (sequence >= kTotalRecords)
                    invalid.fetch_add(1, std::memory_order_relaxed);
                else
                    seen[sequence].fetch_add(1, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (auto& producer : producers)
        producer.join();
    for (auto& consumer : consumers)
        consumer.join();

    REQUIRE(consumed.load() == kTotalRecords);
    REQUIRE(invalid.load() == 0);
    for (const auto& count : seen)
        REQUIRE(count.load() == 1);
}
