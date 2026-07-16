#pragma once

namespace SkyrimTogetherVR::GameplayAdapter
{
// Register the CommonLib Papyrus callback after SKSE::Init succeeds. The callback
// provides the VR-safe connection, chat, player-list, and party command surface.
[[nodiscard]] bool RegisterPapyrusBindings() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter
