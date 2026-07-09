#pragma once

#include <Games/Primitives.h>
#include <NetImmerse/NiPointer.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct NEW_REFR_DATA;
struct TESObjectCELL;
struct TESWorldSpace;
struct NiPoint3;
struct TESForm;
struct Actor;
struct ImageSpaceModifierInstance;

struct GridCellArray
{
    virtual ~GridCellArray();

    virtual void UnloadAll();
    virtual void sub_02();
    virtual void SetCenter(int aX, int aY);
    virtual void ProcessDeltaChange(int aOffsetX, int aOffsetY);
    virtual void UnloadOffset(int aOffsetX, int aOffsetY);
    virtual void LoadOffset(int aOffsetX, int aOffsetY);
    virtual void MoveCell(int aFromX, int aFromY, int aToX, int aToY);
    virtual void SwapCells(int aFirstX, int aFirstY, int aSecondX, int aSecondY);

    uint32_t unk8;
    uint32_t unkC;
    uint32_t dimension;
    TESObjectCELL** arr;
};

static_assert(offsetof(GridCellArray, arr) == 0x18);

struct TES
{
    using CommonLibTESOffsets = Skyrim::RuntimeLayout::TESCommonLibNgOffsets;
    using LocalTESOffsets = Skyrim::RuntimeLayout::TESLocalShimOffsets;

    static TES* Get() noexcept;

    [[nodiscard]] GridCellArray* GetGridCellsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<GridCellArray*>(this, CommonLibTESOffsets::GridCells);
#else
        return cells;
#endif
    }

    [[nodiscard]] int32_t GetCenterGridXData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibTESOffsets::CenterGridX);
#else
        return centerGridX;
#endif
    }

    [[nodiscard]] int32_t GetCenterGridYData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibTESOffsets::CenterGridY);
#else
        return centerGridY;
#endif
    }

    [[nodiscard]] int32_t GetCurrentGridXData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibTESOffsets::CurrentGridX);
#else
        return currentGridX;
#endif
    }

    [[nodiscard]] int32_t GetCurrentGridYData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<int32_t>(this, CommonLibTESOffsets::CurrentGridY);
#else
        return currentGridY;
#endif
    }

    [[nodiscard]] TESObjectCELL* GetInteriorCellData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESObjectCELL*>(this, CommonLibTESOffsets::InteriorCell);
#else
        return interiorCell;
#endif
    }

    [[nodiscard]] TESObjectCELL** GetInteriorBufferData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESObjectCELL**>(this, CommonLibTESOffsets::InteriorBuffer);
#else
        return interiorBuffer;
#endif
    }

    [[nodiscard]] TESObjectCELL** GetExteriorBufferData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESObjectCELL**>(this, CommonLibTESOffsets::ExteriorBuffer);
#else
        return exteriorBuffer;
#endif
    }

    [[nodiscard]] GameValueList<NiPointer<ImageSpaceModifierInstance>>& GetActiveImageSpaceModifiersData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameValueList<NiPointer<ImageSpaceModifierInstance>>>(this, CommonLibTESOffsets::ActiveImageSpaceModifiers);
#else
        return activeImageSpaceModifiers;
#endif
    }

    [[nodiscard]] const GameValueList<NiPointer<ImageSpaceModifierInstance>>& GetActiveImageSpaceModifiersData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<GameValueList<NiPointer<ImageSpaceModifierInstance>>>(this, CommonLibTESOffsets::ActiveImageSpaceModifiers);
#else
        return activeImageSpaceModifiers;
#endif
    }

    uint8_t pad[0x78];
    GridCellArray* cells;

    uint8_t pad80[0xB0 - 0x80];
    int32_t centerGridX;
    int32_t centerGridY;
    int32_t currentGridX;
    int32_t currentGridY;
    TESObjectCELL* interiorCell;
    TESObjectCELL** interiorBuffer;
    TESObjectCELL** exteriorBuffer;
    uint8_t padD8[0x108 - 0xD8];
    GameValueList<NiPointer<ImageSpaceModifierInstance>> activeImageSpaceModifiers;
};

static_assert(offsetof(TES, cells) == TES::LocalTESOffsets::GridCells);
static_assert(offsetof(TES, centerGridX) == TES::LocalTESOffsets::CenterGridX);
static_assert(offsetof(TES, centerGridY) == TES::LocalTESOffsets::CenterGridY);
static_assert(offsetof(TES, currentGridX) == TES::LocalTESOffsets::CurrentGridX);
static_assert(offsetof(TES, currentGridY) == TES::LocalTESOffsets::CurrentGridY);
static_assert(offsetof(TES, interiorCell) == TES::LocalTESOffsets::InteriorCell);
static_assert(offsetof(TES, interiorBuffer) == TES::LocalTESOffsets::InteriorBuffer);
static_assert(offsetof(TES, exteriorBuffer) == TES::LocalTESOffsets::ExteriorBuffer);
static_assert(offsetof(TES, activeImageSpaceModifiers) == TES::LocalTESOffsets::ActiveImageSpaceModifiers);

struct ProcessLists
{
    static ProcessLists* Get() noexcept;

    uint8_t pad00[0x8];

    bool bProcessHigh;
    bool bProcessLow;
    bool bProcessMHigh;
    bool bProcessMLow;
    bool bProcessSchedule;
    uint8_t padD[0x3];
    int32_t numberHighActors;
    uint8_t pad14[0x30 - 0x14];
    GameArray<uint32_t> highActorHandleArray;
    GameArray<uint32_t> lowActorHandleArray;
    GameArray<uint32_t> middleHighActorHandleArray;
    GameArray<uint32_t> middleLowActorHandleArray;
    GameArray<uint32_t>* actorBuckets[4];
};

static_assert(offsetof(ProcessLists, highActorHandleArray) == 0x30);
static_assert(offsetof(ProcessLists, actorBuckets) == 0x90);

struct Mod
{
    uint8_t pad0[0x58];
    char filename[104];
    uint8_t pad60[0x438 - 0xC0];
    uint32_t flags;
    uint8_t pad43C[0x478 - 0x43C];
    uint8_t standardId;
    uint8_t padStandardId;
    uint16_t liteId;

    [[nodiscard]] bool IsLoaded() const noexcept { return standardId != 0xFF; }

    [[nodiscard]] bool IsLite() const noexcept { return ((flags >> 9) & 1) != 0; }

    [[nodiscard]] uint16_t GetId() const noexcept { return IsLite() ? liteId : standardId; }

    [[nodiscard]] uint32_t GetFormId(uint32_t aBaseId) const noexcept;
};

static_assert(offsetof(Mod, filename) == 0x58);
static_assert(offsetof(Mod, standardId) == 0x478);
static_assert(offsetof(Mod, liteId) == 0x47A);

struct ModManager
{
    static ModManager* Get() noexcept;

    uint32_t Spawn(NiPoint3& aPosition, NiPoint3& aRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apCharacter) noexcept;
    Mod* GetByName(const char* acpName) const noexcept;
    TESObjectCELL* GetCellFromCoordinates(int32_t aX, int32_t aY, TESWorldSpace* aWorldSpace, bool aSpawnCell) noexcept;

    uint8_t pad0[0x748];
    GameArray<TESQuest*> quests;
    uint8_t pad760[0xD60 - 0x760];
    GameList<Mod> mods;
};

static_assert(offsetof(ModManager, mods) == 0xD60);

struct Setting
{
    void* unk0;
    uint64_t data;
    const char* name;
};

struct INISettingCollection
{
    static INISettingCollection* Get() noexcept;

    struct Entry
    {
        Setting* setting;
        Entry* next;
    };

    Setting* GetSetting(const char* acpName) noexcept;

    uint8_t unk0[0x118];
    Entry head;
};
