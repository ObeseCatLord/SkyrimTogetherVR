#pragma once

#include <Forms/TESActorBase.h>

#include <Components/TESRaceForm.h>
#include <Components/BGSOverridePackCollection.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BGSColorForm;
struct BGSTextureSet;
struct TESClass;
struct TESCombatStyle;
struct TESObjectARMO;
struct BGSOutfit;
struct BGSHeadPart;
struct BGSRelationship;

struct TESNPC : TESActorBase
{
    using CommonLibNpcOffsets = Skyrim::RuntimeLayout::TESNPCCommonLibNgOffsets;
    using LocalNpcOffsets = Skyrim::RuntimeLayout::TESNPCLocalShimOffsets;

    static constexpr FormType Type = FormType::Npc;

    static TESNPC* Create(const String& acBuffer, uint32_t aChangeFlags) noexcept;

    TESNPC* GetTemplateBase() const noexcept
    {
        TESNPC* pTemplate = GetTemplateNpcData();

        while (pTemplate && pTemplate->IsTemporary())
            pTemplate = pTemplate->GetTemplateNpcData();

        return pTemplate;
    }

    [[nodiscard]] TESRaceForm& GetRaceFormData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESRaceForm>(this, CommonLibNpcOffsets::RaceForm);
#else
        return raceForm;
#endif
    }

    [[nodiscard]] const TESRaceForm& GetRaceFormData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<TESRaceForm>(this, CommonLibNpcOffsets::RaceForm);
#else
        return raceForm;
#endif
    }

    [[nodiscard]] TESClass* GetNpcClassData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESClass*>(this, CommonLibNpcOffsets::NpcClass);
#else
        return npcClass;
#endif
    }

    void SetNpcClassData(TESClass* apClass) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<TESClass*>(this, CommonLibNpcOffsets::NpcClass, apClass);
#else
        npcClass = apClass;
#endif
    }

    [[nodiscard]] TESCombatStyle* GetCombatStyleData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESCombatStyle*>(this, CommonLibNpcOffsets::CombatStyle);
#else
        return combatStyle;
#endif
    }

    void SetCombatStyleData(TESCombatStyle* apCombatStyle) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<TESCombatStyle*>(this, CommonLibNpcOffsets::CombatStyle, apCombatStyle);
#else
        combatStyle = apCombatStyle;
#endif
    }

    void SetOriginalRaceData(TESRace* apRace) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<TESRace*>(this, CommonLibNpcOffsets::OriginalRace, apRace);
#else
        overlayRace = apRace;
#endif
    }

    [[nodiscard]] TESNPC* GetTemplateNpcData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<TESNPC*>(this, CommonLibNpcOffsets::FaceNpc);
#else
        return npcTemplate;
#endif
    }

    [[nodiscard]] BGSOutfit* GetDefaultOutfitData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSOutfit*>(this, CommonLibNpcOffsets::DefaultOutfit);
#else
        return outfits[0];
#endif
    }

    void SetDefaultOutfitData(BGSOutfit* apOutfit) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<BGSOutfit*>(this, CommonLibNpcOffsets::DefaultOutfit, apOutfit);
#else
        outfits[0] = apOutfit;
#endif
    }

    [[nodiscard]] BGSHeadPart** GetHeadPartsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<BGSHeadPart**>(this, CommonLibNpcOffsets::HeadParts);
#else
        return headparts;
#endif
    }

    [[nodiscard]] uint8_t GetHeadPartsCountData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint8_t>(this, CommonLibNpcOffsets::NumHeadParts);
#else
        return headpartsCount;
#endif
    }

    struct FaceMorphs
    {
        float option[19];
        uint32_t presets[4];

        void CopyFrom(const FaceMorphs& acRhs)
        {
            std::copy(std::begin(acRhs.option), std::end(acRhs.option), std::begin(option));
            std::copy(std::begin(acRhs.presets), std::end(acRhs.presets), std::begin(presets));
        }
    };

    struct HeadData
    {
        BGSColorForm* hairColor;
        BGSTextureSet* headTexture;
    };

    TESRaceForm raceForm;
    BGSOverridePackCollection overridePacks;
    void* unkDC;
    uint8_t unk0E0[0x24];
    uint8_t pad1B4[0x6];
    uint16_t unk10A;
    TESClass* npcClass;

    HeadData* headData;
    uintptr_t unk114;
    TESCombatStyle* combatStyle;
    size_t unk11C;
    TESRace* overlayRace;
    TESNPC* npcTemplate;
    float height;
    float weight;
    uintptr_t unk130;
    BSFixedString shortName;
    TESObjectARMO* farArmo;
    BGSOutfit* outfits[2];
    uintptr_t unk144;
    TESFaction* faction;

    BGSHeadPart** headparts;
    uint8_t headpartsCount;

#if TP_PLATFORM_64
    uint8_t pad241[5];
#else
    uint8_t pad151[3];
#endif

    struct Color
    {
        uint8_t red, green, blue;
    } color;

    GameArray<BGSRelationship*>* relationships;
    FaceMorphs* faceMorphs;
    uintptr_t unk160;

    BGSHeadPart* GetHeadPart(uint32_t aType);
    void Serialize(String* apSaveBuffer) const noexcept;
    void Deserialize(const String& acBuffer, uint32_t aChangeFlags) noexcept;
    void Initialize() noexcept;
};

static_assert(TESNPC::CommonLibNpcOffsets::RaceForm == 0x150);
static_assert(TESNPC::CommonLibNpcOffsets::NpcClass == 0x1C0);
static_assert(TESNPC::CommonLibNpcOffsets::HeadData == 0x1C8);
static_assert(TESNPC::CommonLibNpcOffsets::CombatStyle == 0x1D8);
static_assert(TESNPC::CommonLibNpcOffsets::OriginalRace == 0x1E8);
static_assert(TESNPC::CommonLibNpcOffsets::FaceNpc == 0x1F0);
static_assert(TESNPC::CommonLibNpcOffsets::DefaultOutfit == 0x218);
static_assert(TESNPC::CommonLibNpcOffsets::HeadParts == 0x238);
static_assert(TESNPC::CommonLibNpcOffsets::NumHeadParts == 0x240);
static_assert(TESNPC::CommonLibNpcOffsets::BodyTintColor == 0x246);
static_assert(TESNPC::CommonLibNpcOffsets::Relationships == 0x250);
static_assert(TESNPC::CommonLibNpcOffsets::FaceData == 0x258);
static_assert(TESNPC::CommonLibNpcOffsets::TintLayers == 0x260);
static_assert(TESNPC::CommonLibNpcOffsets::Size == 0x268);
static_assert(offsetof(TESNPC, raceForm) == TESNPC::LocalNpcOffsets::RaceForm);
static_assert(offsetof(TESNPC, npcClass) == TESNPC::LocalNpcOffsets::NpcClass);
static_assert(offsetof(TESNPC, headData) == TESNPC::LocalNpcOffsets::HeadData);
static_assert(offsetof(TESNPC, combatStyle) == TESNPC::LocalNpcOffsets::CombatStyle);
static_assert(offsetof(TESNPC, overlayRace) == TESNPC::LocalNpcOffsets::OriginalRace);
static_assert(offsetof(TESNPC, npcTemplate) == TESNPC::LocalNpcOffsets::FaceNpc);
static_assert(offsetof(TESNPC, outfits) == TESNPC::LocalNpcOffsets::DefaultOutfit);
static_assert(offsetof(TESNPC, headparts) == TESNPC::LocalNpcOffsets::HeadParts);
static_assert(offsetof(TESNPC, headpartsCount) == TESNPC::LocalNpcOffsets::NumHeadParts);
static_assert(offsetof(TESNPC, color) == TESNPC::LocalNpcOffsets::BodyTintColor);
static_assert(offsetof(TESNPC, relationships) == TESNPC::LocalNpcOffsets::Relationships);
static_assert(offsetof(TESNPC, faceMorphs) == TESNPC::LocalNpcOffsets::FaceData);
static_assert(offsetof(TESNPC, unk160) == TESNPC::LocalNpcOffsets::TintLayers);
static_assert(sizeof(TESNPC) == TESNPC::LocalNpcOffsets::Size);
