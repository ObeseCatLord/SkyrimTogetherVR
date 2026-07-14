#include <TiltedCore/Stl.hpp>

#include <VersionDb.h>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>

namespace
{
struct TemporaryGameDirectory
{
    TemporaryGameDirectory()
        : Path(std::filesystem::temp_directory_path() / ("stvr-version-db-" + std::to_string(GetCurrentProcessId())))
    {
        std::filesystem::remove_all(Path);
        std::filesystem::create_directories(Path / "Data" / "SKSE" / "Plugins");
    }

    ~TemporaryGameDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(Path, error);
    }

    std::filesystem::path Path;
};

void WriteTextFile(const std::filesystem::path& aPath, const char* apContents)
{
    std::ofstream file(aPath, std::ios::binary);
    REQUIRE(file.good());
    file << apContents;
    REQUIRE(file.good());
}
} // namespace

TEST_CASE("VR address aliases override colliding raw IDs", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";

    WriteTextFile(
        pluginPath / "version-1-4-15-0.csv", "id,offset\n"
                                             "19362,2a7f00\n"
                                             "19789,2b7de0\n"
                                             "30000,300000\n");
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AE_to_SE.csv", "sseid,aeid\n"
                                                      "19362,19789\n");

    VersionDb database;
    REQUIRE(database.Load(game.Path, 1, 4, 15, 0));

    unsigned long long offset = 0;
    REQUIRE(database.FindOffsetById(19362, offset));
    REQUIRE(offset == 0x2A7F00);
    REQUIRE(database.FindOffsetById(19789, offset));
    REQUIRE(offset == 0x2A7F00);
    REQUIRE(database.FindOffsetById(30000, offset));
    REQUIRE(offset == 0x300000);

    unsigned long long id = 0;
    REQUIRE_FALSE(database.FindIdByOffset(0x2B7DE0, id));
    REQUIRE(database.FindIdByOffset(0x2A7F00, id));
    REQUIRE(database.FindOffsetById(id, offset));
    REQUIRE(offset == 0x2A7F00);
}
