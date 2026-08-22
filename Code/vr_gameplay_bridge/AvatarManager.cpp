#include "AvatarManager.h"
#include "ActorAuthorityHooks.h"
#include "AnimationAppearanceManager.h"
#include "EventCapture.h"
#include "AnimationGraphDescriptors.h"
#include "LocalGameplayCapture.h"
#include "RemoteSaveExclusion.h"
#include "RemoteSolvedPosePresentation.h"
#include "remote_actor_admission_policy.h"

#include <vr_common/VRCanonicalEntity.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include <spdlog/spdlog.h>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr float kMinimumScale = 0.1f;
constexpr float kMaximumScale = 10.0f;
constexpr std::size_t kMaximumRemoteAvatars = ActorAuthorityHooks::kManagedRemoteActorRegistryCapacity;
constexpr std::size_t kMaximumLocalNativeActorBindings = 64;
constexpr double kMinimumQuaternionNormSquared = 1.0e-12;
constexpr double kPiOverTwo = 1.57079632679489661923;
constexpr float kAuthorityPositionTolerance = 0.01F;
constexpr float kAuthorityAngleTolerance = 0.001F;
constexpr float kAuthorityScaleTolerance = 0.001F;
constexpr float kAuthorityHealthTolerance = 0.01F;
constexpr std::uint64_t kMoveToVrRva = 0x09E90E0;
constexpr std::array<std::uint8_t, 16> kMoveToVrPrologue{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};

[[nodiscard]] bool ResolveLocation(const std::uint32_t a_cellFormId, const std::uint32_t a_worldspaceFormId, RE::TESObjectCELL*& a_cell, RE::TESWorldSpace*& a_worldspace) noexcept
{
    a_cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_cellFormId);
    a_worldspace = a_worldspaceFormId != 0 ? RE::TESForm::LookupByID<RE::TESWorldSpace>(a_worldspaceFormId) : nullptr;
    if (!a_cell || (a_worldspaceFormId != 0 && !a_worldspace))
        return false;
    return a_cell->IsInteriorCell() ? a_worldspace == nullptr : a_worldspace != nullptr && a_cell->GetRuntimeData().worldSpace == a_worldspace;
}

[[nodiscard]] bool IsFinite(const RootTransform& a_root) noexcept
{
    return std::isfinite(a_root.PositionX) && std::isfinite(a_root.PositionY) && std::isfinite(a_root.PositionZ) && std::isfinite(a_root.RotationX) &&
           std::isfinite(a_root.RotationY) && std::isfinite(a_root.RotationZ) && std::isfinite(a_root.RotationW) && std::isfinite(a_root.Scale);
}

[[nodiscard]] bool NearlyEqual(const float a_lhs, const float a_rhs, const float a_tolerance) noexcept
{
    return std::isfinite(a_lhs) && std::isfinite(a_rhs) && std::abs(a_lhs - a_rhs) <= a_tolerance;
}

[[nodiscard]] bool IsRootCurrent(const RE::Actor& a_actor, const RootTransform& a_root, const RE::NiPoint3& a_angles) noexcept
{
    const auto position = a_actor.GetPosition();
    const auto angle = a_actor.GetAngle();
    return NearlyEqual(position.x, a_root.PositionX, kAuthorityPositionTolerance) && NearlyEqual(position.y, a_root.PositionY, kAuthorityPositionTolerance) &&
           NearlyEqual(position.z, a_root.PositionZ, kAuthorityPositionTolerance) && NearlyEqual(angle.x, a_angles.x, kAuthorityAngleTolerance) &&
           NearlyEqual(angle.y, a_angles.y, kAuthorityAngleTolerance) && NearlyEqual(angle.z, a_angles.z, kAuthorityAngleTolerance) &&
           NearlyEqual(a_actor.GetScale(), a_root.Scale, kAuthorityScaleTolerance);
}

[[nodiscard]] std::size_t HashCombine(std::size_t a_seed, const std::uint64_t a_value) noexcept
{
    return a_seed ^ (std::hash<std::uint64_t>{}(a_value) + 0x9e3779b97f4a7c15ull + (a_seed << 6) + (a_seed >> 2));
}

class PendingActorCleanup final
{
public:
    explicit PendingActorCleanup(RE::Actor* a_actor, const bool a_owned) noexcept
        : _actor(a_actor)
        , _owned(a_owned)
    {
    }

    ~PendingActorCleanup() noexcept
    {
        if (_actor && _owned)
        {
            _actor->Disable();
            _actor->SetDelete(true);
        }
    }

    void Release() noexcept { _actor = nullptr; }

private:
    RE::Actor* _actor;
    bool _owned;
};

class PendingFormCleanup final
{
public:
    explicit PendingFormCleanup(RE::TESForm* a_form) noexcept
        : _form(a_form)
    {
    }
    ~PendingFormCleanup() noexcept
    {
        if (_form)
            _form->SetDelete(true);
    }
    void Reset(RE::TESForm* a_form) noexcept { _form = a_form; }
    void Release() noexcept { _form = nullptr; }

private:
    RE::TESForm* _form;
};

class PendingExistingAiRestore final
{
public:
    PendingExistingAiRestore(
        const RE::ActorHandle& a_actor,
        const bool a_originalAiWasEnabled,
        const bool a_active,
        bool& ar_restorationFailed) noexcept
        : _actor(a_actor)
        , _originalAiWasEnabled(a_originalAiWasEnabled)
        , _active(a_active)
        , _restorationFailed(ar_restorationFailed)
    {
    }

    ~PendingExistingAiRestore() noexcept
    {
        if (_active && !Restore())
            _restorationFailed = true;
    }

    [[nodiscard]] bool Restore() noexcept
    {
        if (!_active)
            return true;

        try
        {
            const auto actor = _actor.get();
            if (!actor)
                return false;
            actor->EnableAI(_originalAiWasEnabled);
            if (actor->IsAIEnabled() != _originalAiWasEnabled)
                return false;
            _active = false;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void Release() noexcept { _active = false; }

private:
    RE::ActorHandle _actor;
    bool _originalAiWasEnabled{};
    bool _active{};
    bool& _restorationFailed;
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

void AvatarManager::RetainFailedAuthorityActor(
    RE::Actor* a_actor,
    RE::TESNPC* a_visualBase,
    const bool a_ownsActor) noexcept
{
    try
    {
        _retainedAuthorityFailures.push_back({RE::ActorHandle{a_actor}, a_visualBase, a_ownsActor});
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: retained an untracked remote actor after authority retirement failed; endpoint remains faulted");
    }
    catch (...)
    {
        // The actor and form are intentionally left untouched even if the
        // diagnostic retention vector cannot allocate.
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: authority retirement failed before untracked actor cleanup; actor left untouched and endpoint remains faulted");
    }
}

bool AvatarManager::DestroyRegisteredActorWithoutRecord(
    RE::Actor* a_actor,
    RE::TESNPC* a_visualBase,
    const bool a_ownsActor,
    const bool a_restoreExistingAi,
    const bool a_existingAiWasEnabled) noexcept
{
    auto retirement = ActorAuthorityHooks::BeginRetireManagedRemoteActor(a_actor);
    if (!retirement.IsQuiescent())
    {
        RetainFailedAuthorityActor(a_actor, a_visualBase, a_ownsActor);
        return false;
    }

    bool existingAiRestored = true;
    if (a_restoreExistingAi)
    {
        if (!a_actor)
            existingAiRestored = false;
        else
        {
            try
            {
                a_actor->EnableAI(a_existingAiWasEnabled);
                existingAiRestored = a_actor->IsAIEnabled() == a_existingAiWasEnabled;
            }
            catch (...)
            {
                existingAiRestored = false;
            }
        }
    }
    if (!existingAiRestored)
    {
        RecordRemoteAvatarAiAdmissionFailure("failed to restore existing remote actor AI after rejected creation");
        RetainFailedAuthorityActor(a_actor, a_visualBase, a_ownsActor);
        return false;
    }

    if (a_ownsActor && a_actor)
    {
        a_actor->Disable();
        a_actor->SetDelete(true);
    }
    if (a_visualBase)
        a_visualBase->SetDelete(true);
    const auto finish = ActorAuthorityHooks::FinishRetireManagedRemoteActor(retirement);
    if (finish != ActorAuthorityHooks::ManagedRemoteActorRetirementResult::Quiescent)
    {
        RetainFailedAuthorityActor(a_actor, a_visualBase, a_ownsActor);
        return false;
    }
    return existingAiRestored;
}

AvatarCommandResult AvatarManager::CreateRemoteAvatar(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    AvatarKey insertedKey{};
    bool avatarInserted{};
    RE::Actor* registeredAuthorityActor{};
    RE::TESNPC* registeredAuthorityVisualBase{};
    bool registeredAuthorityOwnsActor{};
    bool registeredAuthorityRestoreExistingAi{};
    bool registeredAuthorityExistingAiWasEnabled{};
    bool authorityRegistered{};
    auto aiAdmission = RemoteActorAdmissionPolicy::AiDisableAdmission::NotAttempted;
    bool existingAiRestorationFailed{};
    if (!IsCommandPumpOwner())
    {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try
    {
        const auto& identity = a_command.Header.Identity;
        const auto& payload = a_command.Payload.CreateRemoteAvatar;
        if (!IsValidRemoteAvatarCreateFlags(payload.CreateFlags))
        {
            result.Status = CommandStatus::Malformed;
            return result;
        }
        const auto key = MakeAvatarKey(identity);
        const bool useExisting = (payload.CreateFlags & UseExistingReference) != 0;
        if (const auto it = _avatars.find(key); it != _avatars.end())
        {
            const auto& existing = it->second;
            if (existing.LocalActorBaseFormId != payload.LocalActorBaseFormId || existing.CreateFlags != payload.CreateFlags ||
                (useExisting && existing.LocalActorReferenceFormId != payload.LocalReferenceFormId) || existing.LocalCellFormId != payload.LocalCellFormId ||
                existing.LocalWorldspaceFormId != payload.LocalWorldspaceFormId)
            {
                result.Status = CommandStatus::StaleEntity;
                return result;
            }
            return ResultFor(it->second, CommandStatus::Success);
        }

        const auto entityKey = MakeEntityKey(identity);
        if (const auto ledgerIt = _entityLedger.find(entityKey); ledgerIt != _entityLedger.end())
        {
            const auto& ledger = ledgerIt->second;
            if (!ledger.Destroyed || !CanonicalEntity::CanCreateAfterDestroyedGeneration(identity.EntityGeneration, identity.ActionId, ledger.EntityGeneration, ledger.LastAction))
            {
                result.Status = CommandStatus::StaleEntity;
                return result;
            }
        }
        if (_avatars.size() >= kMaximumRemoteAvatars || _tokenExhausted || _nextToken == 0)
        {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }

        RootTransform normalized{};
        RE::NiPoint3 angles{};
        if (!NormalizeRoot(payload.InitialRoot, normalized, angles))
        {
            result.Status = CommandStatus::Malformed;
            return result;
        }
        result.Root = normalized;
        result.LocalCellFormId = payload.LocalCellFormId;
        result.LocalWorldspaceFormId = payload.LocalWorldspaceFormId;

        RE::TESObjectCELL* cell{};
        RE::TESWorldSpace* worldspace{};
        if (!ResolveLocation(payload.LocalCellFormId, payload.LocalWorldspaceFormId, cell, worldspace))
        {
            result.Status = CommandStatus::MissingCell;
            return result;
        }

        auto* templateBase = RE::TESForm::LookupByID<RE::TESNPC>(payload.LocalActorBaseFormId);
        if (!templateBase)
        {
            result.Status = CommandStatus::MissingForm;
            return result;
        }
        RE::TESNPC* base{};
        RE::Actor* actor{};
        PendingFormCleanup pendingForm{nullptr};
        if (useExisting)
        {
            actor = RE::TESForm::LookupByID<RE::Actor>(payload.LocalReferenceFormId);
            if (!actor || actor->GetActorBase() != templateBase)
            {
                result.Status = CommandStatus::MissingForm;
                return result;
            }
        }
        else
        {
            auto* duplicateForm = templateBase->CreateDuplicateForm(false, nullptr);
            base = duplicateForm ? duplicateForm->As<RE::TESNPC>() : nullptr;
            if (!base || base == templateBase)
            {
                result.Status = CommandStatus::EngineRejected;
                return result;
            }
            pendingForm.Reset(base);
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler)
            {
                result.Status = CommandStatus::Inactive;
                return result;
            }
            RE::ObjectRefHandle placedHandle;
            {
                ActorAuthorityHooks::ScopedAuthoritativeReplay authoritativeReplay;
                placedHandle = dataHandler->CreateReferenceAtLocation(
                    base, {normalized.PositionX, normalized.PositionY, normalized.PositionZ}, angles, cell, worldspace, nullptr, nullptr, RE::ObjectRefHandle{}, false, true);
            }
            const auto placed = placedHandle.get();
            actor = placed ? placed->As<RE::Actor>() : nullptr;
            if (!actor)
            {
                if (placed)
                {
                    placed->Disable();
                    placed->SetDelete(true);
                }
                result.Status = CommandStatus::EngineRejected;
                return result;
            }
        }
        PendingActorCleanup pendingActor{actor, !useExisting};

        // A newly placed remote actor is never admitted until the exact VR
        // engine operation has proven it is excluded from save serialization.
        // PendingActorCleanup deletes the owned reference on any failure here.
        if (!useExisting && !RemoteSaveExclusion::MarkTemporary(*actor))
        {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }

        RE::ActorHandle actorHandle{actor};
        if (!actorHandle)
        {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        const auto resolvedActor = actorHandle.get();
        if (!resolvedActor)
        {
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        if (useExisting)
        {
            if (resolvedActor->GetParentCell() != cell || resolvedActor->GetWorldspace() != worldspace)
            {
                result.Status = CommandStatus::EngineRejected;
                return result;
            }
        }
        else
        {
            ActorAuthorityHooks::ScopedAuthoritativeReplay authoritativeReplay;
            resolvedActor->SetPosition({normalized.PositionX, normalized.PositionY, normalized.PositionZ}, true);
            resolvedActor->SetAngle(angles);
            resolvedActor->SetScale(normalized.Scale);
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || resolvedActor.get() == player)
        {
            RecordRemoteAvatarAiAdmissionFailure("remote avatar creation resolved to the local player or no local player");
            result.Status = CommandStatus::EngineRejected;
            return result;
        }

        const bool existingAiWasEnabled = useExisting && resolvedActor->IsAIEnabled();
        PendingExistingAiRestore pendingExistingAi{actorHandle, existingAiWasEnabled, useExisting, existingAiRestorationFailed};
        aiAdmission = RemoteActorAdmissionPolicy::AiDisableAdmission::DisableRequested;
        resolvedActor->EnableAI(false);
        aiAdmission = resolvedActor->IsAIEnabled() ? RemoteActorAdmissionPolicy::AiDisableAdmission::Rejected :
                                                    RemoteActorAdmissionPolicy::AiDisableAdmission::ConfirmedDisabled;
        if (!RemoteActorAdmissionPolicy::CanPublishRemoteAvatar(aiAdmission))
        {
            const bool restored = !RemoteActorAdmissionPolicy::MustRestoreExistingAiOnFailedCreate(useExisting, aiAdmission) ||
                                  pendingExistingAi.Restore();
            RecordRemoteAvatarAiAdmissionFailure(
                restored ? "the engine refused to disable remote actor AI during avatar admission" :
                           "the engine refused remote actor AI disablement and original AI restoration");
            result.Status = CommandStatus::EngineRejected;
            return result;
        }

        const bool isPlayerAvatar = (payload.CreateFlags & PlayerAvatar) != 0;
        if (!ActorAuthorityHooks::RegisterManagedRemoteActor(resolvedActor.get(), isPlayerAvatar))
        {
            const bool restored = !RemoteActorAdmissionPolicy::MustRestoreExistingAiOnFailedCreate(useExisting, aiAdmission) ||
                                  pendingExistingAi.Restore();
            RecordRemoteAvatarAiAdmissionFailure(
                restored ? "remote actor authority registration failed after AI disable admission" :
                           "remote actor authority registration failed and original AI restoration failed");
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        registeredAuthorityActor = resolvedActor.get();
        registeredAuthorityVisualBase = base;
        registeredAuthorityOwnsActor = !useExisting;
        registeredAuthorityRestoreExistingAi = useExisting && existingAiWasEnabled;
        registeredAuthorityExistingAiWasEnabled = existingAiWasEnabled;
        authorityRegistered = true;
        // From this point the two-phase authority lifecycle owns cleanup. The
        // stack guards must not delete a registered actor during exception
        // unwinding before the registry has entered its retiring state.
        pendingActor.Release();
        pendingForm.Release();
        pendingExistingAi.Release();
        AvatarRecord record{};
        record.Token = {_nextToken};
        record.Actor = actorHandle;
        record.VisualBase = base;
        record.OwnsActor = !useExisting;
        record.IsPlayer = isPlayerAvatar;
        record.IsPlayerSummon = (payload.CreateFlags & PlayerSummon) != 0;
        record.ExistingAiStateCaptured = useExisting;
        record.ExistingAiWasEnabled = existingAiWasEnabled;
        record.RestoreAiOnDestroy = useExisting && existingAiWasEnabled;
        record.LastAction = identity.ActionId;
        record.LocalActorBaseFormId = payload.LocalActorBaseFormId;
        record.CreateFlags = payload.CreateFlags;
        record.LocalCellFormId = payload.LocalCellFormId;
        record.LocalWorldspaceFormId = payload.LocalWorldspaceFormId;
        record.LocalActorReferenceFormId = resolvedActor->GetFormID();
        record.Root = normalized;
        const auto [it, inserted] = _avatars.emplace(key, std::move(record));
        if (!inserted)
        {
            static_cast<void>(DestroyRegisteredActorWithoutRecord(
                registeredAuthorityActor,
                registeredAuthorityVisualBase,
                registeredAuthorityOwnsActor,
                registeredAuthorityRestoreExistingAi,
                registeredAuthorityExistingAiWasEnabled));
            authorityRegistered = false;
            RecordRemoteAvatarAiAdmissionFailure("remote avatar record insertion failed after AI disable admission");
            result.Status = CommandStatus::EngineRejected;
            return result;
        }
        insertedKey = key;
        avatarInserted = true;
        _entityLedger[entityKey] = {identity.EntityGeneration, 0, 0, identity.ActionId, false};
        ReconcileAuthoritativeRemoteActor(key, it->second);
        if (_nextToken == std::numeric_limits<std::uint64_t>::max())
            _tokenExhausted = true;
        else
            ++_nextToken;
        authorityRegistered = false;
        return ResultFor(it->second, CommandStatus::Success);
    }
    catch (...)
    {
        if (aiAdmission != RemoteActorAdmissionPolicy::AiDisableAdmission::NotAttempted)
        {
            RecordRemoteAvatarAiAdmissionFailure(
                existingAiRestorationFailed ? "exception during avatar creation left original AI restoration unconfirmed" :
                                              "exception during remote avatar creation after AI disable admission");
        }
        if (avatarInserted)
        {
            const auto it = _avatars.find(insertedKey);
            if (it != _avatars.end())
            {
                if (DestroyRecord(it->second))
                    _avatars.erase(it);
            }
        }
        else if (RemoteActorAdmissionPolicy::MustRetireRegisteredActorOnFailedCreate(authorityRegistered))
        {
            static_cast<void>(DestroyRegisteredActorWithoutRecord(
                registeredAuthorityActor,
                registeredAuthorityVisualBase,
                registeredAuthorityOwnsActor,
                registeredAuthorityRestoreExistingAi,
                registeredAuthorityExistingAiWasEnabled));
        }
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

AvatarCommandResult AvatarManager::UpdateRemoteRootTransform(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner())
    {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try
    {
        const auto& identity = a_command.Header.Identity;
        const auto key = MakeAvatarKey(identity);
        const auto it = _avatars.find(key);
        if (it == _avatars.end() || it->second.Token.Value != a_command.Payload.UpdateRemoteRootTransform.AvatarHandle.Value)
        {
            result.Status = CommandStatus::InvalidHandle;
            return result;
        }
        auto& record = it->second;
        if (identity.SequenceId <= record.LastRootSequence)
        {
            result = ResultFor(record, CommandStatus::StaleEntity);
            return result;
        }
        const auto sourceCellFormId = record.LocalCellFormId;
        const auto sourceWorldspaceFormId = record.LocalWorldspaceFormId;
        const bool changesSpace = sourceCellFormId != a_command.Payload.UpdateRemoteRootTransform.LocalCellFormId ||
                                  sourceWorldspaceFormId != a_command.Payload.UpdateRemoteRootTransform.LocalWorldspaceFormId;
        const bool requestsTransfer = (a_command.Payload.UpdateRemoteRootTransform.UpdateFlags & GameplayBridge::SpatialTransfer) != 0;
        if (changesSpace != requestsTransfer)
            return ResultFor(record, CommandStatus::Malformed);

        RootTransform normalized{};
        RE::NiPoint3 angles{};
        if (!NormalizeRoot(a_command.Payload.UpdateRemoteRootTransform.Root, normalized, angles))
        {
            result = ResultFor(record, CommandStatus::Malformed);
            return result;
        }

        const auto actor = record.Actor.get();
        if (!actor)
        {
            result = ResultFor(record, CommandStatus::InvalidHandle);
            return result;
        }
        RE::TESObjectCELL* targetCell{};
        RE::TESWorldSpace* targetWorldspace{};
        const auto& payload = a_command.Payload.UpdateRemoteRootTransform;
        if (!ResolveLocation(payload.LocalCellFormId, payload.LocalWorldspaceFormId, targetCell, targetWorldspace))
        {
            result = ResultFor(record, CommandStatus::MissingCell);
            return result;
        }
        const RE::NiPoint3 position{normalized.PositionX, normalized.PositionY, normalized.PositionZ};
        if (record.LocalCellFormId != payload.LocalCellFormId || record.LocalWorldspaceFormId != payload.LocalWorldspaceFormId)
        {
            {
                ActorAuthorityHooks::ScopedAuthoritativeReplay authoritativeReplay;
                if (!MoveActorToLocation(*actor, *targetCell, targetWorldspace, position, angles))
                {
                    result = ResultFor(record, CommandStatus::EngineRejected);
                    return result;
                }
            }
            const auto movedActor = record.Actor.get();
            if (!movedActor || movedActor->GetParentCell() != targetCell || movedActor->GetWorldspace() != targetWorldspace)
            {
                result = ResultFor(record, CommandStatus::EngineRejected);
                return result;
            }
        }
        else
        {
            ActorAuthorityHooks::ScopedAuthoritativeReplay authoritativeReplay;
            actor->SetPosition(position, true);
            actor->SetAngle(angles);
        }
        {
            ActorAuthorityHooks::ScopedAuthoritativeReplay authoritativeReplay;
            actor->SetScale(normalized.Scale);
        }
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
    }
    catch (...)
    {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

CommandStatus AvatarManager::ResolveGameplayActor(const CommandRecord& a_command, RE::NiPointer<RE::Actor>& ar_actor) noexcept
{
    if (!IsCommandPumpOwner())
        return CommandStatus::WrongThread;

    const auto& identity = a_command.Header.Identity;
    const auto& payload = a_command.Payload.ApplyGameplayAction;
    if (payload.TargetHandle.Value == kLocalPlayerHandle.Value)
    {
        const auto* mapping = BridgeEndpoint::Get().Mapping();
        SessionIdentitySnapshot session{};
        if (!mapping || identity.Reserved0 != 0 || identity.SequenceId != 0 || identity.ActionId == 0 || !CanonicalEntity::IsValid(identity.EntityId, identity.EntityGeneration) ||
            !TrySnapshotSessionIdentity(mapping->Header, session) || identity.ServerInstanceNonce != session.ServerInstanceNonce ||
            identity.ConnectionGeneration != session.ConnectionGeneration || identity.LifecycleEpoch != mapping->Header.LifecycleEpoch.load(std::memory_order_acquire))
            return CommandStatus::StaleEntity;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return CommandStatus::InvalidHandle;
        ar_actor = RE::NiPointer<RE::Actor>(player);
        return CommandStatus::Success;
    }

    const auto it = _avatars.find(MakeAvatarKey(identity));
    if (it == _avatars.end() || payload.TargetHandle.Value == 0 || it->second.Token.Value != payload.TargetHandle.Value)
        return CommandStatus::InvalidHandle;

    auto& record = it->second;
    if (identity.ActionId == 0 || identity.ActionId <= record.LastAction)
        return CommandStatus::StaleEntity;

    ar_actor = record.Actor.get();
    if (!ar_actor)
        return CommandStatus::InvalidHandle;

    record.LastAction = identity.ActionId;
    return CommandStatus::Success;
}

CommandStatus AvatarManager::ValidateLocalNativeGameplayActor(const CommandRecord& a_command) noexcept
{
    if (!IsCommandPumpOwner())
        return CommandStatus::WrongThread;

    try
    {
        const auto& identity = a_command.Header.Identity;
        const auto& payload = a_command.Payload.ApplyGameplayAction;
        if (payload.TargetHandle.Value != 0 || payload.TargetLocalFormId == 0 || !CanonicalEntity::IsValid(identity.EntityId, identity.EntityGeneration))
            return CommandStatus::Malformed;

        const auto* mapping = BridgeEndpoint::Get().Mapping();
        SessionIdentitySnapshot session{};
        if (!mapping || !TrySnapshotSessionIdentity(mapping->Header, session) || identity.ServerInstanceNonce != session.ServerInstanceNonce ||
            identity.ConnectionGeneration != session.ConnectionGeneration || identity.LifecycleEpoch != mapping->Header.LifecycleEpoch.load(std::memory_order_acquire))
            return CommandStatus::StaleEntity;

        const auto key = MakeAvatarKey(identity);
        const auto action = static_cast<GameplayAction>(payload.Action);
        const bool startNpcObservation = static_cast<GameplayDomain>(payload.Domain) == GameplayDomain::NpcOwnership && action == GameplayAction::StartNpcObservation;
        const bool stopNpcObservation = static_cast<GameplayDomain>(payload.Domain) == GameplayDomain::NpcOwnership && action == GameplayAction::StopNpcObservation;
        bool retiredMatchingStopBinding = false;
        for (auto binding = _localNativeActors.begin(); binding != _localNativeActors.end();)
        {
            const auto& bindingKey = binding->first;
            const auto boundActor = binding->second.Actor.get();
            if (!boundActor || boundActor->GetFormID() != binding->second.LocalReferenceFormId)
            {
                retiredMatchingStopBinding =
                    retiredMatchingStopBinding || (stopNpcObservation && bindingKey == key && binding->second.LocalReferenceFormId == payload.TargetLocalFormId);
                auto retired = binding++;
                RetireLocalNativeGameplayActor(retired);
                continue;
            }

            const bool sameSessionLifecycle = bindingKey.ServerInstanceNonce == identity.ServerInstanceNonce && bindingKey.ConnectionGeneration == identity.ConnectionGeneration &&
                                              bindingKey.LifecycleEpoch == identity.LifecycleEpoch;
            if (startNpcObservation && sameSessionLifecycle && bindingKey.EntityId == identity.EntityId)
            {
                const auto generationOrder = CanonicalEntity::CompareGenerations(identity.EntityGeneration, bindingKey.EntityGeneration);
                if (generationOrder == CanonicalEntity::GenerationOrder::Newer)
                {
                    auto retired = binding++;
                    RetireLocalNativeGameplayActor(retired);
                    continue;
                }
                if (generationOrder == CanonicalEntity::GenerationOrder::OlderOrAmbiguous)
                    return CommandStatus::StaleEntity;
            }
            ++binding;
        }

        const auto existing = _localNativeActors.find(key);
        if (existing != _localNativeActors.end())
        {
            if (existing->second.LocalReferenceFormId != payload.TargetLocalFormId)
                return CommandStatus::StaleEntity;
            if (stopNpcObservation)
                return CommandStatus::Success;
        }
        else if (stopNpcObservation && retiredMatchingStopBinding)
        {
            return CommandStatus::Success;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(payload.TargetLocalFormId);
        if (!actor || actor == RE::PlayerCharacter::GetSingleton())
            return CommandStatus::InvalidHandle;

        if (existing != _localNativeActors.end())
        {
            const auto boundActor = existing->second.Actor.get();
            if (boundActor.get() != actor)
                return CommandStatus::StaleEntity;
            return CommandStatus::Success;
        }

        if (!startNpcObservation)
        {
            return CommandStatus::InvalidHandle;
        }

        for (const auto& [bindingKey, binding] : _localNativeActors)
        {
            if (bindingKey.ServerInstanceNonce != identity.ServerInstanceNonce || bindingKey.ConnectionGeneration != identity.ConnectionGeneration ||
                bindingKey.LifecycleEpoch != identity.LifecycleEpoch)
                continue;
            const auto boundActor = binding.Actor.get();
            if ((bindingKey.EntityId != identity.EntityId || bindingKey.EntityGeneration != identity.EntityGeneration) &&
                (binding.LocalReferenceFormId == payload.TargetLocalFormId || boundActor.get() == actor))
            {
                return CommandStatus::StaleEntity;
            }
        }

        if (_localNativeActors.size() >= kMaximumLocalNativeActorBindings)
            return CommandStatus::QueueOverflow;

        RE::ActorHandle handle{actor};
        const auto resolved = handle.get();
        if (!resolved || resolved->GetFormID() != payload.TargetLocalFormId || resolved.get() != actor)
            return CommandStatus::InvalidHandle;
        const auto [_, inserted] = _localNativeActors.emplace(key, LocalNativeActorBinding{handle, payload.TargetLocalFormId});
        return inserted ? CommandStatus::Success : CommandStatus::EngineRejected;
    }
    catch (...)
    {
        return CommandStatus::EngineRejected;
    }
}

void AvatarManager::ReleaseLocalNativeGameplayActor(const CommandRecord& a_command) noexcept
{
    if (!IsCommandPumpOwner())
        return;

    try
    {
        const auto& payload = a_command.Payload.ApplyGameplayAction;
        const auto binding = _localNativeActors.find(MakeAvatarKey(a_command.Header.Identity));
        if (binding != _localNativeActors.end() && binding->second.LocalReferenceFormId == payload.TargetLocalFormId)
            RetireLocalNativeGameplayActor(binding);
    }
    catch (...)
    {
    }
}

CommandStatus AvatarManager::ResolveActorByHandle(const BridgeIdentity& a_identity, const AdapterHandle a_handle, RE::NiPointer<RE::Actor>& ar_actor) noexcept
{
    if (!IsCommandPumpOwner())
        return CommandStatus::WrongThread;
    if (a_handle.Value < kFirstRemoteAvatarHandle)
        return CommandStatus::InvalidHandle;

    for (auto& [key, record] : _avatars)
    {
        if (key.ServerInstanceNonce != a_identity.ServerInstanceNonce || key.ConnectionGeneration != a_identity.ConnectionGeneration ||
            key.LifecycleEpoch != a_identity.LifecycleEpoch || record.Token.Value != a_handle.Value)
            continue;
        ar_actor = record.Actor.get();
        return ar_actor ? CommandStatus::Success : CommandStatus::InvalidHandle;
    }
    return CommandStatus::InvalidHandle;
}

bool AvatarManager::IsManagedRemoteActor(const RE::Actor* a_actor) const noexcept
{
    return ActorAuthorityHooks::IsManagedRemoteActor(a_actor);
}

bool AvatarManager::IsManagedRemotePlayerActor(const RE::Actor* a_actor) const noexcept
{
    return ActorAuthorityHooks::IsManagedRemotePlayerActor(a_actor);
}

bool AvatarManager::IsPlayerAvatar(const BridgeIdentity& a_identity, const AdapterHandle a_handle) const noexcept
{
    if (a_handle.Value == 0 || a_handle.Value == kLocalPlayerHandle.Value)
        return a_handle.Value == kLocalPlayerHandle.Value;
    const auto found = _avatars.find(MakeAvatarKey(a_identity));
    return found != _avatars.end() && found->second.Token.Value == a_handle.Value && found->second.IsPlayer;
}

CommandStatus AvatarManager::CaptureAnimationSnapshotForApply(
    RE::Actor& a_actor, const AnimationGraphProtocol::SnapshotBuffer& a_expected,
    AnimationGraphProtocol::SnapshotBuffer& ar_previous) noexcept
{
    if (!IsCommandPumpOwner())
        return CommandStatus::WrongThread;
    if (!a_expected.IsComplete())
        return CommandStatus::Malformed;
    return AnimationGraphs::CaptureForApply(a_actor, a_expected, ar_previous) ?
               CommandStatus::Success :
               CommandStatus::EngineRejected;
}

CommandStatus AvatarManager::ApplyAnimationSnapshotToActor(RE::Actor& a_actor, const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept
{
    if (!IsCommandPumpOwner())
        return CommandStatus::WrongThread;
    if (!a_snapshot.IsComplete())
        return CommandStatus::Malformed;
    return ApplyAnimationSnapshot(a_actor, a_snapshot) ? CommandStatus::Success : CommandStatus::EngineRejected;
}

AvatarCommandResult AvatarManager::ApplyRemoteAnimationGraphChunk(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner())
    {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try
    {
        const auto& identity = a_command.Header.Identity;
        const auto it = _avatars.find(MakeAvatarKey(identity));
        const auto& payload = a_command.Payload.ApplyRemoteAnimationGraphChunk;
        if (it == _avatars.end() || it->second.Token.Value != payload.AvatarHandle.Value)
        {
            result.Status = CommandStatus::InvalidHandle;
            return result;
        }

        auto& record = it->second;
        if (identity.SequenceId <= record.LastAnimationSequence)
        {
            return ResultFor(record, CommandStatus::StaleEntity);
        }
        record.LastAnimationSequence = identity.SequenceId;
        _entityLedger[MakeEntityKey(identity)].LastAnimationSequence = identity.SequenceId;

        if (payload.SnapshotId <= record.LastAnimationSnapshot)
            return ResultFor(record, CommandStatus::Success);

        auto& snapshot = record.PendingAnimation;
        const auto valueType = static_cast<AnimationGraphProtocol::ValueType>(payload.ValueType);
        const auto accepted = AnimationGraphProtocol::AcceptChunk(
            snapshot, payload.SnapshotId, payload.DescriptorDigest, payload.DirectionFloatIndex,
            valueType, payload.StartIndex, payload.ValueCount, payload.TotalCount, payload.Direction, payload.Values);
        if (accepted == AnimationGraphProtocol::ChunkAcceptResult::Malformed)
            return ResultFor(record, CommandStatus::Malformed);
        if (accepted == AnimationGraphProtocol::ChunkAcceptResult::Stale || accepted == AnimationGraphProtocol::ChunkAcceptResult::Accepted)
            return ResultFor(record, CommandStatus::Success);

        const auto snapshotId = snapshot.SnapshotId;
        result = ResultFor(record, CommandStatus::Success);
        result.AnimationSnapshotId = snapshotId;
        switch (TryApplyPendingAnimation(record))
        {
        case PendingAnimationResult::WaitingForGraph: return result;
        case PendingAnimationResult::Applied: result.AnimationApplied = true; return result;
        case PendingAnimationResult::Rejected:
            result.Status = CommandStatus::EngineRejected;
            snapshot = {};
            return result;
        }
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
    catch (...)
    {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

void AvatarManager::ProcessPendingAnimationSnapshots() noexcept
{
    if (!IsCommandPumpOwner())
        return;

    for (auto& [key, record] : _avatars)
    {
        if (!record.PendingAnimation.IsComplete())
            continue;
        auto& endpoint = BridgeEndpoint::Get();
        BridgeEndpoint::CommandResultReservation resultReservation;
        if (!endpoint.TryReserveCommandResultEvents(1, resultReservation))
            break;
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
        if (!PublishRemoteAnimationGraphState(
                resultReservation, identity, record.Token, snapshotId,
                applyResult == PendingAnimationResult::Applied ? RemoteAnimationGraphState::Applied : RemoteAnimationGraphState::Faulted,
                applyResult == PendingAnimationResult::Applied ? CommandStatus::Success : CommandStatus::EngineRejected))
        {
            endpoint.Fault("reserved pending-animation result commit failed");
            break;
        }
        if (applyResult == PendingAnimationResult::Rejected)
            record.PendingAnimation = {};
    }
}

void AvatarManager::ProcessAuthoritativeRemoteActors() noexcept
{
    if (!IsCommandPumpOwner())
        return;

    // Avatar creation is capped, and records are never inserted by this pass.
    // Keep the correction owner-thread-only and bounded even after a bad peer
    // submits a long sequence of stale lifecycle commands.
    for (auto& [key, record] : _avatars)
        ReconcileAuthoritativeRemoteActor(key, record);
}

void AvatarManager::RecordAuthoritativeActorState(const CommandRecord& a_command, const RE::Actor& a_actor) noexcept
{
    if (!IsCommandPumpOwner())
        return;

    try
    {
        const auto& payload = a_command.Payload.ApplyGameplayAction;
        if (payload.TargetHandle.Value < kFirstRemoteAvatarHandle)
            return;

        const auto found = _avatars.find(MakeAvatarKey(a_command.Header.Identity));
        const auto boundActor = found != _avatars.end() ? found->second.Actor.get() : RE::NiPointer<RE::Actor>{};
        if (found == _avatars.end() || found->second.Token.Value != payload.TargetHandle.Value || !boundActor || boundActor.get() != &a_actor)
            return;

        const auto action = static_cast<GameplayAction>(payload.Action);
        RemoteActorAuthorityTransition transition{};
        switch (action)
        {
        case GameplayAction::SetActorValue:
            if (payload.LocalFormIdA != static_cast<std::uint32_t>(RE::ActorValue::kHealth))
                return;
            transition = a_actor.IsDead() ? RemoteActorAuthorityTransition::Death : RemoteActorAuthorityTransition::SetHealth;
            break;
        case GameplayAction::ModifyActorValue:
            if (payload.LocalFormIdA != static_cast<std::uint32_t>(RE::ActorValue::kHealth))
                return;
            transition = a_actor.IsDead() ? RemoteActorAuthorityTransition::Death : RemoteActorAuthorityTransition::ModifyHealth;
            break;
        case GameplayAction::SetDeathState: transition = payload.ValueA != 0 ? RemoteActorAuthorityTransition::Death : RemoteActorAuthorityTransition::Respawn; break;
        case GameplayAction::Respawn: transition = RemoteActorAuthorityTransition::Respawn; break;
        default: return;
        }

        const auto health = a_actor.GetActorValue(RE::ActorValue::kHealth);
        if (!ApplyRemoteActorAuthorityTransition(found->second.Authority, transition, health))
            RecordAuthorityFailure(found->second, "the applied actor health was not finite");
    }
    catch (...)
    {
        // This post-command observation must never alter an already-published
        // command result. The next owner tick can still reconcile other actors.
    }
}

AvatarCommandResult AvatarManager::DestroyRemoteAvatar(const CommandRecord& a_command) noexcept
{
    AvatarCommandResult result{};
    if (!IsCommandPumpOwner())
    {
        result.Status = CommandStatus::WrongThread;
        return result;
    }

    try
    {
        const auto& identity = a_command.Header.Identity;
        const auto key = MakeAvatarKey(identity);
        const auto it = _avatars.find(key);
        if (it == _avatars.end() || it->second.Token.Value != a_command.Payload.DestroyRemoteAvatar.AvatarHandle.Value)
        {
            result.Status = CommandStatus::InvalidHandle;
            return result;
        }
        auto& record = it->second;
        if (identity.ActionId <= record.LastAction)
        {
            result = ResultFor(record, CommandStatus::StaleEntity);
            return result;
        }

        result = ResultFor(record, CommandStatus::Success);
        const auto lastRootSequence = record.LastRootSequence;
        const auto lastAnimationSequence = record.LastAnimationSequence;
        const auto actor = record.Actor.get();
        const auto actorBase = actor ? actor->GetActorBase() : record.VisualBase;
        if (!DestroyRecord(record))
        {
            result = ResultFor(record, CommandStatus::EngineRejected);
            return result;
        }
        AnimationAppearanceManager::ForgetTarget(record.Token, actorBase);
        _avatars.erase(it);
        _entityLedger[MakeEntityKey(identity)] = {identity.EntityGeneration, lastRootSequence, lastAnimationSequence, identity.ActionId, true};
        return result;
    }
    catch (...)
    {
        result.Status = CommandStatus::EngineRejected;
        return result;
    }
}

void AvatarManager::RetireAllOnCommandPumpOwner() noexcept
{
    if (!IsCommandPumpOwner())
        return;

    for (auto it = _avatars.begin(); it != _avatars.end();)
    {
        auto& record = it->second;
        const auto actor = record.Actor.get();
        const auto actorBase = actor ? actor->GetActorBase() : record.VisualBase;
        if (!DestroyRecord(record))
        {
            ++it;
            continue;
        }
        AnimationAppearanceManager::ForgetTarget(record.Token, actorBase);
        it = _avatars.erase(it);
    }
    if (_avatars.empty())
        _entityLedger.clear();
    while (!_localNativeActors.empty())
        RetireLocalNativeGameplayActor(_localNativeActors.begin());
}

void AvatarManager::ReconcileAuthoritativeRemoteActor(const AvatarKey& a_key, AvatarRecord& ar_record) noexcept
{
    try
    {
        const auto ledger = _entityLedger.find({a_key.ServerInstanceNonce, a_key.ConnectionGeneration, a_key.LifecycleEpoch, a_key.EntityId});
        if (ledger == _entityLedger.end() || ledger->second.Destroyed || ledger->second.EntityGeneration != a_key.EntityGeneration)
            return;

        const auto actor = ar_record.Actor.get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || !player || actor.get() == player)
        {
            RecordAuthorityFailure(ar_record, "the remote actor no longer resolves to a non-local actor");
            return;
        }

        const bool aiEnabled = actor->IsAIEnabled();
        if (!ar_record.OwnsActor && !ar_record.ExistingAiStateCaptured)
        {
            ar_record.ExistingAiStateCaptured = true;
            ar_record.ExistingAiWasEnabled = aiEnabled;
        }
        if (aiEnabled)
        {
            actor->EnableAI(false);
            if (actor->IsAIEnabled())
            {
                RecordAuthorityFailure(ar_record, "the engine refused to disable remote actor AI");
                return;
            }
            if (!ar_record.OwnsActor && ar_record.ExistingAiWasEnabled)
                ar_record.RestoreAiOnDestroy = true;
            spdlog::debug("VR remote actor authority disabled AI for bridge handle {}", ar_record.Token.Value);
        }

        if (ar_record.IsPlayerSummon)
        {
            auto* process = actor->GetMiddleHighProcess();
            if (!process)
            {
                if (!ar_record.CommandingActorPending)
                {
                    spdlog::warn("VR player summon authority is waiting for middle-high process data for bridge handle {}", ar_record.Token.Value);
                    ar_record.CommandingActorPending = true;
                }
            }
            else
            {
                const auto commandingActor = actor->GetCommandingActor();
                if (!commandingActor || commandingActor.get() != player)
                {
                    if (!ar_record.OwnsActor && !ar_record.OriginalCommandingActorCaptured)
                    {
                        ar_record.OriginalCommandingActor = process->commandingActor;
                        ar_record.OriginalCommandingActorCaptured = true;
                    }
                    process->commandingActor = player->GetHandle();
                    if (!ar_record.OwnsActor)
                        ar_record.RestoreCommandingActorOnDestroy = true;
                }

                const auto verifiedCommandingActor = actor->GetCommandingActor();
                if (!verifiedCommandingActor || verifiedCommandingActor.get() != player)
                {
                    if (!ar_record.CommandingActorPending)
                    {
                        spdlog::warn("VR player summon authority could not bind the local player for bridge handle {}", ar_record.Token.Value);
                        ar_record.CommandingActorPending = true;
                    }
                }
                else
                {
                    if (ar_record.CommandingActorPending)
                        spdlog::info("VR player summon authority bound the local player for bridge handle {}", ar_record.Token.Value);
                    ar_record.CommandingActorPending = false;
                    ar_record.CommandingActorBound = true;
                }
            }
        }

        const auto* parentCell = actor->GetParentCell();
        const auto* worldspace = actor->GetWorldspace();
        const bool sameCell = parentCell && parentCell->GetFormID() == ar_record.LocalCellFormId &&
                              ((ar_record.LocalWorldspaceFormId == 0 && worldspace == nullptr) || (worldspace && worldspace->GetFormID() == ar_record.LocalWorldspaceFormId));
        if (!sameCell)
        {
            if (!ar_record.AuthorityCrossCellDeferred)
            {
                spdlog::warn("VR remote actor authority deferred root reconciliation across cells for bridge handle {}", ar_record.Token.Value);
                ar_record.AuthorityCrossCellDeferred = true;
            }
            return;
        }
        if (ar_record.AuthorityCrossCellDeferred)
        {
            spdlog::info("VR remote actor authority resumed same-cell root reconciliation for bridge handle {}", ar_record.Token.Value);
            ar_record.AuthorityCrossCellDeferred = false;
        }

        RootTransform normalized{};
        RE::NiPoint3 angles{};
        if (!NormalizeRoot(ar_record.Root, normalized, angles))
        {
            RecordAuthorityFailure(ar_record, "the retained network root became invalid");
            return;
        }
        if (!IsRootCurrent(*actor, normalized, angles))
        {
            // SetPosition(..., true) is the typed CommonLib route that updates
            // the actor's character controller together with the root. Do not
            // touch collision, Havok, ragdoll, or VR body-pose state here.
            ActorAuthorityHooks::ScopedAuthoritativeReplay authoritativeReplay;
            actor->SetPosition({normalized.PositionX, normalized.PositionY, normalized.PositionZ}, true);
            actor->SetAngle(angles);
            actor->SetScale(normalized.Scale);
        }

        if (ar_record.Authority.HasHealth && !ar_record.Authority.IsDead)
        {
            const auto health = actor->GetActorValue(RE::ActorValue::kHealth);
            if (!std::isfinite(health))
            {
                RecordAuthorityFailure(ar_record, "the remote actor returned a non-finite health value");
                return;
            }
            if (std::abs(health - ar_record.Authority.Health) > kAuthorityHealthTolerance)
                actor->SetActorValue(RE::ActorValue::kHealth, ar_record.Authority.Health);
        }

        ar_record.AuthorityFailureReported = false;
    }
    catch (...)
    {
        RecordAuthorityFailure(ar_record, "an exception occurred during owner-thread authority reconciliation");
    }
}

void AvatarManager::RecordAuthorityFailure(AvatarRecord& ar_record, const char* ap_reason) noexcept
{
    if (ar_record.AuthorityFailureReported)
        return;
    ar_record.AuthorityFailureReported = true;
    spdlog::warn("VR remote actor authority failure for bridge handle {}: {}", ar_record.Token.Value, ap_reason ? ap_reason : "unspecified failure");
}

void AvatarManager::RecordRemoteAvatarAiAdmissionFailure(const char* ap_reason) noexcept
{
    if (_remoteAvatarAiAdmissionFailureCount != std::numeric_limits<std::uint64_t>::max())
        ++_remoteAvatarAiAdmissionFailureCount;
    const auto total = _remoteAvatarAiAdmissionFailureCount;
    if (RemoteActorAdmissionPolicy::ShouldLogAiAdmissionFailure(total))
    {
        try
        {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: remote avatar AI admission rejected (aggregate={}): {}",
                total,
                ap_reason ? ap_reason : "unspecified failure");
        }
        catch (...)
        {
        }
    }

    if (!_remoteAvatarAiAdmissionEndpointFaulted)
    {
        // CommandExecutor also exposes this EngineRejected result through the
        // shared RejectedCommandCount diagnostic.
        _remoteAvatarAiAdmissionEndpointFaulted = true;
        BridgeEndpoint::Get().Fault("remote avatar AI admission failed closed");
    }
}

void AvatarManager::RecordRestorationFailure(AvatarRecord& ar_record, const char* ap_reason) noexcept
{
    if (ar_record.RestorationFailureReported)
        return;
    ar_record.RestorationFailureReported = true;
    try
    {
        spdlog::warn("VR remote actor restoration failure for bridge handle {}: {}", ar_record.Token.Value, ap_reason ? ap_reason : "unspecified failure");
    }
    catch (...)
    {
    }
}

bool AvatarManager::RestoreExistingRecordMutations(AvatarRecord& ar_record) noexcept
{
    try
    {
        if (ar_record.OwnsActor || (!ar_record.RestoreAiOnDestroy && !ar_record.RestoreCommandingActorOnDestroy))
            return true;
        if (!IsCommandPumpOwner())
        {
            RecordRestorationFailure(ar_record, "restoration was requested off the command-pump owner thread");
            return false;
        }

        const auto actor = ar_record.Actor.get();
        if (!actor)
        {
            RecordRestorationFailure(ar_record, "the existing remote actor no longer resolves");
            return false;
        }

        bool restored = true;
        if (ar_record.RestoreAiOnDestroy)
        {
            try
            {
                actor->EnableAI(ar_record.ExistingAiWasEnabled);
                if (actor->IsAIEnabled() != ar_record.ExistingAiWasEnabled)
                {
                    RecordRestorationFailure(ar_record, "the engine refused to restore existing remote actor AI");
                    restored = false;
                }
                else
                    ar_record.RestoreAiOnDestroy = false;
            }
            catch (...)
            {
                RecordRestorationFailure(ar_record, "an exception occurred while restoring existing remote actor AI");
                restored = false;
            }
        }

        if (ar_record.RestoreCommandingActorOnDestroy)
        {
            try
            {
                auto* process = actor->GetMiddleHighProcess();
                if (!process)
                {
                    RecordRestorationFailure(ar_record, "middle-high process data is unavailable while restoring the commanding actor");
                    restored = false;
                }
                else
                {
                    process->commandingActor = ar_record.OriginalCommandingActor;
                    ar_record.RestoreCommandingActorOnDestroy = false;
                }
            }
            catch (...)
            {
                RecordRestorationFailure(ar_record, "an exception occurred while restoring the commanding actor");
                restored = false;
            }
        }
        return restored;
    }
    catch (...)
    {
        RecordRestorationFailure(ar_record, "an exception occurred while restoring existing remote actor state");
        return false;
    }
}

void AvatarManager::RetireLocalNativeGameplayActor(std::unordered_map<AvatarKey, LocalNativeActorBinding, AvatarKeyHash>::iterator a_binding) noexcept
{
    if (a_binding == _localNativeActors.end())
        return;
    LocalGameplayCapture::StopNpcObservation(a_binding->second.LocalReferenceFormId);
    _localNativeActors.erase(a_binding);
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
    return {a_identity.ServerInstanceNonce, a_identity.ConnectionGeneration, a_identity.LifecycleEpoch, a_identity.EntityId, a_identity.EntityGeneration};
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
    return {a_status, a_record.Token, a_record.LocalCellFormId, a_record.LocalWorldspaceFormId, a_record.LocalActorReferenceFormId, a_record.Root};
}

bool AvatarManager::MoveActorToLocation(
    RE::Actor& a_actor, RE::TESObjectCELL& a_cell, RE::TESWorldSpace* a_worldspace, const RE::NiPoint3& a_position, const RE::NiPoint3& a_angles) noexcept
{
    try
    {
        using MoveTo = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
        static REL::Relocation<MoveTo> moveTo{RELOCATION_ID(56227, 56626)};
        if (moveTo.offset() != kMoveToVrRva || std::memcmp(reinterpret_cast<const void*>(moveTo.address()), kMoveToVrPrologue.data(), kMoveToVrPrologue.size()) != 0)
            return false;
        moveTo(&a_actor, RE::ObjectRefHandle{}, &a_cell, a_worldspace, a_position, a_angles);
        return a_actor.GetParentCell() == &a_cell && a_actor.GetWorldspace() == a_worldspace;
    }
    catch (...)
    {
        return false;
    }
}

bool AvatarManager::ApplyAnimationSnapshot(RE::Actor& a_actor, const AvatarRecord::PendingAnimationSnapshot& a_snapshot) noexcept
{
    AvatarRecord::PendingAnimationSnapshot previous{};
    AnimationGraphs::ResolvedDescriptor descriptor;
    if (!AnimationGraphs::CaptureForApply(a_actor, a_snapshot, previous) || !AnimationGraphs::Resolve(a_actor, descriptor) ||
        !AnimationGraphs::MatchesCounts(descriptor, a_snapshot))
        return false;
    const auto managerUnchanged = [&]() noexcept
    {
        return AnimationGraphs::ManagerMatches(a_actor, descriptor);
    };
    std::size_t booleansWritten{};
    std::size_t floatsWritten{};
    std::size_t integersWritten{};
    const auto rollback = [&]() noexcept
    {
        if (!managerUnchanged())
            return;
        for (std::size_t i = 0; i < booleansWritten; ++i)
            a_actor.SetGraphVariableBool(RE::BSFixedString(descriptor.Descriptor->Booleans[i].data()), previous.Booleans[i]);
        for (std::size_t i = 0; i < floatsWritten; ++i)
            a_actor.SetGraphVariableFloat(RE::BSFixedString(descriptor.Descriptor->Floats[i].data()), previous.Floats[i]);
        for (std::size_t i = 0; i < integersWritten; ++i)
            a_actor.SetGraphVariableInt(RE::BSFixedString(descriptor.Descriptor->Integers[i].data()), previous.Integers[i]);
    };

    for (; booleansWritten < a_snapshot.BooleanCount;)
    {
        if (!managerUnchanged())
        {
            rollback();
            return false;
        }
        const auto index = booleansWritten++;
        // A failed engine setter is conservatively treated as a possible
        // write, so rollback includes this value as well as earlier values.
        if (!a_actor.SetGraphVariableBool(RE::BSFixedString(descriptor.Descriptor->Booleans[index].data()), a_snapshot.Booleans[index]))
        {
            rollback();
            return false;
        }
    }
    for (; floatsWritten < a_snapshot.FloatCount;)
    {
        if (!managerUnchanged())
        {
            rollback();
            return false;
        }
        const auto index = floatsWritten++;
        if (!a_actor.SetGraphVariableFloat(RE::BSFixedString(descriptor.Descriptor->Floats[index].data()), a_snapshot.Floats[index]))
        {
            rollback();
            return false;
        }
    }
    for (; integersWritten < a_snapshot.IntegerCount;)
    {
        if (!managerUnchanged())
        {
            rollback();
            return false;
        }
        const auto index = integersWritten++;
        if (!a_actor.SetGraphVariableInt(RE::BSFixedString(descriptor.Descriptor->Integers[index].data()), a_snapshot.Integers[index]))
        {
            rollback();
            return false;
        }
    }
    if (!managerUnchanged() || !a_actor.SetGraphVariableFloat(
            RE::BSFixedString(descriptor.Descriptor->Floats[a_snapshot.DirectionFloatIndex].data()),
            a_snapshot.Direction) || !managerUnchanged())
    {
        rollback();
        return false;
    }
    return true;
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
    if (!ApplyAnimationSnapshot(*actor, a_record.PendingAnimation))
    {
        return PendingAnimationResult::Rejected;
    }

    a_record.LastAnimationSnapshot = a_record.PendingAnimation.SnapshotId;
    a_record.PendingAnimation = {};
    return PendingAnimationResult::Applied;
}

bool AvatarManager::DestroyRecord(AvatarRecord& a_record) noexcept
{
    const auto actor = a_record.Actor.get();
    auto retirement = ActorAuthorityHooks::BeginRetireManagedRemoteActor(actor.get());
    if (!retirement.IsQuiescent())
        return false;

    const auto restoredExistingState = RestoreExistingRecordMutations(a_record);
    if (!RemoteActorAdmissionPolicy::CanReleaseRetiredActorAfterRestoration(restoredExistingState))
    {
        RecordRemoteAvatarAiAdmissionFailure("failed to restore existing remote actor state during retirement");
        return false;
    }
    // Clear both bounded pose histories while this command-pump path still
    // owns the retired record and before actor/root references can be released
    // or allocator addresses reused by a new remote avatar.
    RemoteSolvedPosePresentation::GetFrameCache().Evict(a_record.Token.Value);
    if (a_record.OwnsActor)
    {
        if (actor)
        {
            actor->Disable();
            actor->SetDelete(true);
        }
    }
    if (a_record.VisualBase)
        a_record.VisualBase->SetDelete(true);
    const auto finish = ActorAuthorityHooks::FinishRetireManagedRemoteActor(retirement);
    if (finish != ActorAuthorityHooks::ManagedRemoteActorRetirementResult::Quiescent)
        return false;
    a_record.VisualBase = nullptr;
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter
