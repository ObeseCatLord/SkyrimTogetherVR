#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
// Executes the Combat, Projectile, and Magic subset of ApplyGameplayAction.
// The command pump owns common/session/capability validation; this entry point
// still validates the action payload before resolving or mutating an actor.
//
[[nodiscard]] CommandStatus ExecuteCombatMagicAction(const CommandRecord& a_command) noexcept;
[[nodiscard]] CommandStatus ApplyProjectileLaunch(const CommandRecord& a_command) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter
