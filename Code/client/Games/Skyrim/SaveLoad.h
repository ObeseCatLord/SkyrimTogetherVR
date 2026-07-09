#pragma once

#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESForm;

#pragma pack(push, 1)

struct BGSSaveLoadManager
{
    virtual ~BGSSaveLoadManager();

    struct SaveData
    {
        [[nodiscard]] uint32_t GetFlagsData() const noexcept
        {
#if TP_SKYRIM_VR
            return 0;
#else
            return flags;
#endif
        }

        void SetFlagsData(uint32_t aFlags) noexcept
        {
#if TP_SKYRIM_VR
            (void)aFlags;
#else
            flags = aFlags;
#endif
        }

        [[nodiscard]] const char* GetSaveNameData() const noexcept
        {
#if TP_SKYRIM_VR
            return nullptr;
#else
            return saveName;
#endif
        }

        void* unk0;           // maybe vtable
        uint32_t someCounter; // 0x8
        uint32_t unkC;
        void* unk10;        // 0x10
        void* someFunction; // 0x18

        uint32_t unk58; // 0x58

        uint32_t flags; // 0x340

        char* saveName; // 0xBB0
    };

    void Save(SaveData* apData);

    struct Struct330
    {
        void* unk0; // 0
        void* data; // 8
    };

    uint8_t pad8[0x330 - 0x8];
    Struct330* struct330; // 330
};

static_assert(offsetof(BGSSaveLoadManager::SaveData, someFunction) == 0x18);

struct BGSSaveFormBuffer
{
    using CommonLibSaveGameBufferOffsets = Skyrim::RuntimeLayout::BGSSaveGameBufferCommonLibNgOffsets;
    using LocalSaveGameBufferOffsets = Skyrim::RuntimeLayout::BGSSaveGameBufferLocalShimOffsets;
    using CommonLibSaveFormBufferOffsets = Skyrim::RuntimeLayout::BGSSaveFormBufferCommonLibNgOffsets;
    using LocalSaveFormBufferOffsets = Skyrim::RuntimeLayout::BGSSaveFormBufferLocalShimOffsets;

    BGSSaveFormBuffer();
    virtual ~BGSSaveFormBuffer() {}

    void WriteId(uint32_t aId) noexcept;

    [[nodiscard]] char* GetBufferData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<char*>(this, CommonLibSaveGameBufferOffsets::Buffer);
#else
        return buffer;
#endif
    }

    void SetBufferData(char* apBuffer) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<char*>(this, CommonLibSaveGameBufferOffsets::Buffer, apBuffer);
#else
        buffer = apBuffer;
#endif
    }

    [[nodiscard]] uint32_t GetCapacityData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibSaveGameBufferOffsets::BufferSize);
#else
        return capacity;
#endif
    }

    void SetCapacityData(uint32_t aCapacity) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibSaveGameBufferOffsets::BufferSize, aCapacity);
#else
        capacity = aCapacity;
#endif
    }

    [[nodiscard]] uint32_t GetPositionData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibSaveGameBufferOffsets::BufferPosition);
#else
        return position;
#endif
    }

    void SetPositionData(uint32_t aPosition) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibSaveGameBufferOffsets::BufferPosition, aPosition);
#else
        position = aPosition;
#endif
    }

    void AdvancePositionData(uint32_t aDelta) noexcept { SetPositionData(GetPositionData() + aDelta); }

    [[nodiscard]] uint32_t GetChangeFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::UnalignedValue<uint32_t>(this, CommonLibSaveFormBufferOffsets::ChangeFlags);
#else
        return changeFlags;
#endif
    }

    void SetChangeFlagsData(uint32_t aChangeFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::UnalignedStore<uint32_t>(this, CommonLibSaveFormBufferOffsets::ChangeFlags, aChangeFlags);
#else
        changeFlags = aChangeFlags;
#endif
    }

    char* buffer;         // 4 - 8
    uint32_t capacity;    // 8 - 10
    uint32_t position;    // C - 14
    uint8_t formId[3];    // 10 - 18 For some reason the flags start at 0x13 on oldrim
    uint32_t changeFlags; // 13 - 1B

    uint8_t pad[0x100]; // Ensure we have enough space as we don't know the exact size
};

static_assert(offsetof(BGSSaveFormBuffer, buffer) == BGSSaveFormBuffer::LocalSaveGameBufferOffsets::Buffer);
static_assert(offsetof(BGSSaveFormBuffer, capacity) == BGSSaveFormBuffer::LocalSaveGameBufferOffsets::BufferSize);
static_assert(offsetof(BGSSaveFormBuffer, position) == BGSSaveFormBuffer::LocalSaveGameBufferOffsets::BufferPosition);
static_assert(offsetof(BGSSaveFormBuffer, formId) == BGSSaveFormBuffer::LocalSaveFormBufferOffsets::FormIdIndex);
static_assert(offsetof(BGSSaveFormBuffer, changeFlags) == BGSSaveFormBuffer::LocalSaveFormBufferOffsets::ChangeFlags);

struct BGSSaveLoadBuffer
{
    char* pBuffer;
};

static_assert(sizeof(BGSSaveLoadBuffer) == 0x8);

struct BGSSaveGameBuffer
{
    virtual ~BGSSaveGameBuffer();

    BGSSaveLoadBuffer Buffer;
    uint32_t iBufferSize;
    uint32_t iBufferPosition;
};

static_assert(sizeof(BGSSaveGameBuffer) == 0x18);

struct BGSChangeFlags
{
    int32_t iFlags;
};

static_assert(sizeof(BGSChangeFlags) == 0x4);

struct BGSSaveLoadFormInfo
{
    uint8_t cData;
};

static_assert(sizeof(BGSSaveLoadFormInfo) == 0x1);

struct BGSNumericIDIndex
{
    uint8_t cData1;
    uint8_t cData2;
    uint8_t cData3;
};

struct BGSSaveLoadFormHeader
{
    BGSNumericIDIndex FormIDIndex;
    BGSChangeFlags iChangeFlags;
    BGSSaveLoadFormInfo FormInfo;
    uint8_t cVersion;
};

static_assert(sizeof(BGSSaveLoadFormHeader) == 0x9);

struct BGSSaveFormBufferReal : BGSSaveGameBuffer
{
    BGSSaveLoadFormHeader Header;
    uint8_t pad21[0x7];
    struct TESForm* pForm;
};

static_assert(sizeof(BGSSaveFormBufferReal) == 0x30);

struct BGSLoadFormBuffer
{
    using CommonLibLoadGameBufferOffsets = Skyrim::RuntimeLayout::BGSLoadGameBufferCommonLibNgOffsets;
    using LocalLoadGameBufferOffsets = Skyrim::RuntimeLayout::BGSLoadGameBufferLocalShimOffsets;
    using CommonLibLoadFormBufferOffsets = Skyrim::RuntimeLayout::BGSLoadFormBufferCommonLibNgOffsets;
    using LocalLoadFormBufferOffsets = Skyrim::RuntimeLayout::BGSLoadFormBufferLocalShimOffsets;

    BGSLoadFormBuffer(uint32_t aChangeFlags);
    virtual ~BGSLoadFormBuffer() {}

    void SetSize(const uint32_t aSize) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<std::size_t>(this, CommonLibLoadGameBufferOffsets::BufferSize, aSize);
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibLoadGameBufferOffsets::DataSize, aSize);
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibLoadFormBufferOffsets::DataSize, aSize);
#else
        capacity = size1 = size2 = aSize;
#endif
    }

    [[nodiscard]] const char* GetBufferData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<const char*>(this, CommonLibLoadGameBufferOffsets::Buffer);
#else
        return buffer;
#endif
    }

    void SetBufferData(const char* apBuffer) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<const char*>(this, CommonLibLoadGameBufferOffsets::Buffer, apBuffer);
#else
        buffer = apBuffer;
#endif
    }

    [[nodiscard]] std::size_t GetCapacityData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<std::size_t>(this, CommonLibLoadGameBufferOffsets::BufferSize);
#else
        return capacity;
#endif
    }

    [[nodiscard]] uint32_t GetPositionData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibLoadGameBufferOffsets::BufferPosition);
#else
        return position;
#endif
    }

    void SetPositionData(uint32_t aPosition) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibLoadGameBufferOffsets::BufferPosition, aPosition);
#else
        position = aPosition;
#endif
    }

    void AdvancePositionData(uint32_t aDelta) noexcept { SetPositionData(GetPositionData() + aDelta); }

    void SetFormIdData(uint32_t aFormId) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibLoadFormBufferOffsets::FormId, aFormId);
#else
        formId = aFormId;
#endif
    }

    void SetFormData(TESForm* apForm) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<TESForm*>(this, CommonLibLoadFormBufferOffsets::Form, apForm);
#else
        form = apForm;
#endif
    }

    void SetChangeFlagsData(uint32_t aChangeFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibLoadFormBufferOffsets::ChangeFlags, aChangeFlags);
#else
        changeFlags = aChangeFlags;
#endif
    }

    void SetOldChangeFlagsData(uint32_t aChangeFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibLoadFormBufferOffsets::OldChangeFlags, aChangeFlags);
#else
        maybeMoreFlags = aChangeFlags;
#endif
    }

    void SetLoadVersionData(uint8_t aVersion) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint8_t>(this, CommonLibLoadFormBufferOffsets::Version, aVersion);
#else
        loadFlag = aVersion;
#endif
    }

    void SetUnk1CData(int32_t aValue) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<int32_t>(this, CommonLibLoadGameBufferOffsets::Unk1C, aValue);
#else
        unk1C = aValue;
#endif
    }

    const char* buffer;      // 8
    size_t capacity;         // 10
    uint32_t unk18;          // 18 - 0x459 ? version maybe
    int32_t unk1C;           // 1C - set to -1
    uint32_t size1;          // 20
    uint32_t position;       // 24
    uint32_t formId;         // 28
    uint32_t size2;          // 2C
    uint32_t unk30;          // 30 - usually 0
    uint32_t unk34;          // 34 - 0 or 1 or 0xFFFF
    struct TESForm* form;    // 38
    uint32_t changeFlags;    // 40
    uint32_t maybeMoreFlags; // 44 ??????
    uint8_t unk48;           // 48 some flags apparently
    uint8_t unk49;
    uint8_t unk4A;
    uint8_t loadFlag;
};

static_assert(offsetof(BGSLoadFormBuffer, buffer) == BGSLoadFormBuffer::LocalLoadGameBufferOffsets::Buffer);
static_assert(offsetof(BGSLoadFormBuffer, capacity) == BGSLoadFormBuffer::LocalLoadGameBufferOffsets::BufferSize);
static_assert(offsetof(BGSLoadFormBuffer, unk1C) == BGSLoadFormBuffer::LocalLoadGameBufferOffsets::Unk1C);
static_assert(offsetof(BGSLoadFormBuffer, size1) == BGSLoadFormBuffer::LocalLoadGameBufferOffsets::DataSize);
static_assert(offsetof(BGSLoadFormBuffer, position) == BGSLoadFormBuffer::LocalLoadGameBufferOffsets::BufferPosition);
static_assert(offsetof(BGSLoadFormBuffer, formId) == BGSLoadFormBuffer::LocalLoadFormBufferOffsets::FormId);
static_assert(offsetof(BGSLoadFormBuffer, size2) == BGSLoadFormBuffer::LocalLoadFormBufferOffsets::DataSize);
static_assert(offsetof(BGSLoadFormBuffer, form) == BGSLoadFormBuffer::LocalLoadFormBufferOffsets::Form);
static_assert(offsetof(BGSLoadFormBuffer, changeFlags) == BGSLoadFormBuffer::LocalLoadFormBufferOffsets::ChangeFlags);
static_assert(offsetof(BGSLoadFormBuffer, maybeMoreFlags) == BGSLoadFormBuffer::LocalLoadFormBufferOffsets::OldChangeFlags);
static_assert(offsetof(BGSLoadFormBuffer, loadFlag) == BGSLoadFormBuffer::LocalLoadFormBufferOffsets::Version);

// TODO: mostly copied from fallout 4, needs to be validated
struct __declspec(align(8)) BGSSaveLoadScrapBuffer
{
    char* pBuffer;
    struct ScrapHeap* pScrapHeap;
    uint32_t uiSize;
};

static_assert(sizeof(BGSSaveLoadScrapBuffer) == 0x18);

struct BGSLoadGameBuffer
{
    virtual ~BGSLoadGameBuffer();

    BGSSaveLoadScrapBuffer Buffer;
    uint32_t iBufferSize;
    uint32_t iBufferPosition;
};

static_assert(sizeof(BGSLoadGameBuffer) == 0x28);

struct __declspec(align(8)) BGSLoadFormData
{
    uint32_t iFormID;
    uint32_t uiDataSize;
    uint32_t uiUncompressedSize;
    TESForm* pForm;
    BGSChangeFlags iChangeFlags;
    BGSChangeFlags iOldChangeFlags;
    uint16_t usFlags;
    BGSSaveLoadFormInfo FormInfo;
    uint8_t cVersion;
};

// static_assert(sizeof(BGSLoadFormData) == 0x28);

struct BGSLoadFormBufferReal : BGSLoadGameBuffer, BGSLoadFormData
{
};

// static_assert(sizeof(BGSLoadFormBufferReal) == 0x50);

#pragma pack(pop)
