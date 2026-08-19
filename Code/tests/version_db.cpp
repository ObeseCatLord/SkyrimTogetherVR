#include <TiltedCore/Stl.hpp>

#include <VersionDb.h>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <string>
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

void WriteRequiredVrProjectFiles(const std::filesystem::path& aPluginPath, bool aOverrideForbiddenIds)
{
    std::string overrides = "id,offset,source,status,name\n"
                            "19796,2a8300,test,verified,TESObjectREFR::ActivateRef\n"
                            "34370,54f500,test,verified,InvisibilityEffect::Finish\n"
                            "38894,640a90,test,verified,ActorEquipManager::EquipObject\n"
                            "90000,990000,test,verified,OrdinaryProjectAddress\n";
    if (aOverrideForbiddenIds)
    {
        overrides += "34989,598b32,test,forbidden,SummonCreatureEffectFactory\n"
                     "37577,62c832,test,forbidden,Actor::IsFleeing\n"
                     "39643,6d96f0,test,forbidden,DialogueProcessResponse\n"
                     "40454,709972,test,forbidden,PlayerCharacterDropObject\n"
                     "42704,767a42,test,forbidden,ActorKnowledge::UpdateDetectionState\n";
    }

    WriteTextFile(aPluginPath / "SkyrimTogetherVR_AddressOverrides.csv", overrides.c_str());
    WriteTextFile(
        aPluginPath / "SkyrimTogetherVR_AE_to_SE.csv", "sseid,aeid\n"
                                                          "70004,34989\n"
                                                          "70001,37577\n"
                                                          "70002,39643\n"
                                                          "70005,40454\n"
                                                          "70003,42704\n");
}

void RequireVrForbiddenLegacyHookIdsAreUnresolvable(VersionDb& aDatabase, bool aGeneratedAliasesOwnTheFormerOffsets)
{
    constexpr unsigned long long kForbiddenIds[] = {34989, 37577, 39643, 40454, 42704};
    for (const auto id : kForbiddenIds)
    {
        unsigned long long value = 0;
        REQUIRE(aDatabase.GetOffsetMap().find(id) == aDatabase.GetOffsetMap().end());
        REQUIRE_FALSE(aDatabase.FindOffsetById(id, value));
        REQUIRE(aDatabase.FindAddressById(id) == nullptr);
    }

    unsigned long long id = 0;
    // The raw VR targets are no longer visible in the reverse map.
    REQUIRE_FALSE(aDatabase.FindIdByOffset(0x62C830, id));
    REQUIRE_FALSE(aDatabase.FindIdByOffset(0x598B30, id));
    REQUIRE_FALSE(aDatabase.FindIdByOffset(0x709970, id));
    REQUIRE_FALSE(aDatabase.FindIdByOffset(0x767A40, id));

    // 39643 was the reverse-map owner for this shared offset. Removing it
    // must restore the ordinary address rather than dropping the offset.
    REQUIRE(aDatabase.FindIdByOffset(0x6D96F0, id));
    REQUIRE(id == 70002);

    if (aGeneratedAliasesOwnTheFormerOffsets)
    {
        // The generated target IDs were removed and their source IDs were
        // restored as the reverse owners of the same offsets.
        REQUIRE(aDatabase.FindIdByOffset(0x62C831, id));
        REQUIRE(id == 70001);
        REQUIRE(aDatabase.FindIdByOffset(0x598B31, id));
        REQUIRE(id == 70004);
        REQUIRE(aDatabase.FindIdByOffset(0x709971, id));
        REQUIRE(id == 70005);
        REQUIRE(aDatabase.FindIdByOffset(0x767A41, id));
        REQUIRE(id == 70003);
    }
    else
    {
        // Explicit forbidden override rows have no surviving peer mapping.
        REQUIRE_FALSE(aDatabase.FindIdByOffset(0x62C832, id));
        REQUIRE_FALSE(aDatabase.FindIdByOffset(0x598B32, id));
        REQUIRE_FALSE(aDatabase.FindIdByOffset(0x709972, id));
        REQUIRE_FALSE(aDatabase.FindIdByOffset(0x767A42, id));
    }

    unsigned long long offset = 0;
    REQUIRE(aDatabase.FindOffsetById(70002, offset));
    REQUIRE(offset == 0x6D96F0);
    REQUIRE(aDatabase.FindOffsetById(19796, offset));
    REQUIRE(offset == 0x2A8300);
    REQUIRE(aDatabase.FindOffsetById(34370, offset));
    REQUIRE(offset == 0x54F500);
    REQUIRE(aDatabase.FindOffsetById(38894, offset));
    REQUIRE(offset == 0x640A90);
    REQUIRE(aDatabase.FindOffsetById(90000, offset));
    REQUIRE(offset == 0x990000);
}

void RequireDumpOmitsVrForbiddenLegacyHookIds(VersionDb& aDatabase, const std::filesystem::path& aPath)
{
    REQUIRE(aDatabase.DumpToTextFile(aPath.string()));

    std::ifstream dump(aPath);
    REQUIRE(dump.good());

    std::string line;
    while (std::getline(dump, line))
    {
        REQUIRE(line.rfind("34989\t", 0) != 0);
        REQUIRE(line.rfind("37577\t", 0) != 0);
        REQUIRE(line.rfind("39643\t", 0) != 0);
        REQUIRE(line.rfind("40454\t", 0) != 0);
        REQUIRE(line.rfind("42704\t", 0) != 0);
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
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AddressOverrides.csv", "id,offset,source,status,name\n"
                                                            "30000,300000,test,verified,Unrelated\n");

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

TEST_CASE("VR forbidden legacy-hook IDs are removed after CSV overlays and aliases", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";

    WriteTextFile(
        pluginPath / "version-1-4-15-0.csv", "id,offset\n"
                                             "70001,62c831\n"
                                             "70002,6d96f0\n"
                                             "70003,767a41\n"
                                             "70004,598b31\n"
                                             "70005,709971\n"
                                             "34989,598b30\n"
                                             "37577,62c830\n"
                                             "39643,6d96f0\n"
                                             "40454,709970\n"
                                             "42704,767a40\n"
                                             "91000,910000\n");
    WriteRequiredVrProjectFiles(pluginPath, false);

    VersionDb database;
    REQUIRE(database.Load(game.Path, 1, 4, 15, 0));

    RequireVrForbiddenLegacyHookIdsAreUnresolvable(database, true);
    RequireDumpOmitsVrForbiddenLegacyHookIds(database, game.Path / "csv-address-dump.txt");

    unsigned long long offset = 0;
    REQUIRE(database.FindOffsetById(91000, offset));
    REQUIRE(offset == 0x910000);
}

TEST_CASE("VR forbidden legacy-hook IDs are removed after binary project overrides", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";

    WriteBinaryAddressLibrary(
        pluginPath / "versionlib-1-4-15-0.bin",
        {{70001, 0x62C831}, {70002, 0x6D96F0}, {70003, 0x767A41}, {70004, 0x598B31}, {70005, 0x709971},
         {34989, 0x598B30}, {37577, 0x62C830}, {39643, 0x6D96F0}, {40454, 0x709970},
         {42704, 0x767A40}, {91000, 0x910000}});
    WriteRequiredVrProjectFiles(pluginPath, true);

    VersionDb database;
    REQUIRE(database.Load(game.Path, 1, 4, 15, 0));

    RequireVrForbiddenLegacyHookIdsAreUnresolvable(database, false);
    RequireDumpOmitsVrForbiddenLegacyHookIds(database, game.Path / "binary-address-dump.txt");

    unsigned long long offset = 0;
    REQUIRE(database.FindOffsetById(91000, offset));
    REQUIRE(offset == 0x910000);
}

TEST_CASE("VR explicit project overrides outrank generated aliases", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";

    WriteTextFile(
        pluginPath / "version-1-4-15-0.csv", "id,offset\n"
                                             "19362,2a7f00\n"
                                             "19789,2b7de0\n");
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AddressOverrides.csv", "id,offset,source,status,name\n"
                                                            "19789,3c0000,test,verified,ExplicitTarget\n");
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AE_to_SE.csv", "sseid,aeid\n"
                                                      "19362,19789\n");

    VersionDb database;
    REQUIRE(database.Load(game.Path, 1, 4, 15, 0));

    unsigned long long offset = 0;
    REQUIRE(database.FindOffsetById(19362, offset));
    REQUIRE(offset == 0x2A7F00);
    REQUIRE(database.FindOffsetById(19789, offset));
    REQUIRE(offset == 0x3C0000);
}

TEST_CASE("VR address loading rejects missing required project overlays", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";
    WriteTextFile(pluginPath / "version-1-4-15-0.csv", "id,offset\n35545,5b4290\n");

    VersionDb database;
    REQUIRE_FALSE(database.Load(game.Path, 1, 4, 15, 0));
    REQUIRE(database.GetOffsetMap().empty());
    REQUIRE(database.GetLastError().find("SkyrimTogetherVR_AddressOverrides.csv") != std::string::npos);
}

TEST_CASE("VR binary address loading rejects truncated entries", "[version-db][skyrim-vr]")
{
    TemporaryGameDirectory game;
    const auto pluginPath = game.Path / "Data" / "SKSE" / "Plugins";
    const auto binaryPath = pluginPath / "versionlib-1-4-15-0.bin";

    WriteBinaryAddressLibrary(binaryPath, {{35545, 0x5B4290}});
    std::filesystem::resize_file(binaryPath, std::filesystem::file_size(binaryPath) - 4);
    WriteTextFile(
        pluginPath / "SkyrimTogetherVR_AddressOverrides.csv", "id,offset,source,status,name\n"
                                                            "35545,5b4290,test,verified,WinMain\n");
    WriteTextFile(pluginPath / "SkyrimTogetherVR_AE_to_SE.csv", "sseid,aeid\n35545,35545\n");

    VersionDb database;
    REQUIRE_FALSE(database.Load(game.Path, 1, 4, 15, 0));
    REQUIRE(database.GetOffsetMap().empty());
    REQUIRE(database.GetLastError().find("Truncated") != std::string::npos);
}
