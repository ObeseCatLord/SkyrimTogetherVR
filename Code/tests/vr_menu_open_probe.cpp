#include <catch2/catch.hpp>

#include <Games/Skyrim/VR/VRMenuOpenProbe.h>

namespace
{
struct FakeMenu
{
    std::uint32_t Flags{};
};

struct FakeEntry
{
    struct
    {
        const char* data{};
    } key;
    struct
    {
        FakeMenu* spMenu{};
    } value;
    FakeEntry* next{};

    [[nodiscard]] bool empty() const noexcept { return next == nullptr; }
};

struct FakeMap
{
    std::uint32_t m_size{};
    std::uint32_t m_freeCount{};
    std::uint32_t m_freeOffset{};
    FakeEntry* m_entries{};
};

SkyrimTogetherVR::MenuOpenState Probe(FakeMap& aMap, const char* apName, const void* apUnreadable = nullptr)
{
    return SkyrimTogetherVR::ProbeVrMenuOpen(
        aMap, apName, sizeof(FakeMenu), 0x40, [apUnreadable](const void* apAddress, std::size_t) { return apAddress != apUnreadable; },
        [](const FakeMenu* apMenu) { return apMenu->Flags; });
}
} // namespace

TEST_CASE("VR menu probe distinguishes closed and open menus")
{
    static constexpr char kMainMenu[] = "Main Menu";
    FakeMenu menu{};
    FakeEntry entries[2]{};
    entries[0] = {{kMainMenu}, {&menu}, &entries[1]};
    FakeMap map{2, 1, 1, entries};

    REQUIRE(Probe(map, "Different interned name") == SkyrimTogetherVR::MenuOpenState::Closed);
    REQUIRE(Probe(map, kMainMenu) == SkyrimTogetherVR::MenuOpenState::Closed);

    menu.Flags = 0x40;
    REQUIRE(Probe(map, kMainMenu) == SkyrimTogetherVR::MenuOpenState::Open);
}

TEST_CASE("VR menu probe fails closed for invalid state")
{
    static constexpr char kMainMenu[] = "Main Menu";
    FakeMenu menu{0x40};
    FakeEntry entries[2]{};
    entries[0] = {{kMainMenu}, {&menu}, &entries[1]};
    FakeMap map{2, 1, 1, entries};

    REQUIRE(Probe(map, nullptr) == SkyrimTogetherVR::MenuOpenState::Unavailable);
    REQUIRE(Probe(map, kMainMenu, entries) == SkyrimTogetherVR::MenuOpenState::Unavailable);
    REQUIRE(Probe(map, kMainMenu, &menu) == SkyrimTogetherVR::MenuOpenState::Unavailable);

    map.m_size = 3;
    REQUIRE(Probe(map, kMainMenu) == SkyrimTogetherVR::MenuOpenState::Unavailable);
    map.m_size = 2;
    map.m_freeCount = 3;
    REQUIRE(Probe(map, kMainMenu) == SkyrimTogetherVR::MenuOpenState::Unavailable);
}

TEST_CASE("VR menu probe treats a registered null menu as closed")
{
    static constexpr char kMainMenu[] = "Main Menu";
    FakeEntry entries[2]{};
    entries[0] = {{kMainMenu}, {nullptr}, &entries[1]};
    FakeMap map{2, 1, 1, entries};

    REQUIRE(Probe(map, kMainMenu) == SkyrimTogetherVR::MenuOpenState::Closed);
}
