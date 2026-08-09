#pragma once

#include "pch.h"

namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
{
enum class RemoteAddTargetResult : std::uint8_t
{
    Success,
    EngineRejected,
};

class ScopedRemoteMagicApplication final
{
public:
    ScopedRemoteMagicApplication() noexcept;
    ~ScopedRemoteMagicApplication() noexcept;

    ScopedRemoteMagicApplication(const ScopedRemoteMagicApplication&) = delete;
    ScopedRemoteMagicApplication& operator=(const ScopedRemoteMagicApplication&) = delete;
};

[[nodiscard]] RemoteAddTargetResult ApplyRemoteAddTarget(
    RE::MagicTarget& a_target,
    RE::MagicTarget::AddTargetData& a_data,
    bool a_applyHealPerkBonus,
    bool a_applyStaminaPerkBonus) noexcept;

[[nodiscard]] bool Install() noexcept;
void Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
