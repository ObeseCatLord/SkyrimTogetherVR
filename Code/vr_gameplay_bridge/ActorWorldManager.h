#pragma once

#include "AvatarManager.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
class ActorWorldManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
    static void ProcessPeriodic() noexcept;
    static void Reset() noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
