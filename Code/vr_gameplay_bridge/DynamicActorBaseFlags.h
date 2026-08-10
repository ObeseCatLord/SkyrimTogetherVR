#pragma once

#include "pch.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
// Remote avatar bases are disposable dynamic forms.  Mutating their local
// flag storage avoids the desktop-only CommonLib notification relocation.
[[nodiscard]] inline bool SetReplicatedDynamicActorBaseFlag(RE::TESNPC* a_actorBase, const RE::ACTOR_BASE_DATA::Flag a_flag, const bool a_enabled) noexcept
{
    if (!a_actorBase || !a_actorBase->IsDynamicForm())
        return false;

    switch (a_flag)
    {
    case RE::ACTOR_BASE_DATA::Flag::kPCLevelMult:
    case RE::ACTOR_BASE_DATA::Flag::kEssential: break;
    default: return false;
    }

    a_actorBase->actorData.actorBaseFlags.set(a_enabled, a_flag);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter
