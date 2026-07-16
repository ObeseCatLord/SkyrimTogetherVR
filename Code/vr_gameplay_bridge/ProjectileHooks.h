#pragma once

namespace SkyrimTogetherVR::GameplayAdapter::ProjectileHooks
{
class ScopedRemoteLaunch final
{
public:
    ScopedRemoteLaunch() noexcept;
    ~ScopedRemoteLaunch() noexcept;

    ScopedRemoteLaunch(const ScopedRemoteLaunch&) = delete;
    ScopedRemoteLaunch& operator=(const ScopedRemoteLaunch&) = delete;
};

[[nodiscard]] bool Install() noexcept;
void Uninstall() noexcept;
}
