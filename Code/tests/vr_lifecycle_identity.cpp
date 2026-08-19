#include <catch2/catch.hpp>

#include <Services/VRLifecycleIdentity.h>

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
