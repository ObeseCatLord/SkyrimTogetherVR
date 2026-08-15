#pragma once

#include "../vr_common/VRGameplayBridge.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
using namespace SkyrimTogetherVR::GameplayBridge;

// Executes the Dialogue, Party, and teleport subset of ApplyGameplayAction.
// Quest mutation is deliberately disabled until stage completion can be
// observed synchronously with sound suppression and ordering semantics.
class QuestDialogueManager final
{
public:
    [[nodiscard]] static constexpr CommandStatus QuestSynchronizationStatus() noexcept
    {
        return CommandStatus::Unsupported;
    }

    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
