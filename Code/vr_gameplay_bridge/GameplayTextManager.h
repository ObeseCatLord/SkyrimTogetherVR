#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
class GameplayTextManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
