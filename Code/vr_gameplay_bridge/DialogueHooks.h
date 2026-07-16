#pragma once

#include <cstdint>

namespace RE
{
class Actor;
}

namespace SkyrimTogetherVR::GameplayAdapter::DialogueHooks
{
[[nodiscard]] bool Install() noexcept;
void Uninstall() noexcept;

[[nodiscard]] bool PlayRemoteVoice(RE::Actor& a_actor, const char* a_resourcePath) noexcept;
}
