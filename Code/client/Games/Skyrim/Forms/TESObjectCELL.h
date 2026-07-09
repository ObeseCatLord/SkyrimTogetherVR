#pragma once

#include <Forms/TESForm.h>
#include <RuntimeLayout.h>

#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESObjectREFR;
struct TESWorldSpace;
struct BGSEncounterZone;
struct LoadedCellData;

struct TESObjectCELL : TESForm
{
    struct LoadedCellData;

    using CommonLibCellOffsets = Skyrim::RuntimeLayout::TESObjectCELLCommonLibNgOffsets;
    using LocalCellOffsets = Skyrim::RuntimeLayout::TESObjectCELLLocalShimOffsets;

    Vector<TESObjectREFR*> GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept;
    void GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept;

    [[nodiscard]] const uint8_t* GetCellFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<uint8_t>(this, CommonLibCellOffsets::CellFlags);
#else
        return cellFlags;
#endif
    }

    [[nodiscard]] bool IsInteriorCellData() const noexcept { return (GetCellFlagsData()[0] & 1) != 0; }

    [[nodiscard]] TESWorldSpace* GetWorldSpaceData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESWorldSpace*>(this, CommonLibCellOffsets::WorldSpace);
#else
        return worldspace;
#endif
    }

    [[nodiscard]] LoadedCellData* GetLoadedCellData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<LoadedCellData*>(this, CommonLibCellOffsets::LoadedCellData);
#else
        return loadedCellData;
#endif
    }

    bool IsValid() const { return GetCellFlagsData()[4] == 7; }

    struct ReferenceData
    {
        struct Reference
        {
            TESObjectREFR* ref;
            void* unk08;

            [[nodiscard]] TESObjectREFR* GetReferenceData() const noexcept { return unk08 != nullptr ? ref : nullptr; }

            TESObjectREFR* Get() const noexcept { return GetReferenceData(); }
        };

        uint64_t unk0;
        uint32_t unk8;
        uint32_t capacity;
        uint32_t available;
        uint32_t unkC;
        void* unk10;
        void* unk18;
        Reference* refArray;

        [[nodiscard]] uint32_t GetCapacityData() const noexcept { return capacity; }

        [[nodiscard]] uint32_t GetAvailableData() const noexcept { return available; }

        [[nodiscard]] const Reference* GetReferenceArrayData() const noexcept { return refArray; }

        [[nodiscard]] TESObjectREFR* GetReferenceAtData(uint32_t aIndex) const noexcept
        {
            const auto* pReferences = GetReferenceArrayData();
            return pReferences && aIndex < GetCapacityData() ? pReferences[aIndex].GetReferenceData() : nullptr;
        }

        uint32_t Count() const noexcept { return GetCapacityData() - GetAvailableData(); }
    };
    static_assert(sizeof(ReferenceData) == 0x30);

    [[nodiscard]] const ReferenceData* GetReferenceData() const noexcept
    {
#if TP_SKYRIM_VR
        return nullptr;
#else
        return &refData;
#endif
    }

    struct LoadedCellData
    {
        using CommonLibLoadedCellDataOffsets = Skyrim::RuntimeLayout::LoadedCellDataCommonLibNgOffsets;
        using LocalLoadedCellDataOffsets = Skyrim::RuntimeLayout::LoadedCellDataLocalShimOffsets;

        [[nodiscard]] BGSEncounterZone* GetEncounterZoneData() const noexcept
        {
#if TP_SKYRIM_VR
            return Skyrim::RuntimeLayout::Value<BGSEncounterZone*>(this, CommonLibLoadedCellDataOffsets::EncounterZone);
#else
            return encounterZone;
#endif
        }

        uint8_t pad0[0x160];
        BGSEncounterZone* encounterZone;
        uint8_t pad168[0x180 - 0x168];
    };
    static_assert(LoadedCellData::CommonLibLoadedCellDataOffsets::EncounterZone == 0x160);
    static_assert(LoadedCellData::CommonLibLoadedCellDataOffsets::Size == 0x180);
    static_assert(offsetof(LoadedCellData, encounterZone) == LoadedCellData::LocalLoadedCellDataOffsets::EncounterZone);
    static_assert(sizeof(LoadedCellData) == LoadedCellData::LocalLoadedCellDataOffsets::Size);

    uint8_t pad20[0x40 - 0x20];
    uint8_t cellFlags[5];
    bool autoWaterLoaded;
    bool cellDetached;
    uint8_t pad47;
    ExtraDataList extraData;
    uint64_t cellData;
    void* pCellLand;
    float waterHeight;
    void* pNavMeshes;
    ReferenceData refData;
    void* pUnkB8;
    GameArray<TESObjectREFR*> objectList;
    GameArray<void*> unkD8;
    GameArray<void*> unkF0;
    GameArray<void*> unk108;
    BSRecursiveLock lock;
    TESWorldSpace* worldspace;
    LoadedCellData* loadedCellData;
    void* pLightingTemplate;
    uint64_t unk140;
};

static_assert(offsetof(TESObjectCELL, cellFlags) == TESObjectCELL::LocalCellOffsets::CellFlags);
static_assert(offsetof(TESObjectCELL, refData) == TESObjectCELL::LocalCellOffsets::References);
static_assert(offsetof(TESObjectCELL, worldspace) == TESObjectCELL::LocalCellOffsets::WorldSpace);
static_assert(offsetof(TESObjectCELL, loadedCellData) == TESObjectCELL::LocalCellOffsets::LoadedCellData);
static_assert(sizeof(TESObjectCELL) == 0x148);
