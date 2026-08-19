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

// SkyrimVR.esm is a mandatory master at runtime index 04. Matching its exact
// calibration cell avoids both mod-form collisions and transition-time
// worldspace probing.
inline constexpr uint32_t kVRPlayroomCellFormId = 0x040008D4;

struct VRLifecycleAdmissionProbe
{
    bool CalibrationOptionMenuOpen{};
    uint32_t CellFormId{};
};

enum class VRLifecycleAdmissionBlocker : uint8_t
{
    None,
    CalibrationOptionMenu,
    VRPlayroom,
};

[[nodiscard]] constexpr VRLifecycleAdmissionBlocker GetVRLifecycleAdmissionBlocker(const VRLifecycleAdmissionProbe& acProbe) noexcept
{
    if (acProbe.CalibrationOptionMenuOpen)
        return VRLifecycleAdmissionBlocker::CalibrationOptionMenu;
    if (acProbe.CellFormId == kVRPlayroomCellFormId)
        return VRLifecycleAdmissionBlocker::VRPlayroom;
    return VRLifecycleAdmissionBlocker::None;
}

[[nodiscard]] constexpr const char* GetVRLifecycleAdmissionBlockerReason(const VRLifecycleAdmissionBlocker aBlocker) noexcept
{
    switch (aBlocker)
    {
    case VRLifecycleAdmissionBlocker::CalibrationOptionMenu: return "calibration_option_menu";
    case VRLifecycleAdmissionBlocker::VRPlayroom: return "vr_playroom";
    case VRLifecycleAdmissionBlocker::None: return nullptr;
    }

    return nullptr;
}

[[nodiscard]] constexpr bool IsSameVRLifecycleIdentity(const VRLifecycleReadinessSample& acLeft, const VRLifecycleReadinessSample& acRight) noexcept
{
    return acLeft.Player == acRight.Player && acLeft.Base == acRight.Base && acLeft.PlayerFormId == acRight.PlayerFormId && acLeft.BaseFormId == acRight.BaseFormId;
}

[[nodiscard]] constexpr bool RequiresVRLifecycleRetirement(const VRLifecycleReadinessSample& acPrevious, const VRLifecycleReadinessSample& acCurrent) noexcept
{
    return !IsSameVRLifecycleIdentity(acPrevious, acCurrent);
}
