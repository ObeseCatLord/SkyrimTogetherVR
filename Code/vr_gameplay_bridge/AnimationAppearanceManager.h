#pragma once

#include "BridgeEndpoint.h"

#include <string_view>

namespace SkyrimTogetherVR::GameplayAdapter
{
class AnimationAppearanceManager final
{
public:
    // Applies only the Animation, Appearance, Equipment, and Inventory
    // GameplayAction payloads. The command-pump owner must call this after
    // common bridge/session validation.
    [[nodiscard]] static CommandStatus Apply(const CommandRecord& a_command) noexcept;
    [[nodiscard]] static CommandStatus StageText(
        const CommandRecord& a_command, std::string_view a_text) noexcept;
    static void ForgetTarget(AdapterHandle a_target, RE::TESNPC* a_npc = nullptr) noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
