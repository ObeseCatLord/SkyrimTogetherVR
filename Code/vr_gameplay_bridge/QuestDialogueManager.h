#pragma once

#include "BridgeEndpoint.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
// Executes the Quest, Dialogue, Party, and teleport subset of
// ApplyGameplayAction. The command pump owns session/capability validation;
// this entry point validates every action field before resolving game forms.
class QuestDialogueManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
