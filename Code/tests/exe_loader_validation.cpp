#if defined(_WIN32)

#include <catch2/catch.hpp>

#include "../immersive_launcher/loader/ExeLoader.h"

#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace
{
constexpr uint32_t kLoadLimit = 0x10000;
constexpr uint32_t kImageBase = 0x40000000;
constexpr uint32_t kHeadersSize = 0x400;
constexpr uint32_t kTextRva = 0x1000;
constexpr uint32_t kRdataRva = 0x2000;
constexpr uint32_t kPdataRva = 0x3000;
constexpr uint32_t kTlsRva = 0x4000;

template <typename T> T* At(std::vector<uint8_t>& aBytes, size_t aOffset)
{
    return reinterpret_cast<T*>(aBytes.data() + aOffset);
}

void AddSection(IMAGE_SECTION_HEADER& aSection, const char* acName, uint32_t aRva, uint32_t aRawOffset, uint32_t aSize, DWORD aCharacteristics)
{
    std::memcpy(aSection.Name, acName, std::strlen(acName));
    aSection.Misc.VirtualSize = aSize;
    aSection.VirtualAddress = aRva;
    aSection.SizeOfRawData = aSize;
    aSection.PointerToRawData = aRawOffset;
    aSection.Characteristics = aCharacteristics;
}

std::vector<uint8_t> MakeValidPeFixture()
{
    std::vector<uint8_t> bytes(0x1000);

    auto* dos = At<IMAGE_DOS_HEADER>(bytes, 0);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    auto* nt = At<IMAGE_NT_HEADERS>(bytes, dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 4;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
    nt->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.ImageBase = kImageBase;
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.SizeOfImage = 0x5000;
    nt->OptionalHeader.SizeOfHeaders = kHeadersSize;
    nt->OptionalHeader.AddressOfEntryPoint = kTextRva;
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;

    auto* sections = IMAGE_FIRST_SECTION(nt);
    AddSection(sections[0], ".text", kTextRva, 0x400, 0x200, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
    AddSection(sections[1], ".rdata", kRdataRva, 0x600, 0x400, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);
    AddSection(sections[2], ".pdata", kPdataRva, 0xA00, 0x400, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ);
    AddSection(sections[3], ".tls", kTlsRva, 0xE00, 0x200, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);

    auto& importDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    importDirectory.VirtualAddress = kRdataRva;
    importDirectory.Size = sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2;
    auto* import = At<IMAGE_IMPORT_DESCRIPTOR>(bytes, 0x600);
    import->OriginalFirstThunk = kRdataRva + 0x80;
    import->Name = kRdataRva + 0x40;
    import->FirstThunk = kRdataRva + 0x90;
    std::memcpy(bytes.data() + 0x640, "KERNEL32.dll", sizeof("KERNEL32.dll"));
    *At<uintptr_t>(bytes, 0x680) = kRdataRva + 0xC0;
    *At<uintptr_t>(bytes, 0x688) = 0;
    *At<uintptr_t>(bytes, 0x690) = 0;
    *At<WORD>(bytes, 0x6C0) = 0;
    std::memcpy(bytes.data() + 0x6C2, "ExitProcess", sizeof("ExitProcess"));

    auto& exceptionDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    exceptionDirectory.VirtualAddress = kPdataRva;
    exceptionDirectory.Size = sizeof(RUNTIME_FUNCTION);
    auto* function = At<RUNTIME_FUNCTION>(bytes, 0xA00);
    function->BeginAddress = kTextRva;
    function->EndAddress = kTextRva + 0x10;
    function->UnwindData = kPdataRva + 0x20;
    *At<uint32_t>(bytes, 0xA20) = 1;

    auto& tlsDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    tlsDirectory.VirtualAddress = kTlsRva + 0x20;
    tlsDirectory.Size = sizeof(IMAGE_TLS_DIRECTORY);
    auto* tls = At<IMAGE_TLS_DIRECTORY>(bytes, 0xE20);
    tls->StartAddressOfRawData = static_cast<ULONGLONG>(kImageBase) + kTlsRva;
    tls->EndAddressOfRawData = static_cast<ULONGLONG>(kImageBase) + kTlsRva + 0x10;
    tls->AddressOfIndex = static_cast<ULONGLONG>(kImageBase) + kTlsRva + 0x10;

    return bytes;
}

void RequireBadImage(std::vector<uint8_t> aBytes)
{
    SetLastError(ERROR_SUCCESS);
    REQUIRE_FALSE(ExeLoader::ValidateImage(std::span<const uint8_t>(aBytes), kLoadLimit));
    REQUIRE(GetLastError() == ERROR_BAD_EXE_FORMAT);
}
} // namespace

void HookFormAllocateSentinelInit()
{
}

TEST_CASE("PE loader accepts its bounded valid fixture", "[exe-loader][pe]")
{
    const auto fixture = MakeValidPeFixture();
    REQUIRE(ExeLoader::ValidateImage(std::span<const uint8_t>(fixture), kLoadLimit));
}

TEST_CASE("PE loader rejects truncated headers and section raw spans", "[exe-loader][pe]")
{
    auto truncated = MakeValidPeFixture();
    truncated.resize(sizeof(IMAGE_DOS_HEADER));
    RequireBadImage(std::move(truncated));

    auto rawSpan = MakeValidPeFixture();
    auto* rawSections = IMAGE_FIRST_SECTION(At<IMAGE_NT_HEADERS>(rawSpan, 0x80));
    rawSections[0].PointerToRawData = static_cast<DWORD>(rawSpan.size() - 1);
    rawSections[0].SizeOfRawData = 2;
    RequireBadImage(std::move(rawSpan));

    auto unmappedImportData = MakeValidPeFixture();
    auto* importSections = IMAGE_FIRST_SECTION(At<IMAGE_NT_HEADERS>(unmappedImportData, 0x80));
    importSections[1].Misc.VirtualSize = 0xC0;
    RequireBadImage(std::move(unmappedImportData));
}

TEST_CASE("PE loader rejects every truncated NT header boundary", "[exe-loader][pe]")
{
    constexpr size_t kNtOffset = 0x80;
    constexpr size_t kNtPrefixSize = sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    const auto requireTruncatedHeader = [](size_t aSize)
    {
        auto truncated = MakeValidPeFixture();
        truncated.resize(aSize);
        RequireBadImage(std::move(truncated));
    };

    SECTION("signature") { requireTruncatedHeader(kNtOffset + sizeof(DWORD) - 1); }
    SECTION("file header") { requireTruncatedHeader(kNtOffset + kNtPrefixSize - 1); }
    SECTION("absent optional header") { requireTruncatedHeader(kNtOffset + kNtPrefixSize); }
    SECTION("partial optional header") { requireTruncatedHeader(kNtOffset + sizeof(IMAGE_NT_HEADERS) - 1); }
}

TEST_CASE("PE loader rejects overflowing virtual spans and entrypoints", "[exe-loader][pe]")
{
    auto virtualSpan = MakeValidPeFixture();
    auto* sections = IMAGE_FIRST_SECTION(At<IMAGE_NT_HEADERS>(virtualSpan, 0x80));
    sections[0].VirtualAddress = 0xFFFFF000;
    RequireBadImage(std::move(virtualSpan));

    auto entryPoint = MakeValidPeFixture();
    At<IMAGE_NT_HEADERS>(entryPoint, 0x80)->OptionalHeader.AddressOfEntryPoint = kRdataRva;
    RequireBadImage(std::move(entryPoint));
}

TEST_CASE("PE loader rejects malformed import, exception, and TLS ranges", "[exe-loader][pe]")
{
    auto importRange = MakeValidPeFixture();
    At<IMAGE_NT_HEADERS>(importRange, 0x80)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0x4FF0;
    RequireBadImage(std::move(importRange));

    auto exceptionRange = MakeValidPeFixture();
    At<RUNTIME_FUNCTION>(exceptionRange, 0xA00)->UnwindData = 0x4FFF;
    RequireBadImage(std::move(exceptionRange));

    auto tlsRange = MakeValidPeFixture();
    At<IMAGE_TLS_DIRECTORY>(tlsRange, 0xE20)->EndAddressOfRawData = static_cast<ULONGLONG>(kImageBase) + 0x5000;
    RequireBadImage(std::move(tlsRange));
}

#endif
