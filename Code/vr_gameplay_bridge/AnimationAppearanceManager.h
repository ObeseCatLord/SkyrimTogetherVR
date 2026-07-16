#pragma once

#include "AvatarManager.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
class AnimationAppearanceManager final
{
public:
    // Applies only the Animation, Appearance, Equipment, and Inventory
    // GameplayAction payloads. The command-pump owner must call this after
    // common bridge/session validation.
    [[nodiscard]] static CommandStatus Apply(const CommandRecord& a_command) noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
