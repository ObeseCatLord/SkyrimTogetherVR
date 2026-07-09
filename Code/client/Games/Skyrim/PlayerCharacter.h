#pragma once

#include <cstddef>
#include <cstdint>

#include <Actor.h>
#include <Misc/TintMask.h>
#include <Forms/ActorValueInfo.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct BSFadeNode;
struct BSTriShape;
struct NiBillboardNode;
struct NiNode;

struct Skills
{
    using CommonLibPlayerSkillsOffsets = Skyrim::RuntimeLayout::PlayerSkillsCommonLibNgOffsets;
    using LocalPlayerSkillsOffsets = Skyrim::RuntimeLayout::PlayerSkillsLocalShimOffsets;

    enum Skill : std::uint32_t
    {
        kOneHanded = 0,
        kTwoHanded = 1,
        kArchery = 2,
        kBlock = 3,
        kSmithing = 4,
        kHeavyArmor = 5,
        kLightArmor = 6,
        kPickpocket = 7,
        kLockpicking = 8,
        kSneak = 9,
        kAlchemy = 10,
        kSpeech = 11,
        kAlteration = 12,
        kConjuration = 13,
        kDestruction = 14,
        kIllusion = 15,
        kRestoration = 16,
        kEnchanting = 17,
        kTotal
    };

    static const Skill GetSkillFromActorValue(int32_t aActorValue) noexcept
    {
        switch (aActorValue)
        {
        case ActorValueInfo::kOneHanded: return kOneHanded;
        case ActorValueInfo::kTwoHanded: return kTwoHanded;
        case ActorValueInfo::kMarksman: return kArchery;
        case ActorValueInfo::kBlock: return kBlock;
        case ActorValueInfo::kSmithing: return kSmithing;
        case ActorValueInfo::kHeavyArmor: return kHeavyArmor;
        case ActorValueInfo::kLightArmor: return kLightArmor;
        case ActorValueInfo::kPickpocket: return kPickpocket;
        case ActorValueInfo::kLockpicking: return kLockpicking;
        case ActorValueInfo::kSneak: return kSneak;
        case ActorValueInfo::kAlchemy: return kAlchemy;
        case ActorValueInfo::kSpeechcraft: return kSpeech;
        case ActorValueInfo::kAlteration: return kAlteration;
        case ActorValueInfo::kConjuration: return kConjuration;
        case ActorValueInfo::kDestruction: return kDestruction;
        case ActorValueInfo::kIllusion: return kIllusion;
        case ActorValueInfo::kRestoration: return kRestoration;
        case ActorValueInfo::kEnchanting: return kEnchanting;
        default: return kTotal;
        }
    }

    static const char* GetSkillString(Skill aSkill) noexcept
    {
        switch (aSkill)
        {
        case kOneHanded: return "One-handed";
        case kTwoHanded: return "Two-handed";
        case kArchery: return "Archery";
        case kBlock: return "Block";
        case kSmithing: return "Smithing";
        case kHeavyArmor: return "Heavy armor";
        case kLightArmor: return "Light armor";
        case kPickpocket: return "Pickpocket";
        case kLockpicking: return "Lockpicking";
        case kSneak: return "Sneak";
        case kAlchemy: return "Alchemy";
        case kSpeech: return "Speech";
        case kAlteration: return "Alteration";
        case kConjuration: return "Conjuration";
        case kDestruction: return "Destruction";
        case kIllusion: return "Illusion";
        case kRestoration: return "Restoration";
        case kEnchanting: return "Enchanting";
        default: return "UNKNOWN";
        }
    }

    struct SkillData
    {
        float level;
        float xp;
        float levelThreshold;
    };
    static_assert(sizeof(SkillData) == 0xC);

    float xp;
    float levelThreshold;
    SkillData skills[Skill::kTotal];
    uint32_t legendaryLevels[Skill::kTotal];

    [[nodiscard]] float GetGlobalXpData() const noexcept
    {
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibPlayerSkillsOffsets::GlobalXp);
    }

    [[nodiscard]] float GetGlobalLevelThresholdData() const noexcept
    {
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibPlayerSkillsOffsets::GlobalLevelThreshold);
    }

    [[nodiscard]] SkillData GetSkillData(Skill aSkill) const noexcept
    {
        if (aSkill >= Skill::kTotal)
            return {};

        const auto* pSkills = Skyrim::RuntimeLayout::Ptr<SkillData>(this, CommonLibPlayerSkillsOffsets::Skills);
        return pSkills[aSkill];
    }

    [[nodiscard]] uint32_t GetLegendaryLevelData(Skill aSkill) const noexcept
    {
        if (aSkill >= Skill::kTotal)
            return 0;

        const auto* pLegendaryLevels = Skyrim::RuntimeLayout::Ptr<uint32_t>(this, CommonLibPlayerSkillsOffsets::LegendaryLevels);
        return pLegendaryLevels[aSkill];
    }
};

static_assert(Skills::CommonLibPlayerSkillsOffsets::GlobalXp == 0x0);
static_assert(Skills::CommonLibPlayerSkillsOffsets::GlobalLevelThreshold == 0x4);
static_assert(Skills::CommonLibPlayerSkillsOffsets::Skills == 0x8);
static_assert(Skills::CommonLibPlayerSkillsOffsets::LegendaryLevels == 0xE0);
static_assert(Skills::CommonLibPlayerSkillsOffsets::Size == 0x128);
static_assert(offsetof(Skills, xp) == Skills::LocalPlayerSkillsOffsets::GlobalXp);
static_assert(offsetof(Skills, levelThreshold) == Skills::LocalPlayerSkillsOffsets::GlobalLevelThreshold);
static_assert(offsetof(Skills, skills) == Skills::LocalPlayerSkillsOffsets::Skills);
static_assert(offsetof(Skills, legendaryLevels) == Skills::LocalPlayerSkillsOffsets::LegendaryLevels);
static_assert(sizeof(Skills) == Skills::LocalPlayerSkillsOffsets::Size);

struct TESQuest;

struct PlayerCharacter : Actor
{
    using VrCommonLibOffsets = Skyrim::RuntimeLayout::PlayerCharacterVRCommonLibNgOffsets;
    using VrInfoRuntimeOffsets = Skyrim::RuntimeLayout::PlayerCharacterInfoRuntimeDataCommonLibNgOffsets;
    using VrGameStateOffsets = Skyrim::RuntimeLayout::PlayerCharacterGameStateDataCommonLibNgOffsets;
    using LocalPlayerOffsets = Skyrim::RuntimeLayout::PlayerCharacterLocalShimOffsets;

    static constexpr FormType Type = FormType::Character;
    static int32_t LastUsedCombatSkill;

    static PlayerCharacter* Get() noexcept;

    static void SetGodMode(bool aSet) noexcept;

    const GameArray<TintMask*>& GetTints() const noexcept;
    bool CanReadTintData() const noexcept;
    bool CanReadObjectiveData() const noexcept;
    Skills* GetSkillsData() const noexcept;
    int32_t* GetDifficultyData() noexcept;
    const int32_t* GetDifficultyData() const noexcept;

    // TODO: there's an in game function for this in fallout 4, maybe also for skyrim?
    void SetDifficulty(const int32_t aDifficulty, bool aForceUpdate = true, bool aExpectGameDataLoaded = true) noexcept;

    void AddSkillExperience(int32_t aSkill, float aExperience) noexcept;
    float GetSkillExperience(Skills::Skill aSkill) const noexcept;

    NiPoint3 RespawnPlayer() noexcept;

    void PayCrimeGoldToAllFactions() noexcept;

    void SetWaypoint(NiPoint3* apPosition, TESWorldSpace* apWorldSpace) noexcept;
    void RemoveWaypoint() noexcept;

    struct Objective
    {
        BSFixedString name;
        TESQuest* quest;
    };

    struct ObjectiveInstance
    {
        Objective* instance;
        uint64_t instanceCount;
    };

    const GameArray<ObjectiveInstance>& GetObjectives() const noexcept;

    struct PlayerSkillsRuntimeData
    {
        Skills* data;
    };

    struct VRNodeData
    {
        NiNode* PlayerWorldNode;              // 000 / PlayerCharacter + 3F0
        NiNode* FollowNode;                   // 008
        NiNode* FollowOffset;                 // 010
        NiNode* HeightOffsetNode;             // 018
        NiNode* SnapWalkOffsetNode;           // 020
        NiNode* RoomNode;                     // 028
        NiNode* BlackSphere;                  // 030
        NiNode* uiNode;                       // 038
        BSTriShape* InWorldUIQuadGeo;         // 040
        NiNode* UIPointerNode;                // 048
        BSTriShape* UIPointerGeo;             // 050
        NiNode* DialogueUINode;               // 058
        NiNode* TeleportDestinationPreview;   // 060
        NiNode* TeleportDestinationFail;      // 068
        NiNode* TeleportSprintPreview;        // 070
        NiNode* SpellOrigin;                  // 078
        NiNode* SpellDestination;             // 080
        NiNode* ArrowOrigin;                  // 088
        NiNode* ArrowDestination;             // 090
        NiNode* QuestMarker;                  // 098
        NiNode* LeftWandNode;                 // 0A0
        NiNode* LeftWandShakeNode;            // 0A8
        NiNode* LeftValveIndexControllerNode; // 0B0
        NiNode* unkNode4A8;                   // 0B8
        NiNode* LeftWeaponOffsetNode;         // 0C0
        NiNode* LeftCrossbowOffsetNode;       // 0C8
        NiNode* LeftMeleeWeaponOffsetNode;    // 0D0
        NiNode* LeftStaffWeaponOffsetNode;    // 0D8
        NiNode* LeftShieldOffsetNode;         // 0E0
        NiNode* RightShieldOffsetNode;        // 0E8
        NiNode* SecondaryMagicOffsetNode;     // 0F0
        NiNode* SecondaryMagicAimNode;        // 0F8
        NiNode* SecondaryStaffMagicOffsetNode; // 100
        NiNode* RightWandNode;                // 108
        NiNode* RightWandShakeNode;           // 110
        NiNode* RightValveIndexControllerNode; // 118
        NiNode* unkNode510;                   // 120
        NiNode* RightWeaponOffsetNode;        // 128
        NiNode* RightCrossbowOffsetNode;      // 130
        NiNode* RightMeleeWeaponOffsetNode;   // 138
        NiNode* RightStaffWeaponOffsetNode;   // 140
        NiNode* PrimaryMagicOffsetNode;       // 148
        NiNode* PrimaryMagicAimNode;          // 150
        NiNode* PrimaryStaffMagicOffsetNode;  // 158
        uint64_t unk550;                      // 160
        NiBillboardNode* CrosshairParent;     // 168
        NiBillboardNode* CrosshairSecondaryParent; // 170
        NiBillboardNode* TargetLockParent;    // 178
        NiNode* unkNode570;                   // 180
        NiNode* LastSyncPos;                  // 188
        NiNode* UprightHmdNode;               // 190
        NiNode* MapMarkers3D;                 // 198
        NiNode* NPCLHnd;                      // 1A0
        NiNode* NPCRHnd;                      // 1A8
        NiNode* NPCLClv;                      // 1B0
        NiNode* NPCRClv;                      // 1B8
        uint32_t unk5B0;                      // 1C0
        uint32_t unk5B4;                      // 1C4
        uint64_t unk5B8;                      // 1C8
        uint64_t unk5C0;                      // 1D0
        NiNode* BowAimNode;                   // 1D8
        NiNode* BowRotationNode;              // 1E0
        NiNode* ArrowSnapNode;                // 1E8
        BSFadeNode* ArrowNode;                // 1F0
        BSFadeNode* ArrowFireNode;            // 1F8
        uint64_t unk5F0;                      // 200
        NiNode* ArrowHoldOffsetNode;          // 208
        NiNode* ArrowHoldNode;                // 210
        uint64_t unk608;                      // 218
        float unkFloat610;                    // 220
        uint32_t unk614;                      // 224
        uint64_t unk618;                      // 228
        uint64_t unk620;                      // 230
        uint64_t unk628;                      // 238
        uint64_t unk630;                      // 240
        void* QuestMarkerBillBoardsNodeArray; // 248
        void* TeleportNodeArray;              // 250
        void* QuestMarkerBillBoardsNodeArray2; // 258
        uint64_t unk650;                      // 260
        void* TeleportNodeArray2;             // 268
        void* QuestMarkerBillBoardsNodeArray3; // 270
        uint64_t unk668;                      // 278
        float unkFloat670;                    // 280
        uint32_t unk674;                      // 284
        void* TeleportNodeArray3;             // 288
    };

    [[nodiscard]] VRNodeData* GetVRNodeData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<VRNodeData>(this, VrCommonLibOffsets::VRNodeData);
#else
        return nullptr;
#endif
    }

    [[nodiscard]] const VRNodeData* GetVRNodeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<VRNodeData>(this, VrCommonLibOffsets::VRNodeData);
#else
        return nullptr;
#endif
    }

    uint8_t pad1[0x588 - sizeof(Actor)];
    GameArray<ObjectiveInstance> objectives;
    uint8_t pad588[0x9B0 - 0x598];
    Skills** pSkills;
    uint8_t pad9B8[0xAC8 - 0x9B8];
    TESForm* locationForm;
    uint8_t padAC8[0x28];
    int32_t difficulty;
    uint8_t padAFC[0xB10 - 0xAFC];
    GameArray<TintMask*> baseTints;
    GameArray<TintMask*>* overlayTints;

    uint8_t padPlayerEnd[0xBE0 - 0xB30];
};

#if TP_SKYRIM_VR
static_assert(PlayerCharacter::VrCommonLibOffsets::InfoRuntimeData == 0xFD0);
static_assert(PlayerCharacter::VrCommonLibOffsets::SkillsPointer == 0x10B0);
static_assert(PlayerCharacter::VrCommonLibOffsets::GameStateData == 0x11F4);
static_assert(PlayerCharacter::VrCommonLibOffsets::VRNodeData == 0x3F0);
static_assert(PlayerCharacter::VrInfoRuntimeOffsets::Skills == 0xE0);
static_assert(PlayerCharacter::VrInfoRuntimeOffsets::Size == 0x150);
static_assert(PlayerCharacter::VrGameStateOffsets::Difficulty == 0x0);
static_assert(PlayerCharacter::VrGameStateOffsets::Size == 0xC);
static_assert(PlayerCharacter::VrCommonLibOffsets::InfoRuntimeData + PlayerCharacter::VrInfoRuntimeOffsets::Skills == PlayerCharacter::VrCommonLibOffsets::SkillsPointer);
static_assert(PlayerCharacter::VrCommonLibOffsets::GameStateData + PlayerCharacter::VrGameStateOffsets::Difficulty == 0x11F4);
static_assert(sizeof(PlayerCharacter::PlayerSkillsRuntimeData) == 0x8);
static_assert(offsetof(PlayerCharacter::VRNodeData, SpellOrigin) == 0x78);
static_assert(offsetof(PlayerCharacter::VRNodeData, ArrowOrigin) == 0x88);
static_assert(offsetof(PlayerCharacter::VRNodeData, LeftWandNode) == 0xA0);
static_assert(offsetof(PlayerCharacter::VRNodeData, LeftValveIndexControllerNode) == 0xB0);
static_assert(offsetof(PlayerCharacter::VRNodeData, LeftWeaponOffsetNode) == 0xC0);
static_assert(offsetof(PlayerCharacter::VRNodeData, SecondaryMagicOffsetNode) == 0xF0);
static_assert(offsetof(PlayerCharacter::VRNodeData, SecondaryMagicAimNode) == 0xF8);
static_assert(offsetof(PlayerCharacter::VRNodeData, RightWandNode) == 0x108);
static_assert(offsetof(PlayerCharacter::VRNodeData, RightValveIndexControllerNode) == 0x118);
static_assert(offsetof(PlayerCharacter::VRNodeData, RightWeaponOffsetNode) == 0x128);
static_assert(offsetof(PlayerCharacter::VRNodeData, PrimaryMagicOffsetNode) == 0x148);
static_assert(offsetof(PlayerCharacter::VRNodeData, PrimaryMagicAimNode) == 0x150);
static_assert(offsetof(PlayerCharacter::VRNodeData, UprightHmdNode) == 0x190);
static_assert(offsetof(PlayerCharacter::VRNodeData, BowAimNode) == 0x1D8);
static_assert(offsetof(PlayerCharacter::VRNodeData, BowRotationNode) == 0x1E0);
static_assert(offsetof(PlayerCharacter::VRNodeData, TeleportNodeArray3) == 0x288);
static_assert(sizeof(PlayerCharacter::VRNodeData) == 0x290);
#else
static_assert(offsetof(PlayerCharacter, objectives) == PlayerCharacter::LocalPlayerOffsets::Objectives);
static_assert(offsetof(PlayerCharacter, pSkills) == PlayerCharacter::LocalPlayerOffsets::SkillsPointer);
static_assert(offsetof(PlayerCharacter, locationForm) == PlayerCharacter::LocalPlayerOffsets::LocationForm);
static_assert(offsetof(PlayerCharacter, difficulty) == PlayerCharacter::LocalPlayerOffsets::Difficulty);
static_assert(offsetof(PlayerCharacter, baseTints) == PlayerCharacter::LocalPlayerOffsets::BaseTints);
static_assert(offsetof(PlayerCharacter, overlayTints) == PlayerCharacter::LocalPlayerOffsets::OverlayTints);
static_assert(sizeof(PlayerCharacter) == 0xBE8);
#endif
