/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

// Changes:
// - 2021/2/24: Moved TLS routine.
// - 2021/2/25: Implemented CEG decryption method.

#include <winternl.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "ExeLoader.h"
#include "steam/SteamCeg.h"
#include "utils/NtInternal.h"

size_t GetMappedTlsSlotCapacity() noexcept;

namespace
{
const uint8_t* g_mappedTlsTemplate = nullptr;
size_t g_mappedTlsTemplateSize = 0;

constexpr size_t kMaximumImportEntries = 1U << 20;
constexpr size_t kMaximumStringLength = 32768;
constexpr size_t kAesBlockSize = 16;

bool BadImage() noexcept
{
    SetLastError(ERROR_BAD_EXE_FORMAT);
    return false;
}

bool RangeFits(size_t aOffset, size_t aLength, size_t aLimit) noexcept
{
    return aOffset <= aLimit && aLength <= aLimit - aOffset;
}

bool IsTextSection(const IMAGE_SECTION_HEADER& acSection) noexcept
{
    static constexpr char kTextName[] = ".text";
    return std::memcmp(acSection.Name, kTextName, sizeof(kTextName) - 1) == 0;
}

class ImageView
{
public:
    ImageView(std::span<const uint8_t> aBytes, const IMAGE_NT_HEADERS* apNtHeader, const IMAGE_SECTION_HEADER* apSections, uint16_t aSectionCount, uint32_t aLoadLimit) noexcept
        : m_bytes(aBytes)
        , m_ntHeader(apNtHeader)
        , m_sections(apSections)
        , m_sectionCount(aSectionCount)
        , m_loadLimit(aLoadLimit)
    {
    }

    bool IsVirtualRange(uint32_t aRva, size_t aLength) const noexcept
    {
        return RangeFits(aRva, aLength, m_ntHeader->OptionalHeader.SizeOfImage) && RangeFits(aRva, aLength, m_loadLimit);
    }

    bool RvaToOffset(uint32_t aRva, size_t aLength, size_t& aOffset) const noexcept { return RvaToOffset(aRva, aLength, aOffset, false); }

    bool RvaToMappedOffset(uint32_t aRva, size_t aLength, size_t& aOffset) const noexcept { return RvaToOffset(aRva, aLength, aOffset, true); }

    template <typename T> const T* GetRva(uint32_t aRva) const noexcept
    {
        size_t offset = 0;
        if (!RvaToOffset(aRva, sizeof(T), offset))
            return nullptr;

        return reinterpret_cast<const T*>(m_bytes.data() + offset);
    }

    template <typename T> const T* GetMappedRva(uint32_t aRva) const noexcept
    {
        size_t offset = 0;
        if (!RvaToMappedOffset(aRva, sizeof(T), offset))
            return nullptr;

        return reinterpret_cast<const T*>(m_bytes.data() + offset);
    }

    bool HasMappedCString(uint32_t aRva) const noexcept
    {
        if (!aRva || aRva >= m_ntHeader->OptionalHeader.SizeOfImage)
            return false;

        const auto remaining = static_cast<size_t>(m_ntHeader->OptionalHeader.SizeOfImage - aRva);
        const auto maximumLength = std::min(remaining, kMaximumStringLength);
        for (size_t length = 0; length < maximumLength; ++length)
        {
            size_t offset = 0;
            if (!RvaToMappedOffset(aRva + static_cast<uint32_t>(length), 1, offset))
                return false;

            if (m_bytes[offset] == '\0')
                return length != 0;
        }

        return false;
    }

private:
    bool RvaToOffset(uint32_t aRva, size_t aLength, size_t& aOffset, bool aRequireMapped) const noexcept
    {
        if (!IsVirtualRange(aRva, aLength))
            return false;

        const auto headersSize = static_cast<size_t>(m_ntHeader->OptionalHeader.SizeOfHeaders);
        if (aRva < headersSize)
        {
            if (aRequireMapped || !RangeFits(aRva, aLength, headersSize) || !RangeFits(aRva, aLength, m_bytes.size()))
                return false;

            aOffset = aRva;
            return true;
        }

        for (uint16_t index = 0; index < m_sectionCount; ++index)
        {
            const auto& section = m_sections[index];
            const auto virtualSize = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if (aRva < section.VirtualAddress)
                continue;

            const auto relativeRva = static_cast<size_t>(aRva - section.VirtualAddress);
            if (relativeRva > virtualSize || aLength > static_cast<size_t>(virtualSize) - relativeRva || relativeRva > section.SizeOfRawData ||
                aLength > static_cast<size_t>(section.SizeOfRawData) - relativeRva)
            {
                continue;
            }

            const auto mappedSize = std::min(section.Misc.VirtualSize, section.SizeOfRawData);
            if (aRequireMapped && (relativeRva > mappedSize || aLength > static_cast<size_t>(mappedSize) - relativeRva))
            {
                continue;
            }

            const auto rawOffset = static_cast<size_t>(section.PointerToRawData) + relativeRva;
            if (!RangeFits(rawOffset, aLength, m_bytes.size()))
                return false;

            aOffset = rawOffset;
            return true;
        }

        return false;
    }

    std::span<const uint8_t> m_bytes;
    const IMAGE_NT_HEADERS* m_ntHeader;
    const IMAGE_SECTION_HEADER* m_sections;
    uint16_t m_sectionCount;
    uint32_t m_loadLimit;
};

bool ValidateImportThunkTable(const ImageView& acImage, uint32_t aLookupRva, uint32_t aAddressRva) noexcept
{
    if (!aLookupRva || !aAddressRva)
        return false;

    const auto maximumEntries = std::min(kMaximumImportEntries, static_cast<size_t>(std::numeric_limits<uint32_t>::max() - aLookupRva) / sizeof(uintptr_t));
    for (size_t index = 0; index < maximumEntries; ++index)
    {
        const auto offsetWithinTable = index * sizeof(uintptr_t);
        const auto lookupRva = aLookupRva + static_cast<uint32_t>(offsetWithinTable);
        const auto* thunk = acImage.GetMappedRva<uintptr_t>(lookupRva);
        if (!thunk || !acImage.IsVirtualRange(aAddressRva, offsetWithinTable + sizeof(uintptr_t)))
            return false;

        const auto value = *thunk;
        if (!value)
            return true;

        if (IMAGE_SNAP_BY_ORDINAL(value))
            continue;

        if (value > std::numeric_limits<uint32_t>::max())
            return false;

        const auto nameRva = static_cast<uint32_t>(value);
        if (!acImage.GetMappedRva<WORD>(nameRva) || nameRva > std::numeric_limits<uint32_t>::max() - offsetof(IMAGE_IMPORT_BY_NAME, Name) ||
            !acImage.HasMappedCString(nameRva + offsetof(IMAGE_IMPORT_BY_NAME, Name)))
        {
            return false;
        }
    }

    return false;
}

bool ValidateImportDirectory(const ImageView& acImage, const IMAGE_DATA_DIRECTORY& acDirectory) noexcept
{
    size_t importDirectoryOffset = 0;
    if (!acDirectory.VirtualAddress || !acDirectory.Size || acDirectory.Size % sizeof(IMAGE_IMPORT_DESCRIPTOR) != 0 ||
        !acImage.GetMappedRva<IMAGE_IMPORT_DESCRIPTOR>(acDirectory.VirtualAddress) ||
        !acImage.RvaToMappedOffset(acDirectory.VirtualAddress, acDirectory.Size, importDirectoryOffset))
    {
        return false;
    }

    const auto descriptorCount = acDirectory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    for (uint32_t index = 0; index < descriptorCount; ++index)
    {
        const auto descriptorRva = acDirectory.VirtualAddress + index * static_cast<uint32_t>(sizeof(IMAGE_IMPORT_DESCRIPTOR));
        const auto* descriptor = acImage.GetMappedRva<IMAGE_IMPORT_DESCRIPTOR>(descriptorRva);
        if (!descriptor)
            return false;

        if (!descriptor->Name)
        {
            return descriptor->OriginalFirstThunk == 0 && descriptor->TimeDateStamp == 0 && descriptor->ForwarderChain == 0 && descriptor->FirstThunk == 0;
        }

        if (!acImage.HasMappedCString(descriptor->Name) || !descriptor->FirstThunk ||
            !ValidateImportThunkTable(acImage, descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk, descriptor->FirstThunk))
        {
            return false;
        }
    }

    return false;
}

bool ValidateExceptionDirectory(const ImageView& acImage, const IMAGE_DATA_DIRECTORY& acDirectory) noexcept
{
    if (!acDirectory.VirtualAddress || !acDirectory.Size || acDirectory.Size % sizeof(RUNTIME_FUNCTION) != 0 ||
        !acImage.GetMappedRva<RUNTIME_FUNCTION>(acDirectory.VirtualAddress) || !acImage.IsVirtualRange(acDirectory.VirtualAddress, acDirectory.Size))
    {
        return false;
    }

    const auto entryCount = acDirectory.Size / sizeof(RUNTIME_FUNCTION);
    uint32_t previousBeginAddress = 0;
    for (uint32_t index = 0; index < entryCount; ++index)
    {
        const auto functionRva = acDirectory.VirtualAddress + index * static_cast<uint32_t>(sizeof(RUNTIME_FUNCTION));
        const auto* function = acImage.GetMappedRva<RUNTIME_FUNCTION>(functionRva);
        if (!function || function->BeginAddress >= function->EndAddress ||
            !acImage.IsVirtualRange(function->BeginAddress, static_cast<size_t>(function->EndAddress - function->BeginAddress)) || !function->UnwindData ||
            !acImage.GetMappedRva<uint32_t>(function->UnwindData) || (index > 0 && function->BeginAddress < previousBeginAddress))
        {
            return false;
        }

        previousBeginAddress = function->BeginAddress;
    }

    return true;
}

bool AddressToRva(const IMAGE_OPTIONAL_HEADER& acHeader, uint64_t aAddress, uint32_t& aRva) noexcept
{
    if (aAddress < acHeader.ImageBase)
        return false;

    const auto offset = aAddress - acHeader.ImageBase;
    if (offset > std::numeric_limits<uint32_t>::max())
        return false;

    aRva = static_cast<uint32_t>(offset);
    return true;
}

bool ValidateTlsCallbacks(const ImageView& acImage, const IMAGE_OPTIONAL_HEADER& acHeader, uint64_t aCallbacksAddress) noexcept
{
    if (!aCallbacksAddress)
        return true;

    uint32_t callbacksRva = 0;
    if (!AddressToRva(acHeader, aCallbacksAddress, callbacksRva))
        return false;

    const auto maximumCallbacks = std::min(kMaximumImportEntries, static_cast<size_t>(std::numeric_limits<uint32_t>::max() - callbacksRva) / sizeof(uintptr_t));
    for (size_t index = 0; index < maximumCallbacks; ++index)
    {
        const auto callbackRva = callbacksRva + static_cast<uint32_t>(index * sizeof(uintptr_t));
        const auto* callback = acImage.GetMappedRva<uintptr_t>(callbackRva);
        if (!callback)
            return false;

        if (!*callback)
            return true;

        uint32_t targetRva = 0;
        if (!AddressToRva(acHeader, *callback, targetRva) || !acImage.IsVirtualRange(targetRva, 1))
            return false;
    }

    return false;
}

bool ValidateTlsDirectory(const ImageView& acImage, const IMAGE_OPTIONAL_HEADER& acHeader, const IMAGE_DATA_DIRECTORY& acDirectory) noexcept
{
    if (!acDirectory.VirtualAddress && !acDirectory.Size)
        return true;

    if (!acDirectory.VirtualAddress || acDirectory.Size < sizeof(IMAGE_TLS_DIRECTORY) || !acImage.GetMappedRva<IMAGE_TLS_DIRECTORY>(acDirectory.VirtualAddress) ||
        !acImage.IsVirtualRange(acDirectory.VirtualAddress, acDirectory.Size))
    {
        return false;
    }

    size_t tlsDirectoryOffset = 0;
    const auto* tls = acImage.GetMappedRva<IMAGE_TLS_DIRECTORY>(acDirectory.VirtualAddress);
    uint32_t startRva = 0;
    uint32_t endRva = 0;
    uint32_t indexRva = 0;
    if (!tls || tls->EndAddressOfRawData <= tls->StartAddressOfRawData || !AddressToRva(acHeader, tls->StartAddressOfRawData, startRva) ||
        !AddressToRva(acHeader, tls->EndAddressOfRawData, endRva) || !AddressToRva(acHeader, tls->AddressOfIndex, indexRva) || endRva <= startRva ||
        !acImage.RvaToMappedOffset(acDirectory.VirtualAddress, acDirectory.Size, tlsDirectoryOffset) ||
        !acImage.RvaToMappedOffset(startRva, static_cast<size_t>(endRva - startRva), tlsDirectoryOffset) ||
        !acImage.IsVirtualRange(startRva, static_cast<size_t>(endRva - startRva)) || !acImage.IsVirtualRange(indexRva, sizeof(DWORD)) ||
        tls->SizeOfZeroFill > std::numeric_limits<uint32_t>::max() - (endRva - startRva) || !ValidateTlsCallbacks(acImage, acHeader, tls->AddressOfCallBacks))
    {
        return false;
    }

    return true;
}
} // namespace

#if defined(_M_AMD64)
typedef enum _FUNCTION_TABLE_TYPE
{
    RF_SORTED,
    RF_UNSORTED,
    RF_CALLBACK
} FUNCTION_TABLE_TYPE;

typedef struct _DYNAMIC_FUNCTION_TABLE
{
    LIST_ENTRY Links;
    PRUNTIME_FUNCTION FunctionTable;
    LARGE_INTEGER TimeStamp;

    ULONG_PTR MinimumAddress;
    ULONG_PTR MaximumAddress;
    ULONG_PTR BaseAddress;

    PGET_RUNTIME_FUNCTION_CALLBACK Callback;
    PVOID Context;
    PWSTR OutOfProcessCallbackDll;
    FUNCTION_TABLE_TYPE Type;
    ULONG EntryCount;
} DYNAMIC_FUNCTION_TABLE, *PDYNAMIC_FUNCTION_TABLE;
#endif

// TODO: move me to wstring util..
std::wstring ConvertStringToWstring(const std::string_view str)
{
    int nChars = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, str.data(), static_cast<int>(str.length()), NULL, 0);

    std::wstring wstrTo;
    if (nChars)
    {
        wstrTo.resize(nChars);
        if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, str.data(), static_cast<int>(str.length()), &wstrTo[0], nChars))
        {
            return wstrTo;
        }
    }
    return {};
}

ExeLoader::ExeLoader(uint32_t aLoadLimit, TFuncHandler aFuncHandler)
    : m_loadLimit(aLoadLimit)
    , m_pFuncHandler(aFuncHandler)
{
}

bool ExeLoader::ValidateImage(std::span<const uint8_t> aProgramBuffer, uint32_t aLoadLimit) noexcept
{
    if (aProgramBuffer.size() > std::numeric_limits<uint32_t>::max() || aLoadLimit == 0 || !RangeFits(0, sizeof(IMAGE_DOS_HEADER), aProgramBuffer.size()))
    {
        return BadImage();
    }

    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(aProgramBuffer.data());
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)))
        return BadImage();

    const auto ntOffset = static_cast<size_t>(dosHeader->e_lfanew);
    if (!RangeFits(ntOffset, sizeof(IMAGE_NT_HEADERS), aProgramBuffer.size()))
        return BadImage();

    const auto* ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(aProgramBuffer.data() + ntOffset);
    if (ntHeader->Signature != IMAGE_NT_SIGNATURE || ntHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !(ntHeader->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) || !ntHeader->FileHeader.NumberOfSections ||
        ntHeader->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER) || ntHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        ntHeader->OptionalHeader.NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
        ntHeader->OptionalHeader.SectionAlignment == 0 || ntHeader->OptionalHeader.FileAlignment == 0 || ntHeader->OptionalHeader.SizeOfImage == 0 ||
        ntHeader->OptionalHeader.SizeOfImage > aLoadLimit || ntHeader->OptionalHeader.SizeOfHeaders < sizeof(IMAGE_DOS_HEADER) ||
        ntHeader->OptionalHeader.SizeOfHeaders > ntHeader->OptionalHeader.SizeOfImage || ntHeader->OptionalHeader.SizeOfHeaders > aProgramBuffer.size())
    {
        return BadImage();
    }

    const auto sectionTableOffset = ntOffset + sizeof(IMAGE_NT_HEADERS);
    const auto sectionTableSize = static_cast<size_t>(ntHeader->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!RangeFits(sectionTableOffset, sectionTableSize, aProgramBuffer.size()) || !RangeFits(sectionTableOffset, sectionTableSize, ntHeader->OptionalHeader.SizeOfHeaders))
    {
        return BadImage();
    }

    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(aProgramBuffer.data() + sectionTableOffset);
    ImageView image(aProgramBuffer, ntHeader, sections, ntHeader->FileHeader.NumberOfSections, aLoadLimit);
    bool entryPointInExecutableSection = false;
    for (uint16_t index = 0; index < ntHeader->FileHeader.NumberOfSections; ++index)
    {
        const auto& section = sections[index];
        const auto virtualSize = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
        if (virtualSize == 0 || !image.IsVirtualRange(section.VirtualAddress, virtualSize) ||
            (section.SizeOfRawData != 0 &&
             (section.PointerToRawData < ntHeader->OptionalHeader.SizeOfHeaders || !RangeFits(section.PointerToRawData, section.SizeOfRawData, aProgramBuffer.size()))))
        {
            return BadImage();
        }

        if (ntHeader->OptionalHeader.AddressOfEntryPoint >= section.VirtualAddress)
        {
            const auto entryOffset = static_cast<size_t>(ntHeader->OptionalHeader.AddressOfEntryPoint - section.VirtualAddress);
            if (entryOffset < virtualSize && (section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
                entryPointInExecutableSection = true;
        }
    }

    if (!ntHeader->OptionalHeader.AddressOfEntryPoint || !image.GetMappedRva<uint8_t>(ntHeader->OptionalHeader.AddressOfEntryPoint) || !entryPointInExecutableSection ||
        !ValidateImportDirectory(image, ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]) ||
        !ValidateExceptionDirectory(image, ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]) ||
        !ValidateTlsDirectory(image, ntHeader->OptionalHeader, ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS]))
    {
        return BadImage();
    }

    return true;
}

bool ExeLoader::LoadImports(const IMAGE_NT_HEADERS* apNtHeader)
{
    if (!m_pFuncHandler)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    const auto* importDirectory = &apNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto* descriptor = GetTargetRVA<const IMAGE_IMPORT_DESCRIPTOR>(importDirectory->VirtualAddress);
    const auto descriptorCount = importDirectory->Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);

    for (DWORD descriptorIndex = 0; descriptorIndex < descriptorCount && descriptor->Name; ++descriptorIndex, ++descriptor)
    {
        auto dllName = ConvertStringToWstring(GetTargetRVA<char>(descriptor->Name));

        HMODULE hMod = LoadLibraryW(dllName.c_str());
        if (!hMod)
            return false;

        // "don't load"
        if (*reinterpret_cast<uint32_t*>(hMod) == 0xFFFFFFFF)
            continue;

        auto nameTableEntry = GetTargetRVA<uintptr_t>(descriptor->OriginalFirstThunk);
        auto addressTableEntry = GetTargetRVA<uintptr_t>(descriptor->FirstThunk);

        if (!descriptor->OriginalFirstThunk)
            nameTableEntry = GetTargetRVA<uintptr_t>(descriptor->FirstThunk);

        for (; *nameTableEntry; ++nameTableEntry, ++addressTableEntry)
        {
            FARPROC function = nullptr;
            const char* functionName = nullptr;

            if (IMAGE_SNAP_BY_ORDINAL(*nameTableEntry))
            {
                function = GetProcAddress(hMod, MAKEINTRESOURCEA(IMAGE_ORDINAL(*nameTableEntry)));
            }
            else
            {
                const auto* import = GetTargetRVA<IMAGE_IMPORT_BY_NAME>(static_cast<uint32_t>(*nameTableEntry));
                functionName = import->Name;
                function = m_pFuncHandler(hMod, functionName);
            }

            if (!function)
            {
                SetLastError(ERROR_PROC_NOT_FOUND);
                return false;
            }

            *addressTableEntry = reinterpret_cast<uintptr_t>(function);
        }
    }

    return true;
}

bool ExeLoader::LoadSections(const IMAGE_NT_HEADERS* apNtHeader)
{
    const auto* section = IMAGE_FIRST_SECTION(apNtHeader);
    for (uint16_t index = 0; index < apNtHeader->FileHeader.NumberOfSections; ++index, ++section)
    {
        const auto virtualSize = std::max(section->Misc.VirtualSize, section->SizeOfRawData);
        if (!RangeFits(section->VirtualAddress, virtualSize, m_loadLimit))
            return BadImage();

        if (section->SizeOfRawData == 0)
            continue;

        if (!RangeFits(section->PointerToRawData, section->SizeOfRawData, m_binarySize))
            return BadImage();

        const auto copySize = std::min(section->SizeOfRawData, section->Misc.VirtualSize);
        if (copySize == 0)
            continue;

        auto* targetAddress = GetTargetRVA<uint8_t>(section->VirtualAddress);
        const auto* sourceAddress = m_pBinary + section->PointerToRawData;
        std::memcpy(targetAddress, sourceAddress, copySize);

        DWORD oldProtect = 0;
        if (!VirtualProtect(targetAddress, copySize, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
    }

    return true;
}

bool ExeLoader::LoadTLS(const IMAGE_NT_HEADERS* apNtHeader, const IMAGE_NT_HEADERS* apSourceNt)
{
    g_mappedTlsTemplate = nullptr;
    g_mappedTlsTemplateSize = 0;

    const auto& directory = apNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!directory.VirtualAddress && !directory.Size)
        return true;

    const auto* sourceTls = GetTargetRVA<IMAGE_TLS_DIRECTORY>(directory.VirtualAddress);
    const auto* targetTls = GetTargetRVA<IMAGE_TLS_DIRECTORY>(apSourceNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
    const auto tlsTemplateSize = static_cast<size_t>(sourceTls->EndAddressOfRawData - sourceTls->StartAddressOfRawData);
    if (tlsTemplateSize == 0 || tlsTemplateSize > GetMappedTlsSlotCapacity())
        return BadImage();

    *reinterpret_cast<DWORD*>(sourceTls->AddressOfIndex) = 0;

    g_mappedTlsTemplate = reinterpret_cast<const uint8_t*>(sourceTls->StartAddressOfRawData);
    g_mappedTlsTemplateSize = tlsTemplateSize;

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(targetTls->StartAddressOfRawData), tlsTemplateSize, PAGE_READWRITE, &oldProtect))
    {
        g_mappedTlsTemplate = nullptr;
        g_mappedTlsTemplateSize = 0;
        return false;
    }

    if (!ApplyMappedTlsToCurrentThread())
    {
        g_mappedTlsTemplate = nullptr;
        g_mappedTlsTemplateSize = 0;
        return BadImage();
    }

    std::memcpy(reinterpret_cast<void*>(targetTls->StartAddressOfRawData), g_mappedTlsTemplate, g_mappedTlsTemplateSize);
    return true;
}

bool ExeLoader::ApplyMappedTlsToCurrentThread() noexcept
{
    if (!g_mappedTlsTemplate || g_mappedTlsTemplateSize == 0 || g_mappedTlsTemplateSize > GetMappedTlsSlotCapacity())
        return false;

    auto* const tlsBase = *reinterpret_cast<uint8_t**>(__readgsqword(0x58));
    if (!tlsBase)
        return false;

    std::memcpy(tlsBase, g_mappedTlsTemplate, g_mappedTlsTemplateSize);
    return true;
}

size_t ExeLoader::GetMappedTlsTemplateSize() noexcept
{
    return g_mappedTlsTemplateSize;
}

size_t ExeLoader::GetMappedTlsSlotCapacity() noexcept
{
    return ::GetMappedTlsSlotCapacity();
}

bool ExeLoader::LoadExceptionTable(IMAGE_NT_HEADERS* apNtHeader)
{
    IMAGE_DATA_DIRECTORY* exceptionDirectory = &apNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    RUNTIME_FUNCTION* functionList = GetTargetRVA<RUNTIME_FUNCTION>(exceptionDirectory->VirtualAddress);
    DWORD entryCount = exceptionDirectory->Size / sizeof(RUNTIME_FUNCTION);

    if (!RtlAddFunctionTable(functionList, entryCount, reinterpret_cast<DWORD64>(m_moduleHandle)))
        return BadImage();

    // Replace the function table stored for debugger purposes (though we just added it above).
    PLIST_ENTRY(NTAPI * rtlGetFunctionTableListHead)(VOID);
    rtlGetFunctionTableListHead = reinterpret_cast<decltype(rtlGetFunctionTableListHead)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetFunctionTableListHead"));

    if (!rtlGetFunctionTableListHead)
        return true;

    auto tableListHead = rtlGetFunctionTableListHead();
    auto tableListEntry = tableListHead->Flink;
    while (tableListEntry != tableListHead)
    {
        auto functionTable = CONTAINING_RECORD(tableListEntry, DYNAMIC_FUNCTION_TABLE, Links);

        if (functionTable->BaseAddress == reinterpret_cast<ULONG_PTR>(m_moduleHandle) && functionTable->FunctionTable != functionList)
        {
            DWORD oldProtect = 0;
            if (!VirtualProtect(functionTable, sizeof(DYNAMIC_FUNCTION_TABLE), PAGE_READWRITE, &oldProtect))
                return false;

            functionTable->EntryCount = entryCount;
            functionTable->FunctionTable = functionList;

            if (!VirtualProtect(functionTable, sizeof(DYNAMIC_FUNCTION_TABLE), oldProtect, &oldProtect))
                return false;
        }

        tableListEntry = functionTable->Links.Flink;
    }

    return true;
}

bool ExeLoader::Rva2Offset(uint32_t aRva, size_t aLength, size_t& aOffset) const noexcept
{
    if (!m_pBinary || !m_binarySize || !RangeFits(0, sizeof(IMAGE_DOS_HEADER), m_binarySize))
        return false;

    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(m_pBinary);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)))
        return false;

    const auto ntOffset = static_cast<size_t>(dosHeader->e_lfanew);
    if (!RangeFits(ntOffset, sizeof(IMAGE_NT_HEADERS), m_binarySize))
        return false;

    const auto* ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(m_pBinary + ntOffset);
    const auto sectionOffset = ntOffset + sizeof(IMAGE_NT_HEADERS);
    const auto sectionSize = static_cast<size_t>(ntHeader->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!RangeFits(sectionOffset, sectionSize, m_binarySize))
        return false;

    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(m_pBinary + sectionOffset);
    ImageView image(std::span<const uint8_t>(m_pBinary, m_binarySize), ntHeader, sections, ntHeader->FileHeader.NumberOfSections, m_loadLimit);
    return image.RvaToOffset(aRva, aLength, aOffset);
}

bool ExeLoader::DecryptCeg(IMAGE_NT_HEADERS* apSourceNt)
{
    const auto entry = apSourceNt->OptionalHeader.AddressOfEntryPoint;
    size_t entryOffset = 0;
    if (!Rva2Offset(entry, sizeof(uint32_t), entryOffset))
        return BadImage();

    if (*reinterpret_cast<const uint32_t*>(m_pBinary + entryOffset) != 0x000000e8)
        return true;

    const auto* sections = IMAGE_FIRST_SECTION(apSourceNt);
    const IMAGE_SECTION_HEADER* textSection = nullptr;
    for (uint16_t index = 0; index < apSourceNt->FileHeader.NumberOfSections; ++index)
    {
        if (IsTextSection(sections[index]))
        {
            textSection = &sections[index];
            break;
        }
    }

    size_t textOffset = 0;
    if (!textSection || textSection->SizeOfRawData < sizeof(steam::SteamStubHeaderV31::CodeSectionStolenData) || textSection->SizeOfRawData % kAesBlockSize != 0 ||
        entryOffset < sizeof(steam::SteamStubHeaderV31) || !RangeFits(entryOffset - sizeof(steam::SteamStubHeaderV31), sizeof(steam::SteamStubHeaderV31), m_binarySize) ||
        !Rva2Offset(textSection->VirtualAddress, textSection->SizeOfRawData, textOffset))
    {
        return BadImage();
    }

    steam::CEGLocationInfo info{const_cast<uint8_t*>(m_pBinary + entryOffset), {const_cast<uint8_t*>(m_pBinary + textOffset), textSection->SizeOfRawData}};
    const auto realEntry = steam::CrackCEGInPlace(info);
    if (realEntry > std::numeric_limits<uint32_t>::max() || apSourceNt->FileHeader.NumberOfSections <= 1)
        return BadImage();

    --apSourceNt->FileHeader.NumberOfSections;
    apSourceNt->OptionalHeader.AddressOfEntryPoint = static_cast<uint32_t>(realEntry);
    return ValidateImage(std::span<const uint8_t>(m_pBinary, m_binarySize), m_loadLimit);
}

bool ExeLoader::Load(std::span<uint8_t> aProgramBuffer)
{
    m_pEntryPoint = nullptr;
    m_pBinary = nullptr;
    m_binarySize = 0;
    m_moduleHandle = nullptr;

    if (!ValidateImage(aProgramBuffer, m_loadLimit))
        return false;

    m_pBinary = aProgramBuffer.data();
    m_binarySize = aProgramBuffer.size();
    m_moduleHandle = GetModuleHandleW(nullptr);
    if (!m_moduleHandle)
        return false;

    const auto* dosHeader = GetRVA<const IMAGE_DOS_HEADER>(0);
    auto* ntHeader = GetRVA<IMAGE_NT_HEADERS>(dosHeader->e_lfanew);
    if (!DecryptCeg(ntHeader))
        return false;

    auto* sourceHeader = GetTargetRVA<IMAGE_DOS_HEADER>(0);
    if (sourceHeader->e_magic != IMAGE_DOS_SIGNATURE || sourceHeader->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)))
        return BadImage();

    auto* sourceNtHeader = GetTargetRVA<IMAGE_NT_HEADERS>(sourceHeader->e_lfanew);
    if (sourceNtHeader->Signature != IMAGE_NT_SIGNATURE || sourceNtHeader->OptionalHeader.SizeOfHeaders < static_cast<DWORD>(sourceHeader->e_lfanew) + sizeof(IMAGE_NT_HEADERS) ||
        ntHeader->FileHeader.NumberOfSections >
            (sourceNtHeader->OptionalHeader.SizeOfHeaders - static_cast<DWORD>(sourceHeader->e_lfanew) - sizeof(IMAGE_NT_HEADERS)) / sizeof(IMAGE_SECTION_HEADER))
    {
        return BadImage();
    }

    m_pEntryPoint = GetTargetRVA<void>(ntHeader->OptionalHeader.AddressOfEntryPoint);

    // Store these as they will get overridden by the target's header, but we
    // need them in order to preserve debugger-facing launcher metadata.
    const auto sourceChecksum = sourceNtHeader->OptionalHeader.CheckSum;
    const auto sourceTimestamp = sourceNtHeader->FileHeader.TimeDateStamp;
    const auto sourceDebugDir = sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

    if (!LoadSections(ntHeader))
        return false;

    DWORD oldProtect = 0;
    const auto headerCopySize = sizeof(IMAGE_NT_HEADERS) + static_cast<size_t>(ntHeader->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!VirtualProtect(sourceNtHeader, headerCopySize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    // SKSEVR preloads can hook _initterm_e while resolving imports, so expose
    // the mapped IAT before loading any of the target's dependencies.
    sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!LoadImports(ntHeader))
        return false;

#if defined(_M_AMD64)
    if (!LoadExceptionTable(ntHeader) || !LoadTLS(ntHeader, sourceNtHeader))
        return false;
#endif

    std::memcpy(sourceNtHeader, ntHeader, headerCopySize);

    // Good old switcheroo.
    // TODO: consider making this optional to allow loading the game's pdb.
    sourceNtHeader->OptionalHeader.CheckSum = sourceChecksum;
    sourceNtHeader->FileHeader.TimeDateStamp = sourceTimestamp;
    sourceNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] = sourceDebugDir;
    m_pBinary = nullptr;
    m_binarySize = 0;

    // Set a hook to check if anything loaded messes with critical hooks.
    extern void HookFormAllocateSentinelInit();
    HookFormAllocateSentinelInit();

    return true;
}
