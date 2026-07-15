#pragma once

#include "../RuntimeVersion.h"

#include <Windows.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace launcher
{
namespace detail
{
struct LanguageAndCodePage
{
    WORD Language;
    WORD CodePage;
};

enum class VersionValueState
{
    Missing,
    Valid,
    Invalid,
};

inline bool BufferContains(const uint8_t* acpBuffer, size_t aBufferSize, const void* acpValue, size_t aValueSize) noexcept
{
    if (!acpBuffer || !acpValue)
        return false;

    const auto begin = reinterpret_cast<uintptr_t>(acpBuffer);
    const auto value = reinterpret_cast<uintptr_t>(acpValue);
    if (aBufferSize > std::numeric_limits<uintptr_t>::max() - begin || value < begin)
        return false;

    const auto end = begin + aBufferSize;
    return value <= end && aValueSize <= end - value;
}

inline VersionValueState QueryTranslatedVersion(
    const uint8_t* acpBuffer, size_t aBufferSize, const LanguageAndCodePage& acTranslation, const wchar_t* acpKey,
    RuntimeVersion& aVersion) noexcept
{
    wchar_t query[80]{};
    if (swprintf_s(
            query, _countof(query), L"\\StringFileInfo\\%04x%04x\\%ls", static_cast<unsigned int>(acTranslation.Language),
            static_cast<unsigned int>(acTranslation.CodePage), acpKey) < 0)
        return VersionValueState::Invalid;

    wchar_t* pValue = nullptr;
    UINT valueLength = 0;
    if (!VerQueryValueW(const_cast<uint8_t*>(acpBuffer), query, reinterpret_cast<void**>(&pValue), &valueLength))
        return VersionValueState::Missing;

    if (!pValue || !valueLength || valueLength > std::numeric_limits<size_t>::max() / sizeof(wchar_t) ||
        !BufferContains(acpBuffer, aBufferSize, pValue, static_cast<size_t>(valueLength) * sizeof(wchar_t)))
        return VersionValueState::Invalid;

    RuntimeVersion parsed{};
    if (!ParseRuntimeVersionString(pValue, valueLength, parsed))
        return VersionValueState::Invalid;

    aVersion = parsed;
    return VersionValueState::Valid;
}

inline bool MergeVersionValue(
    VersionValueState aState, const RuntimeVersion& acCandidate, bool& aHasValue, RuntimeVersion& aValue) noexcept
{
    if (aState == VersionValueState::Invalid)
        return false;
    if (aState == VersionValueState::Missing)
        return true;

    if (aHasValue && !aValue.Equals(acCandidate))
        return false;

    aHasValue = true;
    aValue = acCandidate;
    return true;
}
} // namespace detail

inline bool QueryFileVersion(const wchar_t* acpPath, RuntimeVersion& aVersion) noexcept
{
    aVersion = {};
    if (!acpPath || !*acpPath)
        return false;

    DWORD ignoredHandle = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(acpPath, &ignoredHandle);
    if (!infoSize)
        return false;

    auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[infoSize]);
    if (!buffer || !GetFileVersionInfoW(acpPath, 0, infoSize, buffer.get()))
        return false;

    detail::LanguageAndCodePage* pTranslations = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(buffer.get(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&pTranslations), &translationBytes) || !pTranslations ||
        translationBytes < sizeof(detail::LanguageAndCodePage) || translationBytes % sizeof(detail::LanguageAndCodePage) != 0 ||
        !detail::BufferContains(buffer.get(), infoSize, pTranslations, translationBytes))
        return false;
    const size_t translationCount = translationBytes / sizeof(detail::LanguageAndCodePage);

    RuntimeVersion productVersion{};
    RuntimeVersion fileVersion{};
    bool hasProductVersion = false;
    bool hasFileVersion = false;
    for (size_t index = 0; index < translationCount; ++index)
    {
        bool duplicate = false;
        for (size_t prior = 0; prior < index; ++prior)
        {
            if (pTranslations[prior].Language == pTranslations[index].Language && pTranslations[prior].CodePage == pTranslations[index].CodePage)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        RuntimeVersion candidate{};
        const auto productState = detail::QueryTranslatedVersion(buffer.get(), infoSize, pTranslations[index], L"ProductVersion", candidate);
        if (!detail::MergeVersionValue(productState, candidate, hasProductVersion, productVersion))
            return false;

        candidate = {};
        const auto fileState = detail::QueryTranslatedVersion(buffer.get(), infoSize, pTranslations[index], L"FileVersion", candidate);
        if (!detail::MergeVersionValue(fileState, candidate, hasFileVersion, fileVersion))
            return false;
    }

    if (!hasProductVersion && !hasFileVersion)
        return false;
    if (hasProductVersion && hasFileVersion && !productVersion.Equals(fileVersion))
        return false;

    aVersion = hasProductVersion ? productVersion : fileVersion;
    return true;
}
} // namespace launcher
