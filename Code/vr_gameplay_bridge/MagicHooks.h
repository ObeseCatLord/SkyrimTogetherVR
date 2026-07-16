#pragma once

namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
{
class ScopedRemoteMagicApplication final
{
public:
    ScopedRemoteMagicApplication() noexcept;
    ~ScopedRemoteMagicApplication() noexcept;

    ScopedRemoteMagicApplication(const ScopedRemoteMagicApplication&) = delete;
    ScopedRemoteMagicApplication& operator=(const ScopedRemoteMagicApplication&) = delete;
};

[[nodiscard]] bool Install() noexcept;
void Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::MagicHooks
