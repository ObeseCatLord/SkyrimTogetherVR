#pragma once

#include "BridgeEndpoint.h"

#include <array>
#include <functional>
#include <unordered_map>

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
    [[nodiscard]] AvatarCommandResult CreateRemoteAvatar(const CommandRecord& a_command) noexcept;
    [[nodiscard]] AvatarCommandResult UpdateRemoteRootTransform(const CommandRecord& a_command) noexcept;
    [[nodiscard]] AvatarCommandResult ApplyRemoteAnimationGraphChunk(const CommandRecord& a_command) noexcept;
    [[nodiscard]] CommandStatus ResolveGameplayActor(
        const CommandRecord& a_command,
        RE::NiPointer<RE::Actor>& ar_actor) noexcept;
    [[nodiscard]] CommandStatus ResolveActorByHandle(
        const BridgeIdentity& a_identity,
        AdapterHandle a_handle,
        RE::NiPointer<RE::Actor>& ar_actor) noexcept;
    [[nodiscard]] bool IsManagedRemoteActor(const RE::Actor* a_actor) const noexcept;
    [[nodiscard]] bool IsPlayerAvatar(const BridgeIdentity& a_identity, AdapterHandle a_handle) const noexcept;
    [[nodiscard]] CommandStatus ApplyAnimationSnapshotToActor(
        RE::Actor& a_actor,
        const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept;
    void ProcessPendingAnimationSnapshots() noexcept;
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
        std::uint32_t LocalCellFormId{};
        std::uint32_t LocalWorldspaceFormId{};
        std::uint32_t LocalActorReferenceFormId{};
        RootTransform Root{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
        PendingAnimationSnapshot PendingAnimation{};
        bool AnimationGraphValidated{};
        bool OwnsActor{};
        bool IsPlayer{};
    };

    struct EntityLedger
    {
        std::uint32_t EntityGeneration{};
        std::uint64_t LastRootSequence{};
        std::uint64_t LastAnimationSequence{};
        std::uint64_t LastAction{};
        bool Destroyed{};
    };

    [[nodiscard]] static AvatarKey MakeAvatarKey(const BridgeIdentity& a_identity) noexcept;
    [[nodiscard]] static EntityKey MakeEntityKey(const BridgeIdentity& a_identity) noexcept;
    [[nodiscard]] bool IsCommandPumpOwner() const noexcept;
    [[nodiscard]] static bool NormalizeRoot(const RootTransform& a_root, RootTransform& a_normalized, RE::NiPoint3& a_angles) noexcept;
    [[nodiscard]] static AvatarCommandResult ResultFor(const AvatarRecord& a_record, CommandStatus a_status) noexcept;
    [[nodiscard]] static bool MoveActorToLocation(RE::Actor& a_actor, RE::TESObjectCELL& a_cell,
                                                  RE::TESWorldSpace* a_worldspace, const RE::NiPoint3& a_position,
                                                  const RE::NiPoint3& a_angles) noexcept;
    [[nodiscard]] static bool ValidateAnimationGraph(RE::Actor& a_actor) noexcept;
    [[nodiscard]] static bool ApplyAnimationSnapshot(RE::Actor& a_actor, const AvatarRecord::PendingAnimationSnapshot& a_snapshot) noexcept;
    [[nodiscard]] static PendingAnimationResult TryApplyPendingAnimation(AvatarRecord& a_record) noexcept;
    void DestroyRecord(AvatarRecord& a_record) noexcept;

    std::unordered_map<AvatarKey, AvatarRecord, AvatarKeyHash> _avatars;
    std::unordered_map<EntityKey, EntityLedger, EntityKeyHash> _entityLedger;
    std::uint64_t _nextToken{GameplayBridge::kFirstRemoteAvatarHandle};
    std::uint32_t _commandPumpOwnerThreadId{};
    bool _tokenExhausted{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter
