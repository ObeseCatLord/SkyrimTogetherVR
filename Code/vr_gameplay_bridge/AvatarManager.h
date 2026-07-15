#pragma once

#include "BridgeEndpoint.h"

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
    RootTransform Root{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
};

class AvatarManager
{
public:
    static AvatarManager& Get() noexcept;

    void BindCommandPumpOwner(std::uint32_t a_threadId) noexcept;
    [[nodiscard]] AvatarCommandResult CreateRemoteAvatar(const CommandRecord& a_command) noexcept;
    [[nodiscard]] AvatarCommandResult UpdateRemoteRootTransform(const CommandRecord& a_command) noexcept;
    [[nodiscard]] AvatarCommandResult DestroyRemoteAvatar(const CommandRecord& a_command) noexcept;
    void RetireAllOnCommandPumpOwner() noexcept;

private:
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
        AdapterHandle Token{};
        RE::ActorHandle Actor;
        std::uint64_t LastSequence{};
        std::uint64_t LastAction{};
        std::uint32_t LocalCellFormId{};
        std::uint32_t LocalWorldspaceFormId{};
        RootTransform Root{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    };

    struct EntityLedger
    {
        std::uint32_t EntityGeneration{};
        std::uint64_t LastSequence{};
        std::uint64_t LastAction{};
        bool Destroyed{};
    };

    [[nodiscard]] static AvatarKey MakeAvatarKey(const BridgeIdentity& a_identity) noexcept;
    [[nodiscard]] static EntityKey MakeEntityKey(const BridgeIdentity& a_identity) noexcept;
    [[nodiscard]] bool IsCommandPumpOwner() const noexcept;
    [[nodiscard]] static bool NormalizeRoot(const RootTransform& a_root, RootTransform& a_normalized, RE::NiPoint3& a_angles) noexcept;
    [[nodiscard]] static AvatarCommandResult ResultFor(const AvatarRecord& a_record, CommandStatus a_status) noexcept;
    void DestroyRecord(AvatarRecord& a_record) noexcept;

    std::unordered_map<AvatarKey, AvatarRecord, AvatarKeyHash> _avatars;
    std::unordered_map<EntityKey, EntityLedger, EntityKeyHash> _entityLedger;
    std::uint64_t _nextToken{GameplayBridge::kFirstRemoteAvatarHandle};
    std::uint32_t _commandPumpOwnerThreadId{};
    bool _tokenExhausted{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter
