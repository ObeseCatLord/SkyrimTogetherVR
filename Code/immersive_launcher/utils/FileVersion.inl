#pragma once

#include <Memory.hpp>
#include <Windows.h>

inline bool QueryFileVersion(const wchar_t* acpPath, launcher::RuntimeVersion& aVersion) noexcept
{
    aVersion = {};
    if (!acpPath || !*acpPath)
        return false;

    DWORD ignoredHandle = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(acpPath, &ignoredHandle);
    if (!infoSize)
        return false;

    auto buffer = TiltedPhoques::MakeUnique<uint8_t[]>(infoSize);
    if (!buffer || !GetFileVersionInfoW(acpPath, 0, infoSize, buffer.get()))
        return false;

    VS_FIXEDFILEINFO* pFixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (!VerQueryValueW(buffer.get(), L"\\", reinterpret_cast<void**>(&pFixedInfo), &fixedInfoSize) || !pFixedInfo || fixedInfoSize < sizeof(VS_FIXEDFILEINFO) ||
        pFixedInfo->dwSignature != 0xFEEF04BD)
        return false;

    aVersion.Major = HIWORD(pFixedInfo->dwFileVersionMS);
    aVersion.Minor = LOWORD(pFixedInfo->dwFileVersionMS);
    aVersion.Revision = HIWORD(pFixedInfo->dwFileVersionLS);
    aVersion.Build = LOWORD(pFixedInfo->dwFileVersionLS);
    return aVersion.IsValid();
}
