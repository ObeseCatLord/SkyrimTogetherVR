#pragma once

#include <cstdint>

// Parent cells gate readiness and diagnostics, but do not define a player lifecycle.
struct VRLifecycleReadinessSample
{
    std::uintptr_t Player{};
    std::uintptr_t Base{};
    std::uintptr_t Cell{};
    uint32_t PlayerFormId{};
    uint32_t BaseFormId{};
    uint32_t CellFormId{};
};

[[nodiscard]] constexpr bool IsSameVRLifecycleIdentity(const VRLifecycleReadinessSample& acLeft, const VRLifecycleReadinessSample& acRight) noexcept
{
    return acLeft.Player == acRight.Player && acLeft.Base == acRight.Base && acLeft.PlayerFormId == acRight.PlayerFormId && acLeft.BaseFormId == acRight.BaseFormId;
}

[[nodiscard]] constexpr bool RequiresVRLifecycleRetirement(const VRLifecycleReadinessSample& acPrevious, const VRLifecycleReadinessSample& acCurrent) noexcept
{
    return !IsSameVRLifecycleIdentity(acPrevious, acCurrent);
}
