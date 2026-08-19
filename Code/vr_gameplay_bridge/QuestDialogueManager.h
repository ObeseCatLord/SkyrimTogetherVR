#pragma once

#include "../vr_common/VRGameplayBridge.h"

namespace SkyrimTogetherVR::GameplayAdapter
{
using namespace SkyrimTogetherVR::GameplayBridge;

// Executes the Quest, Dialogue, Party, and teleport subset of
// ApplyGameplayAction. Quest mutation uses the native synchronous stage path;
// the bridge dispatcher retains the capability gate that decides when it may
// be reached.
class QuestDialogueManager final
{
public:
    [[nodiscard]] static CommandStatus Execute(const CommandRecord& a_command) noexcept;
};
} // namespace SkyrimTogetherVR::GameplayAdapter
