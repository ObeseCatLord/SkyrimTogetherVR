#pragma once

#include <RE/M/MagicTarget.h>

#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
{
enum class RemoteAddTargetResult : std::uint8_t
{
    Success,
    EngineRejected,
    PerkBonusUnsupported,
};

class ScopedRemoteMagicApplication final
{
public:
    ScopedRemoteMagicApplication() noexcept;
    ~ScopedRemoteMagicApplication() noexcept;

    ScopedRemoteMagicApplication(const ScopedRemoteMagicApplication&) = delete;
    ScopedRemoteMagicApplication& operator=(const ScopedRemoteMagicApplication&) = delete;
};

[[nodiscard]] bool HasValidRemoteAddTargetPerkBonusFlags(
    const RE::MagicItem& a_magicItem,
    bool a_applyHealPerkBonus,
    bool a_applyStaminaPerkBonus) noexcept;
[[nodiscard]] RemoteAddTargetResult ApplyRemoteAddTarget(
    RE::MagicTarget& a_target,
    RE::MagicTarget::AddTargetData& a_data,
    bool a_applyHealPerkBonus,
    bool a_applyStaminaPerkBonus) noexcept;

[[nodiscard]] bool Install() noexcept;
void Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
