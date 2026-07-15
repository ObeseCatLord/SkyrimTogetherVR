#include "AvatarManager.h"

#include <vr_common/VRCanonicalEntity.h>

#include <cmath>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr float kMinimumScale = 0.1f;
constexpr float kMaximumScale = 10.0f;
constexpr std::size_t kMaximumRemoteAvatars = 64;
constexpr double kMinimumQuaternionNormSquared = 1.0e-12;
constexpr double kPiOverTwo = 1.57079632679489661923;

[[nodiscard]] bool IsFinite(const RootTransform& a_root) noexcept
{
    return std::isfinite(a_root.PositionX) && std::isfinite(a_root.PositionY) && std::isfinite(a_root.PositionZ) &&
           std::isfinite(a_root.RotationX) && std::isfinite(a_root.RotationY) && std::isfinite(a_root.RotationZ) &&
           std::isfinite(a_root.RotationW) && std::isfinite(a_root.Scale);
}

[[nodiscard]] std::size_t HashCombine(std::size_t a_seed, const std::uint64_t a_value) noexcept
{
    return a_seed ^ (std::hash<std::uint64_t>{}(a_value) + 0x9e3779b97f4a7c15ull + (a_seed << 6) + (a_seed >> 2));
}

class PendingActorCleanup final
{
public:
    explicit PendingActorCleanup(RE::Actor* a_actor) noexcept
        : _actor(a_actor)
    {
    }

    ~PendingActorCleanup() noexcept
    {
        if (_actor) {
            _actor->Disable();
            _actor->SetDelete(true);
        }
    }

    void Release() noexcept { _actor = nullptr; }

private:
    RE::Actor* _actor;
};
} // namespace

AvatarManager& AvatarManager::Get() noexcept
{
    static AvatarManager manager;
    return manager;
}

void AvatarManager::BindCommandPumpOwner(const std::uint32_t a_threadId) noexcept
{
    _commandPumpOwnerThreadId = a_threadId;
}

AvatarCommandResult AvatarManager::CreateRemoteAvatar(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner()) {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try {
        const auto& identity = a_command.Header.Identity;
        const auto& payload = a_command.Payload.CreateRemoteAvatar;
        const auto key = MakeAvatarKey(identity);
        if (const auto it = _avatars.find(key); it != _avatars.end())
            return ResultFor(it->second, CommandStatus::Success);

        const auto entityKey = MakeEntityKey(identity);
        if (const auto ledgerIt = _entityLedger.find(entityKey); ledgerIt != _entityLedger.end()) {
            const auto& ledger = ledgerIt->second;
            if (!ledger.Destroyed || !CanonicalEntity::CanCreateAfterDestroyedGeneration(
                    identity.EntityGeneration,
                    identity.ActionId,
                    ledger.EntityGeneration,
                    ledger.LastAction)) {
                result.Status = CommandStatus::StaleEntity;
                return result;
            }
        }
        if (_avatars.size() >= kMaximumRemoteAvatars || _tokenExhausted || _nextToken == 0) {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }

        RootTransform normalized{};
        RE::NiPoint3 angles{};
        if (!NormalizeRoot(payload.InitialRoot, normalized, angles)) {
            result.Status = CommandStatus::Malformed;
            return result;
        }
        result.Root = normalized;
        result.LocalCellFormId = payload.LocalCellFormId;
        result.LocalWorldspaceFormId = payload.LocalWorldspaceFormId;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            result.Status = CommandStatus::Inactive;
            return result;
        }
        const auto* cell = player->GetParentCell();
        if (!cell || payload.LocalCellFormId == 0 || cell->GetFormID() != payload.LocalCellFormId) {
            result.Status = CommandStatus::MissingCell;
            return result;
        }
        if (payload.LocalWorldspaceFormId != 0) {
            const auto* worldspace = player->GetWorldspace();
            if (!worldspace || worldspace->GetFormID() != payload.LocalWorldspaceFormId) {
                result.Status = CommandStatus::MissingCell;
                return result;
            }
        }

        auto* base = RE::TESForm::LookupByID<RE::TESNPC>(payload.LocalActorBaseFormId);
        if (!base) {
            result.Status = CommandStatus::MissingForm;
            return result;
        }
        const auto placed = player->PlaceObjectAtMe(base, false);
        auto* actor = placed ? placed->As<RE::Actor>() : nullptr;
        if (!actor) {
            if (placed) {
                placed->Disable();
                placed->SetDelete(true);
            }
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        PendingActorCleanup pendingActor{actor};

        RE::ActorHandle actorHandle{actor};
        if (!actorHandle) {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        const auto resolvedActor = actorHandle.get();
        if (!resolvedActor) {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        resolvedActor->SetPosition({normalized.PositionX, normalized.PositionY, normalized.PositionZ}, true);
        resolvedActor->SetAngle(angles);
        resolvedActor->SetScale(normalized.Scale);
        AvatarRecord record{};
        record.Token = {_nextToken};
        record.Actor = actorHandle;
        record.LastAction = identity.ActionId;
        record.LocalCellFormId = payload.LocalCellFormId;
        record.LocalWorldspaceFormId = payload.LocalWorldspaceFormId;
        record.Root = normalized;
        const auto [it, inserted] = _avatars.emplace(key, std::move(record));
        if (!inserted) {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        try {
            _entityLedger[entityKey] = {identity.EntityGeneration, 0, identity.ActionId, false};
        } catch (...) {
            _avatars.erase(it);
            throw;
        }
        if (_nextToken == std::numeric_limits<std::uint64_t>::max())
            _tokenExhausted = true;
        else
            ++_nextToken;
        pendingActor.Release();
        return ResultFor(it->second, CommandStatus::Success);
    } catch (...) {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

AvatarCommandResult AvatarManager::UpdateRemoteRootTransform(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner()) {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try {
        const auto& identity = a_command.Header.Identity;
        const auto key = MakeAvatarKey(identity);
        const auto it = _avatars.find(key);
        if (it == _avatars.end() || it->second.Token.Value != a_command.Payload.UpdateRemoteRootTransform.AvatarHandle.Value) {
            result.Status = CommandStatus::InvalidHandle;
            return result;
        }
        auto& record = it->second;
        if (identity.SequenceId <= record.LastSequence) {
            result = ResultFor(record, CommandStatus::StaleEntity);
            return result;
        }

        RootTransform normalized{};
        RE::NiPoint3 angles{};
        if (!NormalizeRoot(a_command.Payload.UpdateRemoteRootTransform.Root, normalized, angles)) {
            result = ResultFor(record, CommandStatus::Malformed);
            return result;
        }

        const auto actor = record.Actor.get();
        if (!actor) {
            result = ResultFor(record, CommandStatus::InvalidHandle);
            return result;
        }
        actor->SetPosition({normalized.PositionX, normalized.PositionY, normalized.PositionZ}, true);
        actor->SetAngle(angles);
        actor->SetScale(normalized.Scale);
        record.LastSequence = identity.SequenceId;
        record.Root = normalized;
        _entityLedger[MakeEntityKey(identity)].LastSequence = identity.SequenceId;
        return ResultFor(record, CommandStatus::Success);
    } catch (...) {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

AvatarCommandResult AvatarManager::DestroyRemoteAvatar(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner()) {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try {
        const auto& identity = a_command.Header.Identity;
        const auto key = MakeAvatarKey(identity);
        const auto it = _avatars.find(key);
        if (it == _avatars.end() || it->second.Token.Value != a_command.Payload.DestroyRemoteAvatar.AvatarHandle.Value) {
            result.Status = CommandStatus::InvalidHandle;
            return result;
        }
        auto& record = it->second;
        if (identity.ActionId <= record.LastAction) {
            result = ResultFor(record, CommandStatus::StaleEntity);
            return result;
        }

        result = ResultFor(record, CommandStatus::Success);
        const auto lastSequence = record.LastSequence;
        DestroyRecord(record);
        _avatars.erase(it);
        _entityLedger[MakeEntityKey(identity)] = {identity.EntityGeneration, lastSequence, identity.ActionId, true};
        return result;
    } catch (...) {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

void AvatarManager::RetireAllOnCommandPumpOwner() noexcept
{
    if (!IsCommandPumpOwner())
        return;

    for (auto& [_, record] : _avatars)
        DestroyRecord(record);
    _avatars.clear();
    _entityLedger.clear();
}

std::size_t AvatarManager::AvatarKeyHash::operator()(const AvatarKey& a_key) const noexcept
{
    auto seed = HashCombine(0, a_key.ServerInstanceNonce);
    seed = HashCombine(seed, a_key.ConnectionGeneration);
    seed = HashCombine(seed, a_key.LifecycleEpoch);
    seed = HashCombine(seed, a_key.EntityId);
    return HashCombine(seed, a_key.EntityGeneration);
}

std::size_t AvatarManager::EntityKeyHash::operator()(const EntityKey& a_key) const noexcept
{
    auto seed = HashCombine(0, a_key.ServerInstanceNonce);
    seed = HashCombine(seed, a_key.ConnectionGeneration);
    seed = HashCombine(seed, a_key.LifecycleEpoch);
    return HashCombine(seed, a_key.EntityId);
}

AvatarManager::AvatarKey AvatarManager::MakeAvatarKey(const BridgeIdentity& a_identity) noexcept
{
    return {a_identity.ServerInstanceNonce, a_identity.ConnectionGeneration, a_identity.LifecycleEpoch, a_identity.EntityId,
            a_identity.EntityGeneration};
}

AvatarManager::EntityKey AvatarManager::MakeEntityKey(const BridgeIdentity& a_identity) noexcept
{
    return {a_identity.ServerInstanceNonce, a_identity.ConnectionGeneration, a_identity.LifecycleEpoch, a_identity.EntityId};
}

bool AvatarManager::IsCommandPumpOwner() const noexcept
{
    return _commandPumpOwnerThreadId != 0 && _commandPumpOwnerThreadId == GetCurrentThreadId();
}

bool AvatarManager::NormalizeRoot(const RootTransform& a_root, RootTransform& a_normalized, RE::NiPoint3& a_angles) noexcept
{
    if (!IsFinite(a_root) || a_root.Scale < kMinimumScale || a_root.Scale > kMaximumScale)
        return false;

    const auto x = static_cast<double>(a_root.RotationX);
    const auto y = static_cast<double>(a_root.RotationY);
    const auto z = static_cast<double>(a_root.RotationZ);
    const auto w = static_cast<double>(a_root.RotationW);
    const auto normSquared = x * x + y * y + z * z + w * w;
    if (!std::isfinite(normSquared) || normSquared <= kMinimumQuaternionNormSquared)
        return false;

    const auto inverseNorm = 1.0 / std::sqrt(normSquared);
    const auto nx = x * inverseNorm;
    const auto ny = y * inverseNorm;
    const auto nz = z * inverseNorm;
    const auto nw = w * inverseNorm;
    const auto sinPitch = 2.0 * (nw * ny - nz * nx);
    a_angles.x = static_cast<float>(std::atan2(2.0 * (nw * nx + ny * nz), 1.0 - 2.0 * (nx * nx + ny * ny)));
    a_angles.y = static_cast<float>(std::abs(sinPitch) >= 1.0 ? std::copysign(kPiOverTwo, sinPitch) : std::asin(sinPitch));
    a_angles.z = static_cast<float>(std::atan2(2.0 * (nw * nz + nx * ny), 1.0 - 2.0 * (ny * ny + nz * nz)));

    a_normalized = a_root;
    a_normalized.RotationX = static_cast<float>(nx);
    a_normalized.RotationY = static_cast<float>(ny);
    a_normalized.RotationZ = static_cast<float>(nz);
    a_normalized.RotationW = static_cast<float>(nw);
    return true;
}

AvatarCommandResult AvatarManager::ResultFor(const AvatarRecord& a_record, const CommandStatus a_status) noexcept
{
    return {a_status, a_record.Token, a_record.LocalCellFormId, a_record.LocalWorldspaceFormId, a_record.Root};
}

void AvatarManager::DestroyRecord(AvatarRecord& a_record) noexcept
{
    if (const auto actor = a_record.Actor.get()) {
        actor->Disable();
        actor->SetDelete(true);
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
