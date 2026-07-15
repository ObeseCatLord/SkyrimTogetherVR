#pragma once

#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdio.h>
#include <string>
#include <utility>

#pragma comment(lib, "version.lib")

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

class VersionDb
{
public:
    VersionDb() { Clear(); }
    ~VersionDb() {}

    static VersionDb& Get();

private:
    std::map<unsigned long long, unsigned long long> _data;
    std::map<unsigned long long, unsigned long long> _rdata;
    std::set<unsigned long long> _vrProjectOverrideIds;
    int _ver[4];
    std::string _verStr;
    std::string _moduleName;
    std::string _lastError;
    unsigned long long _base;

    template <typename T> static bool TryRead(std::ifstream& file, T& value)
    {
        return static_cast<bool>(file.read(reinterpret_cast<char*>(&value), sizeof(T)));
    }

    bool Fail(std::string error)
    {
        _data.clear();
        _rdata.clear();
        _vrProjectOverrideIds.clear();
        for (int& versionPart : _ver)
            versionPart = 0;
        _verStr.clear();
        _moduleName.clear();
        _base = 0;
        _lastError = std::move(error);
        return false;
    }

    static void* ToPointer(unsigned long long v) { return (void*)v; }

    static unsigned long long FromPointer(void* ptr) { return (unsigned long long)ptr; }

    static bool ParseVersionFromString(const char* ptr, int& major, int& minor, int& revision, int& build) { return sscanf_s(ptr, "%d.%d.%d.%d", &major, &minor, &revision, &build) == 4 && ((major != 1 && major != 0) || minor != 0 || revision != 0 || build != 0); }

    static void StripBomAndLineEnding(std::string& line)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
            line.erase(0, 3);
    }

    static std::string TrimCsvField(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n\"");
        if (first == std::string::npos)
            return {};

        const auto last = value.find_last_not_of(" \t\r\n\"");
        return value.substr(first, last - first + 1);
    }

    static bool SplitFirstTwoCsvFields(const std::string& line, std::string& first, std::string& second)
    {
        const auto firstComma = line.find(',');
        if (firstComma == std::string::npos)
            return false;

        const auto secondComma = line.find(',', firstComma + 1);
        first = TrimCsvField(line.substr(0, firstComma));
        second = TrimCsvField(secondComma == std::string::npos ? line.substr(firstComma + 1) : line.substr(firstComma + 1, secondComma - firstComma - 1));
        return !first.empty() && !second.empty();
    }

    static bool ParseUnsigned(const std::string& value, int base, unsigned long long& result)
    {
        if (value.empty())
            return false;

        char* end = nullptr;
        result = std::strtoull(value.c_str(), &end, base);
        return end != value.c_str() && end && *end == '\0';
    }

    void SetOffsetForId(unsigned long long id, unsigned long long offset)
    {
        const auto existing = _data.find(id);
        if (existing != _data.end() && existing->second != offset)
        {
            const auto previousOffset = existing->second;
            const auto reverse = _rdata.find(previousOffset);
            if (reverse != _rdata.end() && reverse->second == id)
            {
                _rdata.erase(reverse);
                for (const auto& [candidateId, candidateOffset] : _data)
                {
                    if (candidateId != id && candidateOffset == previousOffset)
                    {
                        _rdata[previousOffset] = candidateId;
                        break;
                    }
                }
            }
        }

        _data[id] = offset;
        _rdata[offset] = id;
    }

    bool LoadCsvOffsetFile(const std::filesystem::path& path, bool aAllowVersionMetadata, std::set<unsigned long long>* apLoadedIds = nullptr)
    {
        std::ifstream file(path);
        if (!file.good())
            return Fail("Missing required address file: " + path.string());

        std::string line;
        bool loadedAny = false;
        size_t lineNumber = 0;
        std::map<unsigned long long, unsigned long long> fileEntries;
        while (std::getline(file, line))
        {
            ++lineNumber;
            StripBomAndLineEnding(line);
            if (line.empty())
                continue;

            std::string idField;
            std::string offsetField;
            if (!SplitFirstTwoCsvFields(line, idField, offsetField))
                return Fail("Malformed address row in " + path.string() + " at line " + std::to_string(lineNumber));

            unsigned long long id = 0;
            unsigned long long offset = 0;
            if (!ParseUnsigned(idField, 10, id) || !ParseUnsigned(offsetField, 16, offset))
            {
                if (lineNumber == 1 || (aAllowVersionMetadata && lineNumber == 2 && offsetField.find('.') != std::string::npos))
                    continue;
                return Fail("Malformed address row in " + path.string() + " at line " + std::to_string(lineNumber));
            }

            const auto [existing, inserted] = fileEntries.emplace(id, offset);
            if (!inserted)
            {
                if (existing->second == offset)
                    continue;
                return Fail("Conflicting duplicate address ID " + std::to_string(id) + " in " + path.string());
            }

            SetOffsetForId(id, offset);
            if (apLoadedIds)
                apLoadedIds->insert(id);
            loadedAny = true;
        }

        if (!file.eof() && file.fail())
            return Fail("Failed while reading address file: " + path.string());
        if (!loadedAny)
            return Fail("Address file contains no usable rows: " + path.string());
        return true;
    }

#if TP_SKYRIM_VR
    bool ApplyVrProjectAddressFiles(const std::filesystem::path& acGamePath)
    {
        const auto pluginPath = acGamePath / "Data" / "SKSE" / "Plugins";
        if (!LoadCsvOffsetFile(pluginPath / "SkyrimTogetherVR_AddressOverrides.csv", false, &_vrProjectOverrideIds))
            return false;
        return ApplyIdAliasFile(pluginPath / "SkyrimTogetherVR_AE_to_SE.csv");
    }

    bool ApplyIdAliasFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.good())
            return Fail("Missing required address alias file: " + path.string());

        std::string line;
        size_t lineNumber = 0;
        bool loadedAny = false;
        std::map<unsigned long long, unsigned long long> aliasTargets;
        while (std::getline(file, line))
        {
            ++lineNumber;
            StripBomAndLineEnding(line);
            if (line.empty())
                continue;

            std::string seIdField;
            std::string aeIdField;
            if (!SplitFirstTwoCsvFields(line, seIdField, aeIdField))
                return Fail("Malformed address alias row in " + path.string() + " at line " + std::to_string(lineNumber));

            unsigned long long seId = 0;
            unsigned long long aeId = 0;
            if (!ParseUnsigned(seIdField, 10, seId) || !ParseUnsigned(aeIdField, 10, aeId))
            {
                if (lineNumber == 1)
                    continue;
                return Fail("Malformed address alias row in " + path.string() + " at line " + std::to_string(lineNumber));
            }

            const auto mapped = _data.find(seId);
            if (mapped == _data.end())
                return Fail("Address alias source ID " + std::to_string(seId) + " is absent from the loaded VR database");

            const auto [existingAlias, inserted] = aliasTargets.emplace(aeId, mapped->second);
            if (!inserted && existingAlias->second != mapped->second)
                return Fail("Conflicting aliases target address ID " + std::to_string(aeId));

            if (_vrProjectOverrideIds.contains(aeId))
            {
                // Explicit hand-authored project corrections are authoritative.
                // Package audits report differing generated aliases.
                loadedAny = true;
                continue;
            }

            // Generated aliases are authoritative over coincidentally equal raw
            // VR numeric IDs used by unrelated functions.
            SetOffsetForId(aeId, mapped->second);
            loadedAny = true;
        }

        if (!file.eof() && file.fail())
            return Fail("Failed while reading address alias file: " + path.string());
        if (!loadedAny)
            return Fail("Address alias file contains no usable rows: " + path.string());
        return true;
    }

    bool LoadVrCsvAddressLibrary(const std::filesystem::path& acGamePath, int major, int minor, int revision, int build)
    {
        char fileName[256];
        _snprintf_s(fileName, 256, "version-%d-%d-%d-%d.csv", major, minor, revision, build);

        const auto pluginPath = acGamePath / "Data" / "SKSE" / "Plugins";
        if (!LoadCsvOffsetFile(pluginPath / fileName, true))
            return false;

        for (int i = 0; i < 4; i++)
            _ver[i] = 0;

        _ver[0] = major;
        _ver[1] = minor;
        _ver[2] = revision;
        _ver[3] = build;

        {
            char verName[64];
            _snprintf_s(verName, 64, "%d.%d.%d.%d", _ver[0], _ver[1], _ver[2], _ver[3]);
            _verStr = verName;
        }

        _moduleName = "SkyrimVR.exe";
        _base = reinterpret_cast<unsigned long long>(GetModuleHandleA(NULL));

        if (!ApplyVrProjectAddressFiles(acGamePath))
            return false;

        return !_data.empty();
    }
#endif

public:
    const std::string& GetModuleName() const { return _moduleName; }
    const std::string& GetLoadedVersionString() const { return _verStr; }
    const std::string& GetLastError() const { return _lastError; }

    const std::map<unsigned long long, unsigned long long>& GetOffsetMap() const { return _data; }

    void* FindAddressById(unsigned long long id) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return NULL;

        unsigned long long offset = 0;
        if (!FindOffsetById(id, offset))
            return NULL;

        return ToPointer(b + offset);
    }

    bool FindOffsetById(unsigned long long id, unsigned long long& result) const
    {
        auto itr = _data.find(id);
        if (itr != _data.end())
        {
            result = itr->second;
            return true;
        }
        return false;
    }

    bool FindIdByAddress(void* ptr, unsigned long long& result) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return false;

        unsigned long long addr = FromPointer(ptr);
        return FindIdByOffset(addr - b, result);
    }

    bool FindIdByOffset(unsigned long long offset, unsigned long long& result) const
    {
        auto itr = _rdata.find(offset);
        if (itr == _rdata.end())
            return false;

        result = itr->second;
        return true;
    }

    bool GetExecutableVersion(int& major, int& minor, int& revision, int& build) const
    {
        TCHAR szVersionFile[MAX_PATH];
        GetModuleFileName(NULL, szVersionFile, MAX_PATH);

        DWORD verHandle = 0;
        UINT size = 0;
        LPBYTE lpBuffer = NULL;
        DWORD verSize = GetFileVersionInfoSize(szVersionFile, &verHandle);

        if (verSize != NULL)
        {
            LPSTR verData = new char[verSize];

            if (GetFileVersionInfo(szVersionFile, verHandle, verSize, verData))
            {
                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\ProductVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }

                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\FileVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }
            }

            delete[] verData;
        }

        return false;
    }

    void GetLoadedVersion(int& major, int& minor, int& revision, int& build) const
    {
        major = _ver[0];
        minor = _ver[1];
        revision = _ver[2];
        build = _ver[3];
    }

    void Clear()
    {
        _data.clear();
        _rdata.clear();
        _vrProjectOverrideIds.clear();
        for (int i = 0; i < 4; i++)
            _ver[i] = 0;
        _verStr = std::string();
        _moduleName = std::string();
        _lastError = std::string();
        _base = 0;
    }

    bool Load(const std::filesystem::path& acGamePath, const TiltedPhoques::String& acExeVersion)
    {
        int major, minor, revision, build;

        if (!ParseVersionFromString(acExeVersion.c_str(), major, minor, revision, build))
        {
            Clear();
            return Fail("Executable version text is invalid: " + std::string(acExeVersion.c_str()));
        }

        return Load(acGamePath, major, minor, revision, build);
    }

    bool Load(const std::filesystem::path& acGamePath, int major, int minor, int revision, int build)
    {
        Clear();

#if TP_SKYRIM_VR
        if (major != 1 || minor != 4 || revision != 15 || build != 0)
            return Fail(
                "Unsupported Skyrim VR runtime " + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision) + "." +
                std::to_string(build));
#endif

        char fileName[256];
        _snprintf_s(fileName, 256, "versionlib-%d-%d-%d-%d.bin", major, minor, revision, build);

        const auto binaryPath = acGamePath / "Data" / "SKSE" / "Plugins" / fileName;
        std::ifstream file(binaryPath, std::ios::binary);
        if (!file.good())
        {
#if TP_SKYRIM_VR
            return LoadVrCsvAddressLibrary(acGamePath, major, minor, revision, build);
#else
            return Fail("Missing address library: " + binaryPath.string());
#endif
        }

        int format = 0;
        if (!TryRead(file, format) || format != 2)
            return Fail("Invalid or truncated address-library header: " + binaryPath.string());

        for (int i = 0; i < 4; i++)
        {
            if (!TryRead(file, _ver[i]))
                return Fail("Truncated address-library runtime version: " + binaryPath.string());
        }
        if (_ver[0] != major || _ver[1] != minor || _ver[2] != revision || _ver[3] != build)
            return Fail("Address-library runtime version does not match the executable: " + binaryPath.string());

        {
            char verName[64];
            _snprintf_s(verName, 64, "%d.%d.%d.%d", _ver[0], _ver[1], _ver[2], _ver[3]);
            _verStr = verName;
        }

        int tnLen = 0;
        if (!TryRead(file, tnLen) || tnLen <= 0 || tnLen >= 0x10000)
            return Fail("Invalid address-library module-name length: " + binaryPath.string());

        _moduleName.resize(static_cast<size_t>(tnLen));
        if (!file.read(_moduleName.data(), tnLen))
            return Fail("Truncated address-library module name: " + binaryPath.string());
#if TP_SKYRIM_VR
        if (_stricmp(_moduleName.c_str(), "SkyrimVR.exe") != 0)
            return Fail("Address library targets the wrong executable module: " + _moduleName);
#endif

        {
            HMODULE handle = GetModuleHandleA(NULL);
            _base = (unsigned long long)handle;
        }

        int ptrSize = 0;
        int addrCount = 0;
        if (!TryRead(file, ptrSize) || (ptrSize != 4 && ptrSize != 8))
            return Fail("Invalid address-library pointer size: " + binaryPath.string());
        if (!TryRead(file, addrCount) || addrCount <= 0 || addrCount > 2000000)
            return Fail("Invalid address-library entry count: " + binaryPath.string());

        unsigned char type, low, high;
        unsigned char b1, b2;
        unsigned short w1, w2;
        unsigned int d1, d2;
        unsigned long long q1, q2;
        unsigned long long pvid = 0;
        unsigned long long poffset = 0;
        unsigned long long tpoffset;
        const auto checkedAdd = [](unsigned long long left, unsigned long long right, unsigned long long& result)
        {
            if (left > std::numeric_limits<unsigned long long>::max() - right)
                return false;
            result = left + right;
            return true;
        };
        const auto checkedSubtract = [](unsigned long long left, unsigned long long right, unsigned long long& result)
        {
            if (left < right)
                return false;
            result = left - right;
            return true;
        };
        for (int i = 0; i < addrCount; i++)
        {
            if (!TryRead(file, type))
                return Fail("Truncated address-library entry " + std::to_string(i) + ": " + binaryPath.string());
            low = type & 0xF;
            high = type >> 4;

            switch (low)
            {
            case 0:
                if (!TryRead(file, q1))
                    return Fail("Truncated address ID at entry " + std::to_string(i));
                break;
            case 1:
                if (!checkedAdd(pvid, 1, q1))
                    return Fail("Address ID overflow at entry " + std::to_string(i));
                break;
            case 2:
                if (!TryRead(file, b1) || !checkedAdd(pvid, b1, q1))
                    return Fail("Invalid address ID delta at entry " + std::to_string(i));
                break;
            case 3:
                if (!TryRead(file, b1) || !checkedSubtract(pvid, b1, q1))
                    return Fail("Invalid address ID delta at entry " + std::to_string(i));
                break;
            case 4:
                if (!TryRead(file, w1) || !checkedAdd(pvid, w1, q1))
                    return Fail("Invalid address ID delta at entry " + std::to_string(i));
                break;
            case 5:
                if (!TryRead(file, w1) || !checkedSubtract(pvid, w1, q1))
                    return Fail("Invalid address ID delta at entry " + std::to_string(i));
                break;
            case 6:
                if (!TryRead(file, w1))
                    return Fail("Truncated address ID at entry " + std::to_string(i));
                q1 = w1;
                break;
            case 7:
                if (!TryRead(file, d1))
                    return Fail("Truncated address ID at entry " + std::to_string(i));
                q1 = d1;
                break;
            default: return Fail("Invalid address ID encoding at entry " + std::to_string(i));
            }

            tpoffset = (high & 8) != 0 ? (poffset / (unsigned long long)ptrSize) : poffset;

            switch (high & 7)
            {
            case 0:
                if (!TryRead(file, q2))
                    return Fail("Truncated address offset at entry " + std::to_string(i));
                break;
            case 1:
                if (!checkedAdd(tpoffset, 1, q2))
                    return Fail("Address offset overflow at entry " + std::to_string(i));
                break;
            case 2:
                if (!TryRead(file, b2) || !checkedAdd(tpoffset, b2, q2))
                    return Fail("Invalid address offset delta at entry " + std::to_string(i));
                break;
            case 3:
                if (!TryRead(file, b2) || !checkedSubtract(tpoffset, b2, q2))
                    return Fail("Invalid address offset delta at entry " + std::to_string(i));
                break;
            case 4:
                if (!TryRead(file, w2) || !checkedAdd(tpoffset, w2, q2))
                    return Fail("Invalid address offset delta at entry " + std::to_string(i));
                break;
            case 5:
                if (!TryRead(file, w2) || !checkedSubtract(tpoffset, w2, q2))
                    return Fail("Invalid address offset delta at entry " + std::to_string(i));
                break;
            case 6:
                if (!TryRead(file, w2))
                    return Fail("Truncated address offset at entry " + std::to_string(i));
                q2 = w2;
                break;
            case 7:
                if (!TryRead(file, d2))
                    return Fail("Truncated address offset at entry " + std::to_string(i));
                q2 = d2;
                break;
            }

            if ((high & 8) != 0)
            {
                if (q2 > std::numeric_limits<unsigned long long>::max() / static_cast<unsigned long long>(ptrSize))
                    return Fail("Address offset multiplication overflow at entry " + std::to_string(i));
                q2 *= (unsigned long long)ptrSize;
            }

            SetOffsetForId(q1, q2);

            poffset = q2;
            pvid = q1;
        }

#if TP_SKYRIM_VR
        // Project corrections must be authoritative regardless of whether the
        // base VR Address Library is distributed as CSV or versionlib binary.
        if (!ApplyVrProjectAddressFiles(acGamePath))
            return false;
#endif

        return !_data.empty() || Fail("Address library contains no entries: " + binaryPath.string());
    }

    bool DumpToTextFile(const std::string& path)
    {
        std::ofstream f = std::ofstream(path.c_str());
        if (!f.good())
            return false;

        for (auto itr = _data.begin(); itr != _data.end(); itr++)
        {
            f << std::dec;
            f << itr->first;
            f << '\t';
            f << std::hex;
            f << itr->second + 0x140000000;
            f << '\n';
        }

        return true;
    }
};

template <class T> struct VersionDbPtr
{
    VersionDbPtr(const uint32_t aId) noexcept
        : m_pPtr{nullptr}
        , m_id{aId}
    {
    }

    VersionDbPtr() = delete;
    VersionDbPtr(VersionDbPtr&) = delete;
    VersionDbPtr& operator=(VersionDbPtr&) = delete;

    operator T*() const noexcept { return Get(); }

    T* operator->() const noexcept { return Get(); }

    T* Get() const noexcept { return static_cast<T*>(GetPtr()); }

    void* GetPtr() const noexcept
    {
        if (m_pPtr == nullptr)
            m_pPtr = VersionDb::Get().FindAddressById(m_id);

        return m_pPtr;
    }

private:
    mutable void* m_pPtr;
    uint32_t m_id;
};
