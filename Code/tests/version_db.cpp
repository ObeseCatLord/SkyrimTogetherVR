#include <TiltedCore/Stl.hpp>

#include <VersionDb.h>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

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

template <class T> void WriteBinaryValue(std::ofstream& aFile, const T& acValue)
{
    aFile.write(reinterpret_cast<const char*>(&acValue), sizeof(acValue));
    REQUIRE(aFile.good());
}

void WriteBinaryAddressLibrary(
    const std::filesystem::path& aPath, const std::vector<std::pair<unsigned long long, unsigned long long>>& acEntries)
{
    std::ofstream file(aPath, std::ios::binary);
    REQUIRE(file.good());

    WriteBinaryValue(file, 2);
    for (const int version : {1, 4, 15, 0})
        WriteBinaryValue(file, version);

    constexpr char moduleName[] = "SkyrimVR.exe";
    const int moduleNameLength = static_cast<int>(sizeof(moduleName) - 1);
    WriteBinaryValue(file, moduleNameLength);
    file.write(moduleName, moduleNameLength);
    REQUIRE(file.good());

    WriteBinaryValue(file, 8);
    WriteBinaryValue(file, static_cast<int>(acEntries.size()));
    for (const auto& [id, offset] : acEntries)
    {
        WriteBinaryValue(file, static_cast<unsigned char>(0));
        WriteBinaryValue(file, id);
        WriteBinaryValue(file, offset);
    }
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

TEST_CASE("VR project overlays apply after binary address libraries", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";

    WriteBinaryAddressLibrary(
        pluginPath / "versionlib-1-4-15-0.bin", {{514178, 0x111000}, {400327, 0x222000}, {30000, 0x300000}});
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AddressOverrides.csv", "id,offset,source,status,name\n"
                                                            "514178,1f83200,database,verified,UI::Singleton\n");
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AE_to_SE.csv", "sseid,aeid\n"
                                                      "514178,400327\n");

    VersionDb database;
    REQUIRE(database.Load(game.Path, 1, 4, 15, 0));

    unsigned long long offset = 0;
    REQUIRE(database.FindOffsetById(514178, offset));
    REQUIRE(offset == 0x1F83200);
    REQUIRE(database.FindOffsetById(400327, offset));
    REQUIRE(offset == 0x1F83200);
    REQUIRE(database.FindOffsetById(30000, offset));
    REQUIRE(offset == 0x300000);

    unsigned long long id = 0;
    REQUIRE_FALSE(database.FindIdByOffset(0x111000, id));
    REQUIRE_FALSE(database.FindIdByOffset(0x222000, id));
    REQUIRE(database.FindIdByOffset(0x1F83200, id));
}
