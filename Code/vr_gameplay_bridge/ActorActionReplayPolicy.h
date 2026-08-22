#pragma once

#include <vr_common/VRGameplayBridge.h>

namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
{
using CommandStatus = SkyrimTogetherVR::GameplayBridge::CommandStatus;

// Deliberately engine-free transaction policy for exact remote action replay.
// The callbacks let the runtime bind Skyrim state and graph operations while
// unit coverage can prove that rejected preflight never reaches a mutator and
// that every post-mutation failure restores the captured values.
struct ReplayTransactionCallbacks
{
    void* Context{};
    CommandStatus (*ApplyGraph)(void*){};
    CommandStatus (*RestoreGraph)(void*){};
    void (*ApplyActorState)(void*){};
    void (*RestoreActorState)(void*){};
    bool (*ForceAction)(void*){};
};

struct ReplayTransactionResult
{
    CommandStatus Status{CommandStatus::EngineRejected};
    CommandStatus GraphRestoreStatus{CommandStatus::Success};
    bool ForceActionInvoked{};
    bool RollbackAttempted{};
    bool ExceptionCaught{};
};

[[nodiscard]] inline ReplayTransactionResult RunReplayTransaction(
    const CommandStatus a_preflight,
    const ReplayTransactionCallbacks& a_callbacks) noexcept
{
    ReplayTransactionResult result{a_preflight};
    if (a_preflight != CommandStatus::Success)
        return result;
    if (!a_callbacks.Context || !a_callbacks.ApplyGraph || !a_callbacks.RestoreGraph || !a_callbacks.ApplyActorState ||
        !a_callbacks.RestoreActorState || !a_callbacks.ForceAction)
        return {};

    bool graphMutationMayHaveStarted{};
    bool actorStateMutationMayHaveStarted{};
    const auto rollback = [&]() noexcept
    {
        result.RollbackAttempted = graphMutationMayHaveStarted || actorStateMutationMayHaveStarted;
        if (graphMutationMayHaveStarted)
        {
            try
            {
                result.GraphRestoreStatus = a_callbacks.RestoreGraph(a_callbacks.Context);
            }
            catch (...)
            {
                result.GraphRestoreStatus = CommandStatus::EngineRejected;
            }
        }
        if (actorStateMutationMayHaveStarted)
        {
            try
            {
                a_callbacks.RestoreActorState(a_callbacks.Context);
            }
            catch (...)
            {
                // The game binding uses direct field restoration. Keep this
                // policy noexcept even if a future binding throws.
            }
        }
    };

    try
    {
        graphMutationMayHaveStarted = true;
        result.Status = a_callbacks.ApplyGraph(a_callbacks.Context);
        if (result.Status != CommandStatus::Success)
        {
            rollback();
            return result;
        }

        actorStateMutationMayHaveStarted = true;
        a_callbacks.ApplyActorState(a_callbacks.Context);
        result.ForceActionInvoked = true;
        if (!a_callbacks.ForceAction(a_callbacks.Context))
        {
            result.Status = CommandStatus::EngineRejected;
            rollback();
            return result;
        }

        result.Status = CommandStatus::Success;
        return result;
    }
    catch (...)
    {
        result.Status = CommandStatus::EngineRejected;
        result.ExceptionCaught = true;
        rollback();
        return result;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
