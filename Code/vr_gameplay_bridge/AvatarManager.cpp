#include "AvatarManager.h"
#include "EventCapture.h"
#include "HumanoidAnimationGraph.h"

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

[[nodiscard]] bool ResolveLocation(
    const std::uint32_t a_cellFormId,
    const std::uint32_t a_worldspaceFormId,
    RE::TESObjectCELL*& a_cell,
    RE::TESWorldSpace*& a_worldspace) noexcept
{
    a_cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_cellFormId);
    a_worldspace = a_worldspaceFormId != 0 ? RE::TESForm::LookupByID<RE::TESWorldSpace>(a_worldspaceFormId) : nullptr;
    if (!a_cell || (a_worldspaceFormId != 0 && !a_worldspace))
        return false;
    return a_cell->IsInteriorCell() ? a_worldspace == nullptr : a_worldspace != nullptr && a_cell->GetRuntimeData().worldSpace == a_worldspace;
}

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

        RE::TESObjectCELL* cell{};
        RE::TESWorldSpace* worldspace{};
        if (!ResolveLocation(payload.LocalCellFormId, payload.LocalWorldspaceFormId, cell, worldspace)) {
            result.Status = CommandStatus::MissingCell;
            return result;
        }

        auto* base = RE::TESForm::LookupByID<RE::TESNPC>(payload.LocalActorBaseFormId);
        if (!base) {
            result.Status = CommandStatus::MissingForm;
            return result;
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            result.Status = CommandStatus::Inactive;
            return result;
        }
        const auto placedHandle = dataHandler->CreateReferenceAtLocation(
            base,
            {normalized.PositionX, normalized.PositionY, normalized.PositionZ},
            angles,
            cell,
            worldspace,
            nullptr,
            nullptr,
            RE::ObjectRefHandle{},
            false,
            true);
        const auto placed = placedHandle.get();
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
            _entityLedger[entityKey] = {identity.EntityGeneration, 0, 0, identity.ActionId, false};
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
        if (identity.SequenceId <= record.LastRootSequence) {
            result = ResultFor(record, CommandStatus::StaleEntity);
            return result;
        }
        const auto sourceCellFormId = record.LocalCellFormId;
        const auto sourceWorldspaceFormId = record.LocalWorldspaceFormId;
        const bool changesSpace = sourceCellFormId != a_command.Payload.UpdateRemoteRootTransform.LocalCellFormId ||
                                  sourceWorldspaceFormId != a_command.Payload.UpdateRemoteRootTransform.LocalWorldspaceFormId;
        const bool requestsTransfer =
            (a_command.Payload.UpdateRemoteRootTransform.UpdateFlags & GameplayBridge::SpatialTransfer) != 0;
        if (changesSpace != requestsTransfer)
            return ResultFor(record, CommandStatus::Malformed);

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
        RE::TESObjectCELL* targetCell{};
        RE::TESWorldSpace* targetWorldspace{};
        const auto& payload = a_command.Payload.UpdateRemoteRootTransform;
        if (!ResolveLocation(payload.LocalCellFormId, payload.LocalWorldspaceFormId, targetCell, targetWorldspace)) {
            result = ResultFor(record, CommandStatus::MissingCell);
            return result;
        }
        const RE::NiPoint3 position{normalized.PositionX, normalized.PositionY, normalized.PositionZ};
        if (record.LocalCellFormId != payload.LocalCellFormId ||
            record.LocalWorldspaceFormId != payload.LocalWorldspaceFormId) {
            if (!MoveActorToLocation(*actor, *targetCell, targetWorldspace, position, angles)) {
                result = ResultFor(record, CommandStatus::EngineRejected);
                return result;
            }
            const auto movedActor = record.Actor.get();
            if (!movedActor || movedActor->GetParentCell() != targetCell || movedActor->GetWorldspace() != targetWorldspace) {
                result = ResultFor(record, CommandStatus::EngineRejected);
                return result;
            }
        } else {
            actor->SetPosition(position, true);
            actor->SetAngle(angles);
        }
        actor->SetScale(normalized.Scale);
        record.LastRootSequence = identity.SequenceId;
        record.LocalCellFormId = payload.LocalCellFormId;
        record.LocalWorldspaceFormId = payload.LocalWorldspaceFormId;
        record.Root = normalized;
        _entityLedger[MakeEntityKey(identity)].LastRootSequence = identity.SequenceId;
        result = ResultFor(record, CommandStatus::Success);
        result.SpatialTransferApplied = changesSpace;
        result.SourceCellFormId = sourceCellFormId;
        result.SourceWorldspaceFormId = sourceWorldspaceFormId;
        return result;
    } catch (...) {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

AvatarCommandResult AvatarManager::ApplyRemoteAnimationGraphChunk(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner()) {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try {
        const auto& identity = a_command.Header.Identity;
        const auto it = _avatars.find(MakeAvatarKey(identity));
        const auto& payload = a_command.Payload.ApplyRemoteAnimationGraphChunk;
        if (it == _avatars.end() || it->second.Token.Value != payload.AvatarHandle.Value) {
            result.Status = CommandStatus::InvalidHandle;
            return result;
        }

        auto& record = it->second;
        if (identity.SequenceId <= record.LastAnimationSequence) {
            return ResultFor(record, CommandStatus::StaleEntity);
        }
        record.LastAnimationSequence = identity.SequenceId;
        _entityLedger[MakeEntityKey(identity)].LastAnimationSequence = identity.SequenceId;

        if (payload.SnapshotId <= record.LastAnimationSnapshot)
            return ResultFor(record, CommandStatus::Success);

        auto& snapshot = record.PendingAnimation;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        const auto accepted = AnimationGraphProtocol::AcceptChunk(
            snapshot, payload.SnapshotId, valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount,
            payload.Direction, payload.Values);
        if (accepted == AnimationGraphProtocol::ChunkAcceptResult::Malformed)
            return ResultFor(record, CommandStatus::Malformed);
        if (accepted == AnimationGraphProtocol::ChunkAcceptResult::Stale ||
            accepted == AnimationGraphProtocol::ChunkAcceptResult::Accepted)
            return ResultFor(record, CommandStatus::Success);

        const auto snapshotId = snapshot.SnapshotId;
        result = ResultFor(record, CommandStatus::Success);
        result.AnimationSnapshotId = snapshotId;
        switch (TryApplyPendingAnimation(record)) {
        case PendingAnimationResult::WaitingForGraph:
            return result;
        case PendingAnimationResult::Applied:
            result.AnimationApplied = true;
            return result;
        case PendingAnimationResult::Rejected:
            result.Status = CommandStatus::EngineRejected;
            snapshot = {};
            return result;
        }
        result.Status = CommandStatus::EngineRejected;
        return result;
    } catch (...) {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

void AvatarManager::ProcessPendingAnimationSnapshots() noexcept
{
    if (!IsCommandPumpOwner())
        return;

    for (auto& [key, record] : _avatars) {
        if (!record.PendingAnimation.IsComplete())
            continue;
        const auto snapshotId = record.PendingAnimation.SnapshotId;
        const auto applyResult = TryApplyPendingAnimation(record);
        if (applyResult == PendingAnimationResult::WaitingForGraph)
            continue;

        BridgeIdentity identity{};
        identity.ServerInstanceNonce = key.ServerInstanceNonce;
        identity.ConnectionGeneration = key.ConnectionGeneration;
        identity.LifecycleEpoch = key.LifecycleEpoch;
        identity.EntityId = key.EntityId;
        identity.EntityGeneration = key.EntityGeneration;
        identity.SequenceId = record.LastAnimationSequence;
        PublishRemoteAnimationGraphState(
            identity,
            record.Token,
            snapshotId,
            applyResult == PendingAnimationResult::Applied ? RemoteAnimationGraphState::Applied : RemoteAnimationGraphState::Faulted,
            applyResult == PendingAnimationResult::Applied ? CommandStatus::Success : CommandStatus::EngineRejected);
        if (applyResult == PendingAnimationResult::Rejected)
            record.PendingAnimation = {};
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
        const auto lastRootSequence = record.LastRootSequence;
        const auto lastAnimationSequence = record.LastAnimationSequence;
        DestroyRecord(record);
        _avatars.erase(it);
        _entityLedger[MakeEntityKey(identity)] = {
            identity.EntityGeneration, lastRootSequence, lastAnimationSequence, identity.ActionId, true};
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

bool AvatarManager::MoveActorToLocation(
    RE::Actor& a_actor,
    RE::TESObjectCELL& a_cell,
    RE::TESWorldSpace* a_worldspace,
    const RE::NiPoint3& a_position,
    const RE::NiPoint3& a_angles) noexcept
{
    try {
        using MoveTo = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*,
                            const RE::NiPoint3&, const RE::NiPoint3&);
        static REL::Relocation<MoveTo> moveTo{RELOCATION_ID(56227, 56626)};
        moveTo(&a_actor, RE::ObjectRefHandle{}, &a_cell, a_worldspace, a_position, a_angles);
        return a_actor.GetParentCell() == &a_cell && a_actor.GetWorldspace() == a_worldspace;
    } catch (...) {
        return false;
    }
}

bool AvatarManager::ApplyAnimationSnapshot(
    RE::Actor& a_actor,
    const AvatarRecord::PendingAnimationSnapshot& a_snapshot) noexcept
{
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> initialManager;
    if (!a_actor.GetAnimationGraphManager(initialManager) || !initialManager)
        return false;

    AvatarRecord::PendingAnimationSnapshot previous{};
    for (std::size_t index = 0; index < HumanoidAnimationGraph::kBooleanNames.size(); ++index) {
        if (!a_actor.GetGraphVariableBool(RE::BSFixedString(HumanoidAnimationGraph::kBooleanNames[index].data()), previous.Booleans[index]))
            return false;
    }
    for (std::size_t index = 0; index < HumanoidAnimationGraph::kFloatNames.size(); ++index) {
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(HumanoidAnimationGraph::kFloatNames[index].data()), previous.Floats[index]))
            return false;
    }
    for (std::size_t index = 0; index < HumanoidAnimationGraph::kIntegerNames.size(); ++index) {
        if (!a_actor.GetGraphVariableInt(RE::BSFixedString(HumanoidAnimationGraph::kIntegerNames[index].data()), previous.Integers[index]))
            return false;
    }

    const auto managerUnchanged = [&]() noexcept {
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> currentManager;
        return a_actor.GetAnimationGraphManager(currentManager) && currentManager && currentManager.get() == initialManager.get();
    };
    std::size_t booleansWritten{};
    std::size_t floatsWritten{};
    std::size_t integersWritten{};
    const auto rollback = [&]() noexcept {
        if (!managerUnchanged())
            return;
        for (std::size_t index = 0; index < booleansWritten; ++index)
            a_actor.SetGraphVariableBool(RE::BSFixedString(HumanoidAnimationGraph::kBooleanNames[index].data()), previous.Booleans[index]);
        for (std::size_t index = 0; index < floatsWritten; ++index)
            a_actor.SetGraphVariableFloat(RE::BSFixedString(HumanoidAnimationGraph::kFloatNames[index].data()), previous.Floats[index]);
        for (std::size_t index = 0; index < integersWritten; ++index)
            a_actor.SetGraphVariableInt(RE::BSFixedString(HumanoidAnimationGraph::kIntegerNames[index].data()), previous.Integers[index]);
    };

    for (; booleansWritten < HumanoidAnimationGraph::kBooleanNames.size(); ++booleansWritten) {
        if (!managerUnchanged() || !a_actor.SetGraphVariableBool(
                RE::BSFixedString(HumanoidAnimationGraph::kBooleanNames[booleansWritten].data()),
                a_snapshot.Booleans[booleansWritten])) {
            rollback();
            return false;
        }
    }
    for (; floatsWritten < HumanoidAnimationGraph::kFloatNames.size(); ++floatsWritten) {
        if (!managerUnchanged() || !a_actor.SetGraphVariableFloat(
                RE::BSFixedString(HumanoidAnimationGraph::kFloatNames[floatsWritten].data()),
                a_snapshot.Floats[floatsWritten])) {
            rollback();
            return false;
        }
    }
    for (; integersWritten < HumanoidAnimationGraph::kIntegerNames.size(); ++integersWritten) {
        if (!managerUnchanged() || !a_actor.SetGraphVariableInt(
                RE::BSFixedString(HumanoidAnimationGraph::kIntegerNames[integersWritten].data()),
                a_snapshot.Integers[integersWritten])) {
            rollback();
            return false;
        }
    }
    if (!managerUnchanged() || !a_actor.SetGraphVariableFloat(RE::BSFixedString("Direction"), a_snapshot.Direction) ||
        !managerUnchanged()) {
        rollback();
        return false;
    }
    return true;
}

bool AvatarManager::ValidateAnimationGraph(RE::Actor& a_actor) noexcept
{
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!a_actor.GetAnimationGraphManager(manager) || !manager)
        return false;

    bool valid = true;
    bool booleanValue{};
    float floatValue{};
    std::int32_t integerValue{};
    for (const auto name : HumanoidAnimationGraph::kBooleanNames)
        valid = a_actor.GetGraphVariableBool(RE::BSFixedString(name.data()), booleanValue) && valid;
    for (const auto name : HumanoidAnimationGraph::kFloatNames)
        valid = a_actor.GetGraphVariableFloat(RE::BSFixedString(name.data()), floatValue) && valid;
    for (const auto name : HumanoidAnimationGraph::kIntegerNames)
        valid = a_actor.GetGraphVariableInt(RE::BSFixedString(name.data()), integerValue) && valid;
    return valid;
}

AvatarManager::PendingAnimationResult AvatarManager::TryApplyPendingAnimation(AvatarRecord& a_record) noexcept
{
    if (!a_record.PendingAnimation.IsComplete())
        return PendingAnimationResult::WaitingForGraph;
    const auto actor = a_record.Actor.get();
    if (!actor)
        return PendingAnimationResult::Rejected;

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!actor->GetAnimationGraphManager(manager) || !manager)
        return PendingAnimationResult::WaitingForGraph;
    if (!a_record.AnimationGraphValidated) {
        if (!ValidateAnimationGraph(*actor))
            return PendingAnimationResult::Rejected;
        a_record.AnimationGraphValidated = true;
    }
    if (!ApplyAnimationSnapshot(*actor, a_record.PendingAnimation)) {
        a_record.AnimationGraphValidated = false;
        return PendingAnimationResult::Rejected;
    }

    a_record.LastAnimationSnapshot = a_record.PendingAnimation.SnapshotId;
    a_record.PendingAnimation = {};
    return PendingAnimationResult::Applied;
}

void AvatarManager::DestroyRecord(AvatarRecord& a_record) noexcept
{
    if (const auto actor = a_record.Actor.get()) {
        actor->Disable();
        actor->SetDelete(true);
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter
