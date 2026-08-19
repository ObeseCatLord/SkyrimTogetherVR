#include <catch2/catch.hpp>

#include <Services/VRLifecycleIdentity.h>

#include <string_view>

namespace
{
constexpr VRLifecycleReadinessSample MakeSample(
    const std::uintptr_t aPlayer, const std::uintptr_t aBase, const std::uintptr_t aCell, const uint32_t aPlayerFormId, const uint32_t aBaseFormId,
    const uint32_t aCellFormId) noexcept
{
    return {aPlayer, aBase, aCell, aPlayerFormId, aBaseFormId, aCellFormId};
}
} // namespace

TEST_CASE("VR lifecycle identity excludes parent-cell transitions", "[skyrim-vr][lifecycle]")
{
    constexpr auto initial = MakeSample(0x1000, 0x2000, 0x3000, 0x14, 0x7, 0x97EC);
    constexpr auto adjacentExterior = MakeSample(0x1000, 0x2000, 0x4000, 0x14, 0x7, 0x97CE);
    constexpr auto nextExterior = MakeSample(0x1000, 0x2000, 0x5000, 0x14, 0x7, 0x97ED);

    CHECK(IsSameVRLifecycleIdentity(initial, adjacentExterior));
    CHECK(IsSameVRLifecycleIdentity(adjacentExterior, nextExterior));
    CHECK_FALSE(RequiresVRLifecycleRetirement(initial, adjacentExterior));
    CHECK_FALSE(RequiresVRLifecycleRetirement(adjacentExterior, nextExterior));
}

TEST_CASE("VR lifecycle identity retires changed player or base", "[skyrim-vr][lifecycle]")
{
    constexpr auto initial = MakeSample(0x1000, 0x2000, 0x3000, 0x14, 0x7, 0x97EC);
    constexpr auto changedPlayer = MakeSample(0x1100, 0x2000, 0x3000, 0x15, 0x7, 0x97EC);
    constexpr auto changedBase = MakeSample(0x1000, 0x2100, 0x3000, 0x14, 0x8, 0x97EC);

    CHECK_FALSE(IsSameVRLifecycleIdentity(initial, changedPlayer));
    CHECK_FALSE(IsSameVRLifecycleIdentity(initial, changedBase));
    CHECK(RequiresVRLifecycleRetirement(initial, changedPlayer));
    CHECK(RequiresVRLifecycleRetirement(initial, changedBase));
}

TEST_CASE("VR lifecycle admission excludes the vanilla VR calibration playroom", "[skyrim-vr][lifecycle]")
{
    constexpr VRLifecycleAdmissionProbe settledSave{false, 0x000097EC};
    constexpr VRLifecycleAdmissionProbe calibrationMenu{true, 0x000097EC};
    constexpr VRLifecycleAdmissionProbe playroom{false, kVRPlayroomCellFormId};
    constexpr VRLifecycleAdmissionProbe otherFullPluginCollision{false, 0x070008D4};
    constexpr VRLifecycleAdmissionProbe lightPluginCollision{false, 0xFE0008D4};
    constexpr VRLifecycleAdmissionProbe temporaryFormCollision{false, 0xFF0008D4};
    constexpr VRLifecycleAdmissionProbe incompletePlayroomCell{false, 0};

    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(settledSave) == VRLifecycleAdmissionBlocker::None);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(calibrationMenu) == VRLifecycleAdmissionBlocker::CalibrationOptionMenu);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(playroom) == VRLifecycleAdmissionBlocker::VRPlayroom);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(otherFullPluginCollision) == VRLifecycleAdmissionBlocker::None);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(lightPluginCollision) == VRLifecycleAdmissionBlocker::None);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(temporaryFormCollision) == VRLifecycleAdmissionBlocker::None);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlocker(incompletePlayroomCell) == VRLifecycleAdmissionBlocker::None);
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlockerReason(VRLifecycleAdmissionBlocker::CalibrationOptionMenu) == std::string_view("calibration_option_menu"));
    STATIC_REQUIRE(GetVRLifecycleAdmissionBlockerReason(VRLifecycleAdmissionBlocker::VRPlayroom) == std::string_view("vr_playroom"));
}
