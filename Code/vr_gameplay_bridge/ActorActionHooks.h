#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
{
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
void Reset() noexcept;
[[nodiscard]] CommandStatus Execute(const CommandRecord& a_command) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
