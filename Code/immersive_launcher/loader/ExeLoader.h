#pragma once

/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

// Changes:
// - 2021/2/25: Added CEG decryption method.

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <span>

class ExeLoader
{
public:
    using TEntryPoint = void (*)();
    using TFuncHandler = FARPROC (*)(HMODULE, const char*);

    explicit ExeLoader(uint32_t aLoadLimit, TFuncHandler aFuncPtr = GetProcAddress);

    bool Load(std::span<uint8_t> aProgramBuffer);

    // Performs all source-image checks without touching the mapped process image.
    // Kept public so the focused PE fixtures can exercise the same gate as Load().
    static bool ValidateImage(std::span<const uint8_t> aProgramBuffer, uint32_t aLoadLimit) noexcept;

    TEntryPoint GetEntryPoint() const { return static_cast<TEntryPoint>(m_pEntryPoint); }

    // The manually mapped game uses TLS slot zero. Apply its initialized block
    // to a thread created after Load() before that thread enters native plugins.
    static bool ApplyMappedTlsToCurrentThread() noexcept;
    static size_t GetMappedTlsTemplateSize() noexcept;
    static size_t GetMappedTlsSlotCapacity() noexcept;

private:
    bool LoadSections(const IMAGE_NT_HEADERS* apNtHeader);
    bool LoadImports(const IMAGE_NT_HEADERS* apNtHeader);
    bool LoadTLS(const IMAGE_NT_HEADERS* apNtHeader, const IMAGE_NT_HEADERS* apSourceNt);
    bool LoadExceptionTable(IMAGE_NT_HEADERS* apNtHeader);
    bool DecryptCeg(IMAGE_NT_HEADERS* apSourceNt);

    template <typename T> inline T* GetRVA(uint32_t aRva) { return (T*)(m_pBinary + aRva); }

    template <typename T> inline T* GetTargetRVA(uint32_t aRva) { return (T*)((uint8_t*)m_moduleHandle + aRva); }

private:
    bool Rva2Offset(uint32_t aRva, size_t aLength, size_t& aOffset) const noexcept;

    const uint8_t* m_pBinary = nullptr;
    size_t m_binarySize = 0;
    const TFuncHandler m_pFuncHandler = nullptr;

    uint32_t m_loadLimit;
    HMODULE m_moduleHandle = nullptr;
    void* m_pEntryPoint = nullptr;
};
