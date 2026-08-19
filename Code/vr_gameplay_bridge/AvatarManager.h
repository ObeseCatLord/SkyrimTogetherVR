#pragma once

#include "BridgeEndpoint.h"

#include <array>
#include <functional>
#include <unordered_map>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter
{
struct AvatarCommandResult
{
    CommandStatus Status{CommandStatus::EngineRejected};
    AdapterHandle AvatarHandle{};
    std::uint32_t LocalCellFormId{};
    std::uint32_t LocalWorldspaceFormId{};
    std::uint32_t LocalActorReferenceFormId{};
    RootTransform Root{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    std::uint64_t AnimationSnapshotId{};
    bool AnimationApplied{};
    bool SpatialTransferApplied{};
    std::uint32_t SourceCellFormId{};
    std::uint32_t SourceWorldspaceFormId{};
};

class AvatarManager
{
public:
    static AvatarManager& Get() noexcept;

    void BindCommandPumpOwner(std::uint32_t a_threadId) noexcept;
    // Narrow public query for command handlers that must avoid touching game
    // state from a non-game thread. The owning-thread implementation remains
    // private so binding remains AvatarManager-owned.
    [[nodiscard]] bool IsOnCommandPumpThread() const noexcept { return IsCommandPumpOwner(); }
    [[nodiscard]] AvatarCommandResult CreateRemoteAvatar(const CommandRecord& a_command) noexcept;
    [[nodiscard]] AvatarCommandResult UpdateRemoteRootTransform(const CommandRecord& a_command) noexcept;
    [[nodiscard]] AvatarCommandResult ApplyRemoteAnimationGraphChunk(const CommandRecord& a_command) noexcept;
    [[nodiscard]] CommandStatus ResolveGameplayActor(const CommandRecord& a_command, RE::NiPointer<RE::Actor>& ar_actor) noexcept;
    [[nodiscard]] CommandStatus ValidateLocalNativeGameplayActor(const CommandRecord& a_command) noexcept;
    void ReleaseLocalNativeGameplayActor(const CommandRecord& a_command) noexcept;
    [[nodiscard]] CommandStatus ResolveActorByHandle(const BridgeIdentity& a_identity, AdapterHandle a_handle, RE::NiPointer<RE::Actor>& ar_actor) noexcept;
    [[nodiscard]] bool IsManagedRemoteActor(const RE::Actor* a_actor) const noexcept;
    [[nodiscard]] bool IsManagedRemotePlayerActor(const RE::Actor* a_actor) const noexcept;
    [[nodiscard]] bool IsPlayerAvatar(const BridgeIdentity& a_identity, AdapterHandle a_handle) const noexcept;
    [[nodiscard]] CommandStatus ApplyAnimationSnapshotToActor(RE::Actor& a_actor, const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept;
    void ProcessPendingAnimationSnapshots() noexcept;
    void ProcessAuthoritativeRemoteActors() noexcept;
    void RecordAuthoritativeActorState(const CommandRecord& a_command, const RE::Actor& a_actor) noexcept;
    [[nodiscard]] AvatarCommandResult DestroyRemoteAvatar(const CommandRecord& a_command) noexcept;
    void RetireAllOnCommandPumpOwner() noexcept;

private:
    enum class PendingAnimationResult
    {
        WaitingForGraph,
        Applied,
        Rejected,
    };
    struct AvatarKey
    {
        std::uint64_t ServerInstanceNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LifecycleEpoch{};
        std::uint64_t EntityId{};
        std::uint32_t EntityGeneration{};

        [[nodiscard]] bool operator==(const AvatarKey& a_rhs) const noexcept = default;
    };

    struct EntityKey
    {
        std::uint64_t ServerInstanceNonce{};
        std::uint64_t ConnectionGeneration{};
        std::uint64_t LifecycleEpoch{};
        std::uint64_t EntityId{};

        [[nodiscard]] bool operator==(const EntityKey& a_rhs) const noexcept = default;
    };

    struct AvatarKeyHash
    {
        [[nodiscard]] std::size_t operator()(const AvatarKey& a_key) const noexcept;
    };

    struct EntityKeyHash
    {
        [[nodiscard]] std::size_t operator()(const EntityKey& a_key) const noexcept;
    };

    struct AvatarRecord
    {
        using PendingAnimationSnapshot = AnimationGraphProtocol::SnapshotBuffer;

        AdapterHandle Token{};
        RE::ActorHandle Actor;
        RE::TESNPC* VisualBase{};
        std::uint64_t LastRootSequence{};
        std::uint64_t LastAnimationSequence{};
        std::uint64_t LastAnimationSnapshot{};
        std::uint64_t LastAction{};
        std::uint32_t LocalActorBaseFormId{};
        std::uint32_t CreateFlags{};
        std::uint32_t LocalCellFormId{};
        std::uint32_t LocalWorldspaceFormId{};
        std::uint32_t LocalActorReferenceFormId{};
        RootTransform Root{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
        RemoteActorAuthorityState Authority{};
        PendingAnimationSnapshot PendingAnimation{};
        bool OwnsActor{};
        bool IsPlayer{};
        bool IsPlayerSummon{};
        bool AuthorityCrossCellDeferred{};
        bool AuthorityFailureReported{};
        RE::ActorHandle OriginalCommandingActor;
        bool ExistingAiStateCaptured{};
        bool ExistingAiWasEnabled{};
        bool RestoreAiOnDestroy{};
        bool OriginalCommandingActorCaptured{};
        bool RestoreCommandingActorOnDestroy{};
        bool RestorationFailureReported{};
        bool CommandingActorPending{};
        bool CommandingActorBound{};
    };

    struct EntityLedger
    {
        std::uint32_t EntityGeneration{};
        std::uint64_t LastRootSequence{};
        std::uint64_t LastAnimationSequence{};
        std::uint64_t LastAction{};
        bool Destroyed{};
    };

    struct LocalNativeActorBinding
    {
        RE::ActorHandle Actor;
        std::uint32_t LocalReferenceFormId{};
    };

    struct RetainedAuthorityFailure
    {
        RE::ActorHandle Actor;
        RE::TESNPC* VisualBase{};
        bool OwnsActor{};
    };

    [[nodiscard]] static AvatarKey MakeAvatarKey(const BridgeIdentity& a_identity) noexcept;
    [[nodiscard]] static EntityKey MakeEntityKey(const BridgeIdentity& a_identity) noexcept;
    [[nodiscard]] bool IsCommandPumpOwner() const noexcept;
    [[nodiscard]] static bool NormalizeRoot(const RootTransform& a_root, RootTransform& a_normalized, RE::NiPoint3& a_angles) noexcept;
    [[nodiscard]] static AvatarCommandResult ResultFor(const AvatarRecord& a_record, CommandStatus a_status) noexcept;
    [[nodiscard]] static bool
    MoveActorToLocation(RE::Actor& a_actor, RE::TESObjectCELL& a_cell, RE::TESWorldSpace* a_worldspace, const RE::NiPoint3& a_position, const RE::NiPoint3& a_angles) noexcept;
    [[nodiscard]] static bool ApplyAnimationSnapshot(RE::Actor& a_actor, const AvatarRecord::PendingAnimationSnapshot& a_snapshot) noexcept;
    [[nodiscard]] static PendingAnimationResult TryApplyPendingAnimation(AvatarRecord& a_record) noexcept;
    void ReconcileAuthoritativeRemoteActor(const AvatarKey& a_key, AvatarRecord& ar_record) noexcept;
    void RecordAuthorityFailure(AvatarRecord& ar_record, const char* ap_reason) noexcept;
    void RecordRestorationFailure(AvatarRecord& ar_record, const char* ap_reason) noexcept;
    [[nodiscard]] bool RestoreExistingRecordMutations(AvatarRecord& ar_record) noexcept;
    void RetireLocalNativeGameplayActor(std::unordered_map<AvatarKey, LocalNativeActorBinding, AvatarKeyHash>::iterator a_binding) noexcept;
    void RetainFailedAuthorityActor(RE::Actor* a_actor, RE::TESNPC* a_visualBase, bool a_ownsActor) noexcept;
    [[nodiscard]] bool DestroyRegisteredActorWithoutRecord(
        RE::Actor* a_actor,
        RE::TESNPC* a_visualBase,
        bool a_ownsActor,
        bool a_restoreExistingAi,
        bool a_existingAiWasEnabled) noexcept;
    void RecordRemoteAvatarAiAdmissionFailure(const char* ap_reason) noexcept;
    // Returns false only after the authority registry has faulted and the
    // record remains intact. Callers must not erase or mutate that record.
    [[nodiscard]] bool DestroyRecord(AvatarRecord& a_record) noexcept;

    std::unordered_map<AvatarKey, AvatarRecord, AvatarKeyHash> _avatars;
    std::unordered_map<EntityKey, EntityLedger, EntityKeyHash> _entityLedger;
    std::unordered_map<AvatarKey, LocalNativeActorBinding, AvatarKeyHash> _localNativeActors;
    std::vector<RetainedAuthorityFailure> _retainedAuthorityFailures;
    std::uint64_t _nextToken{GameplayBridge::kFirstRemoteAvatarHandle};
    std::uint64_t _remoteAvatarAiAdmissionFailureCount{};
    std::uint32_t _commandPumpOwnerThreadId{};
    bool _tokenExhausted{};
    bool _remoteAvatarAiAdmissionEndpointFaulted{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter
