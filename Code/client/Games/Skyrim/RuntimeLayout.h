#pragma once

#include <cstdint>
#include <cstring>

namespace Skyrim::RuntimeLayout
{
template <class T> [[nodiscard]] const T& Ref(const void* apObject, std::uintptr_t aOffset) noexcept
{
    return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(apObject) + aOffset);
}

template <class T> [[nodiscard]] T& Ref(void* apObject, std::uintptr_t aOffset) noexcept
{
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(apObject) + aOffset);
}

template <class T> [[nodiscard]] T Value(const void* apObject, std::uintptr_t aOffset) noexcept
{
    return *reinterpret_cast<T const*>(reinterpret_cast<const std::uint8_t*>(apObject) + aOffset);
}

template <class T> void Store(void* apObject, std::uintptr_t aOffset, T aValue) noexcept
{
    *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(apObject) + aOffset) = aValue;
}

template <class T> [[nodiscard]] T UnalignedValue(const void* apObject, std::uintptr_t aOffset) noexcept
{
    T value{};
    std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(apObject) + aOffset, sizeof(T));
    return value;
}

template <class T> void UnalignedStore(void* apObject, std::uintptr_t aOffset, T aValue) noexcept
{
    std::memcpy(reinterpret_cast<std::uint8_t*>(apObject) + aOffset, &aValue, sizeof(T));
}

template <class T> [[nodiscard]] const T* Ptr(const void* apObject, std::uintptr_t aOffset) noexcept
{
    return reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(apObject) + aOffset);
}

template <class T> [[nodiscard]] T* Ptr(void* apObject, std::uintptr_t aOffset) noexcept
{
    return reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(apObject) + aOffset);
}

struct TESObjectREFRCommonLibNgOffsets
{
    static constexpr std::uintptr_t ObjectReference = 0x40;
    static constexpr std::uintptr_t Rotation = 0x48;
    static constexpr std::uintptr_t Position = 0x54;
    static constexpr std::uintptr_t ParentCell = 0x60;
    static constexpr std::uintptr_t LoadedData = 0x68;
    static constexpr std::uintptr_t ExtraDataList = 0x70;
};

struct TESObjectREFRLocalShimOffsets
{
    static constexpr std::uintptr_t ObjectReference = 0x40;
    static constexpr std::uintptr_t Rotation = 0x48;
    static constexpr std::uintptr_t Position = 0x54;
    static constexpr std::uintptr_t ParentCell = 0x60;
    static constexpr std::uintptr_t LoadedData = 0x68;
    static constexpr std::uintptr_t ExtraDataList = 0x70;
};

struct TESFormCommonLibNgOffsets
{
    static constexpr std::uintptr_t SourceFiles = 0x8;
    static constexpr std::uintptr_t Flags = 0x10;
    static constexpr std::uintptr_t FormID = 0x14;
    static constexpr std::uintptr_t InGameFormFlags = 0x18;
    static constexpr std::uintptr_t FormType = 0x1A;
    static constexpr std::uintptr_t Size = 0x20;
};

struct TESFormLocalShimOffsets
{
    static constexpr std::uintptr_t SourceFiles = 0x8;
    static constexpr std::uintptr_t Flags = 0x10;
    static constexpr std::uintptr_t FormID = 0x14;
    static constexpr std::uintptr_t InGameFormFlags = 0x18;
    static constexpr std::uintptr_t FormType = 0x1A;
    static constexpr std::uintptr_t Size = 0x20;
};

struct TESFullNameCommonLibNgOffsets
{
    static constexpr std::uintptr_t Value = 0x8;
    static constexpr std::uintptr_t Size = 0x10;
};

struct TESFullNameLocalShimOffsets
{
    static constexpr std::uintptr_t Value = 0x8;
    static constexpr std::uintptr_t Size = 0x10;
};

struct BGSKeywordFormCommonLibNgOffsets
{
    static constexpr std::uintptr_t Keywords = 0x8;
    static constexpr std::uintptr_t Count = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct BGSKeywordFormLocalShimOffsets
{
    static constexpr std::uintptr_t Keywords = 0x8;
    static constexpr std::uintptr_t Count = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct TESGlobalCommonLibNgOffsets
{
    static constexpr std::uintptr_t Value = 0x34;
    static constexpr std::uintptr_t Size = 0x38;
};

struct TESGlobalLocalShimOffsets
{
    static constexpr std::uintptr_t Value = 0x34;
    static constexpr std::uintptr_t Size = 0x38;
};

struct CalendarCommonLibNgOffsets
{
    static constexpr std::uintptr_t GameYear = 0x8;
    static constexpr std::uintptr_t GameMonth = 0x10;
    static constexpr std::uintptr_t GameDay = 0x18;
    static constexpr std::uintptr_t GameHour = 0x20;
    static constexpr std::uintptr_t GameDaysPassed = 0x28;
    static constexpr std::uintptr_t TimeScale = 0x30;
    static constexpr std::uintptr_t MidnightsPassed = 0x38;
    static constexpr std::uintptr_t RawDaysPassed = 0x3C;
    static constexpr std::uintptr_t Size = 0x40;
};

struct TimeDataLocalShimOffsets
{
    static constexpr std::uintptr_t GameYear = 0x8;
    static constexpr std::uintptr_t GameMonth = 0x10;
    static constexpr std::uintptr_t GameDay = 0x18;
    static constexpr std::uintptr_t GameHour = 0x20;
    static constexpr std::uintptr_t GameDaysPassed = 0x28;
    static constexpr std::uintptr_t TimeScale = 0x30;
    static constexpr std::uintptr_t MidnightsPassed = 0x38;
    static constexpr std::uintptr_t RawDaysPassed = 0x3C;
    static constexpr std::uintptr_t Size = 0xC8;
};

struct TESCameraCommonLibNgOffsets
{
    static constexpr std::uintptr_t RotationInput = 0x8;
    static constexpr std::uintptr_t TranslationInput = 0x10;
    static constexpr std::uintptr_t ZoomInput = 0x1C;
    static constexpr std::uintptr_t CameraRoot = 0x20;
    static constexpr std::uintptr_t CurrentState = 0x28;
    static constexpr std::uintptr_t Enabled = 0x30;
    static constexpr std::uintptr_t Size = 0x38;
};

struct TESCameraLocalShimOffsets
{
    static constexpr std::uintptr_t RotationInput = 0x8;
    static constexpr std::uintptr_t TranslationInput = 0x10;
    static constexpr std::uintptr_t ZoomInput = 0x1C;
    static constexpr std::uintptr_t CameraRoot = 0x20;
    static constexpr std::uintptr_t CurrentState = 0x28;
    static constexpr std::uintptr_t Enabled = 0x30;
    static constexpr std::uintptr_t Size = 0x38;
};

struct PlayerCameraCommonLibNgOffsets
{
    static constexpr std::uintptr_t BaseSize = 0x38;
    static constexpr std::uintptr_t CameraTarget = 0x3C;
    static constexpr std::uintptr_t TempReturnStates = 0x40;
    static constexpr std::uintptr_t CameraStates = 0xB8;
    static constexpr std::uintptr_t WorldFOV = 0x13C;
    static constexpr std::uintptr_t FirstPersonFOV = 0x140;
    static constexpr std::uintptr_t Position = 0x144;
    static constexpr std::uintptr_t Yaw = 0x154;
    static constexpr std::uintptr_t Size = 0x168;
};

struct PlayerCameraLocalShimOffsets
{
    static constexpr std::uintptr_t BaseSize = 0x38;
    static constexpr std::uintptr_t RuntimeData = 0x38;
    static constexpr std::uintptr_t Size = 0x168;
};

struct NiCameraCommonLibNgOffsets
{
    static constexpr std::uintptr_t SERuntimeData = 0x110;
    static constexpr std::uintptr_t VRRuntimeData = 0x138;
    static constexpr std::uintptr_t WorldToCam = 0x0;
    static constexpr std::uintptr_t VRViewFrustumPtr = 0x40;
    static constexpr std::uintptr_t VRUnknownArrays = 0x48;
    static constexpr std::uintptr_t VRUnknown1C8 = 0x90;
    static constexpr std::uintptr_t SERuntimeData2 = 0x150;
    static constexpr std::uintptr_t VRRuntimeData2 = 0x1CC;
    static constexpr std::uintptr_t ViewFrustum = 0x0;
    static constexpr std::uintptr_t MinNearPlaneDist = 0x1C;
    static constexpr std::uintptr_t MaxFarNearRatio = 0x20;
    static constexpr std::uintptr_t Port = 0x24;
    static constexpr std::uintptr_t LodAdjust = 0x34;
    static constexpr std::uintptr_t SESize = 0x188;
    static constexpr std::uintptr_t VRSize = 0x208;
};

struct NiCameraLocalShimOffsets
{
    static constexpr std::uintptr_t SERuntimeData = 0x110;
    static constexpr std::uintptr_t VRRuntimeData = 0x138;
    static constexpr std::uintptr_t WorldToCam = 0x0;
    static constexpr std::uintptr_t VRViewFrustumPtr = 0x40;
    static constexpr std::uintptr_t VRUnknownArrays = 0x48;
    static constexpr std::uintptr_t VRUnknown1C8 = 0x90;
    static constexpr std::uintptr_t SERuntimeData2 = 0x150;
    static constexpr std::uintptr_t VRRuntimeData2 = 0x1CC;
    static constexpr std::uintptr_t ViewFrustum = 0x0;
    static constexpr std::uintptr_t MinNearPlaneDist = 0x1C;
    static constexpr std::uintptr_t MaxFarNearRatio = 0x20;
    static constexpr std::uintptr_t Port = 0x24;
    static constexpr std::uintptr_t LodAdjust = 0x34;
    static constexpr std::uintptr_t SESize = 0x188;
    static constexpr std::uintptr_t VRSize = 0x208;
};

struct TESActorBaseDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t Flags = 0x8;
    static constexpr std::uintptr_t Level = 0x10;
    static constexpr std::uintptr_t BaseTemplateForm = 0x30;
    static constexpr std::uintptr_t Factions = 0x40;
    static constexpr std::uintptr_t Size = 0x58;
};

struct TESActorBaseDataLocalShimOffsets
{
    static constexpr std::uintptr_t Flags = 0x8;
    static constexpr std::uintptr_t Level = 0x10;
    static constexpr std::uintptr_t Owner = 0x30;
    static constexpr std::uintptr_t Factions = 0x40;
    static constexpr std::uintptr_t Size = 0x58;
};

struct TESActorBaseCommonLibNgOffsets
{
    static constexpr std::uintptr_t ActorData = 0x30;
    static constexpr std::uintptr_t FullName = 0xD8;
    static constexpr std::uintptr_t KeywordForm = 0x110;
    static constexpr std::uintptr_t Size = 0x150;
};

struct TESActorBaseLocalShimOffsets
{
    static constexpr std::uintptr_t ActorData = 0x30;
    static constexpr std::uintptr_t FullName = 0xD8;
    static constexpr std::uintptr_t KeywordForm = 0x110;
    static constexpr std::uintptr_t Size = 0x150;
};

struct TESRaceFormCommonLibNgOffsets
{
    static constexpr std::uintptr_t Race = 0x8;
    static constexpr std::uintptr_t Size = 0x10;
};

struct TESRaceFormLocalShimOffsets
{
    static constexpr std::uintptr_t Race = 0x8;
    static constexpr std::uintptr_t Size = 0x10;
};

struct TESNPCCommonLibNgOffsets
{
    static constexpr std::uintptr_t RaceForm = 0x150;
    static constexpr std::uintptr_t NpcClass = 0x1C0;
    static constexpr std::uintptr_t HeadData = 0x1C8;
    static constexpr std::uintptr_t CombatStyle = 0x1D8;
    static constexpr std::uintptr_t OriginalRace = 0x1E8;
    static constexpr std::uintptr_t FaceNpc = 0x1F0;
    static constexpr std::uintptr_t Height = 0x1F8;
    static constexpr std::uintptr_t Weight = 0x1FC;
    static constexpr std::uintptr_t ShortName = 0x208;
    static constexpr std::uintptr_t FarSkin = 0x210;
    static constexpr std::uintptr_t DefaultOutfit = 0x218;
    static constexpr std::uintptr_t SleepOutfit = 0x220;
    static constexpr std::uintptr_t CrimeFaction = 0x230;
    static constexpr std::uintptr_t HeadParts = 0x238;
    static constexpr std::uintptr_t NumHeadParts = 0x240;
    static constexpr std::uintptr_t BodyTintColor = 0x246;
    static constexpr std::uintptr_t Relationships = 0x250;
    static constexpr std::uintptr_t FaceData = 0x258;
    static constexpr std::uintptr_t TintLayers = 0x260;
    static constexpr std::uintptr_t Size = 0x268;
};

struct TESNPCLocalShimOffsets
{
    static constexpr std::uintptr_t RaceForm = 0x150;
    static constexpr std::uintptr_t NpcClass = 0x1C0;
    static constexpr std::uintptr_t HeadData = 0x1C8;
    static constexpr std::uintptr_t CombatStyle = 0x1D8;
    static constexpr std::uintptr_t OriginalRace = 0x1E8;
    static constexpr std::uintptr_t FaceNpc = 0x1F0;
    static constexpr std::uintptr_t Height = 0x1F8;
    static constexpr std::uintptr_t Weight = 0x1FC;
    static constexpr std::uintptr_t ShortName = 0x208;
    static constexpr std::uintptr_t FarSkin = 0x210;
    static constexpr std::uintptr_t DefaultOutfit = 0x218;
    static constexpr std::uintptr_t SleepOutfit = 0x220;
    static constexpr std::uintptr_t CrimeFaction = 0x230;
    static constexpr std::uintptr_t HeadParts = 0x238;
    static constexpr std::uintptr_t NumHeadParts = 0x240;
    static constexpr std::uintptr_t BodyTintColor = 0x246;
    static constexpr std::uintptr_t Relationships = 0x250;
    static constexpr std::uintptr_t FaceData = 0x258;
    static constexpr std::uintptr_t TintLayers = 0x260;
    static constexpr std::uintptr_t Size = 0x268;
};

struct BGSOutfitCommonLibNgOffsets
{
    static constexpr std::uintptr_t OutfitItems = 0x20;
    static constexpr std::uintptr_t Size = 0x38;
};

struct BGSOutfitLocalShimOffsets
{
    static constexpr std::uintptr_t OutfitItems = 0x20;
    static constexpr std::uintptr_t Size = 0x38;
};

struct TESLeveledListCommonLibNgOffsets
{
    static constexpr std::uintptr_t EntryForm = 0x0;
    static constexpr std::uintptr_t EntryCount = 0x8;
    static constexpr std::uintptr_t EntryLevel = 0xA;
    static constexpr std::uintptr_t EntryExtra = 0x10;
    static constexpr std::uintptr_t EntrySize = 0x18;
    static constexpr std::uintptr_t Entries = 0x8;
    static constexpr std::uintptr_t ChanceNone = 0x10;
    static constexpr std::uintptr_t Flags = 0x11;
    static constexpr std::uintptr_t NumEntries = 0x12;
    static constexpr std::uintptr_t Size = 0x28;
};

struct TESLeveledListLocalShimOffsets
{
    static constexpr std::uintptr_t EntryForm = 0x0;
    static constexpr std::uintptr_t EntryCount = 0x8;
    static constexpr std::uintptr_t EntryLevel = 0xA;
    static constexpr std::uintptr_t EntryExtra = 0x10;
    static constexpr std::uintptr_t EntrySize = 0x18;
    static constexpr std::uintptr_t Entries = 0x8;
    static constexpr std::uintptr_t ChanceNone = 0x10;
    static constexpr std::uintptr_t Flags = 0x11;
    static constexpr std::uintptr_t NumEntries = 0x12;
    static constexpr std::uintptr_t Size = 0x28;
};

struct BGSBipedObjectFormCommonLibNgOffsets
{
    static constexpr std::uintptr_t SlotMask = 0x8;
    static constexpr std::uintptr_t ArmorType = 0xC;
    static constexpr std::uintptr_t Size = 0x10;
};

struct BGSBipedObjectFormLocalShimOffsets
{
    static constexpr std::uintptr_t SlotMask = 0x8;
    static constexpr std::uintptr_t ArmorType = 0xC;
    static constexpr std::uintptr_t Size = 0x10;
};

struct TESObjectARMOCommonLibNgOffsets
{
    static constexpr std::uintptr_t BipedObjectForm = 0x1B0;
    static constexpr std::uintptr_t SlotMask = 0x1B8;
    static constexpr std::uintptr_t Size = 0x228;
};

struct TESObjectARMOLocalShimOffsets
{
    static constexpr std::uintptr_t SlotMask = 0x1B8;
};

struct EffectSettingCommonLibNgOffsets
{
    static constexpr std::uintptr_t FullName = 0x20;
    static constexpr std::uintptr_t KeywordForm = 0x40;
    static constexpr std::uintptr_t Data = 0x68;
    static constexpr std::uintptr_t Archetype = 0xC0;
    static constexpr std::uintptr_t Size = 0x198;
};

struct EffectSettingLocalShimOffsets
{
    static constexpr std::uintptr_t FullName = 0x20;
    static constexpr std::uintptr_t KeywordForm = 0x40;
    static constexpr std::uintptr_t Archetype = 0xC0;
};

struct MagicItemCommonLibNgOffsets
{
    static constexpr std::uintptr_t FullName = 0x30;
    static constexpr std::uintptr_t KeywordForm = 0x40;
    static constexpr std::uintptr_t Effects = 0x58;
    static constexpr std::uintptr_t HostileCount = 0x70;
    static constexpr std::uintptr_t AVEffectSetting = 0x78;
    static constexpr std::uintptr_t Size = 0x90;
};

struct MagicItemLocalShimOffsets
{
    static constexpr std::uintptr_t FullName = 0x30;
    static constexpr std::uintptr_t KeywordForm = 0x40;
    static constexpr std::uintptr_t Effects = 0x58;
    static constexpr std::uintptr_t HostileCount = 0x70;
    static constexpr std::uintptr_t AVEffectSetting = 0x78;
    static constexpr std::uintptr_t Size = 0x90;
};

struct SpellItemCommonLibNgOffsets
{
    static constexpr std::uintptr_t EquipType = 0x90;
    static constexpr std::uintptr_t MenuDisplayObject = 0xA0;
    static constexpr std::uintptr_t Description = 0xB0;
    static constexpr std::uintptr_t Data = 0xC0;
    static constexpr std::uintptr_t CostOverride = 0xC0;
    static constexpr std::uintptr_t Flags = 0xC4;
    static constexpr std::uintptr_t SpellType = 0xC8;
    static constexpr std::uintptr_t ChargeTime = 0xCC;
    static constexpr std::uintptr_t CastingType = 0xD0;
    static constexpr std::uintptr_t Delivery = 0xD4;
    static constexpr std::uintptr_t CastDuration = 0xD8;
    static constexpr std::uintptr_t Range = 0xDC;
    static constexpr std::uintptr_t CastingPerk = 0xE0;
    static constexpr std::uintptr_t Size = 0xE8;
};

struct SpellItemLocalShimOffsets
{
    static constexpr std::uintptr_t EquipType = 0x90;
    static constexpr std::uintptr_t MenuDisplayObject = 0xA0;
    static constexpr std::uintptr_t Description = 0xB0;
    static constexpr std::uintptr_t Data = 0xC0;
    static constexpr std::uintptr_t CostOverride = 0xC0;
    static constexpr std::uintptr_t Flags = 0xC4;
    static constexpr std::uintptr_t SpellType = 0xC8;
    static constexpr std::uintptr_t ChargeTime = 0xCC;
    static constexpr std::uintptr_t CastingType = 0xD0;
    static constexpr std::uintptr_t Delivery = 0xD4;
    static constexpr std::uintptr_t CastDuration = 0xD8;
    static constexpr std::uintptr_t Range = 0xDC;
    static constexpr std::uintptr_t CastingPerk = 0xE0;
    static constexpr std::uintptr_t Size = 0xE8;
};

struct EnchantmentItemCommonLibNgOffsets
{
    static constexpr std::uintptr_t Data = 0x90;
    static constexpr std::uintptr_t CostOverride = 0x90;
    static constexpr std::uintptr_t Flags = 0x94;
    static constexpr std::uintptr_t CastingType = 0x98;
    static constexpr std::uintptr_t ChargeOverride = 0x9C;
    static constexpr std::uintptr_t Delivery = 0xA0;
    static constexpr std::uintptr_t SpellType = 0xA4;
    static constexpr std::uintptr_t ChargeTime = 0xA8;
    static constexpr std::uintptr_t BaseEnchantment = 0xB0;
    static constexpr std::uintptr_t WornRestrictions = 0xB8;
    static constexpr std::uintptr_t Size = 0xC0;
};

struct EnchantmentItemLocalShimOffsets
{
    static constexpr std::uintptr_t Data = 0x90;
    static constexpr std::uintptr_t CostOverride = 0x90;
    static constexpr std::uintptr_t Flags = 0x94;
    static constexpr std::uintptr_t CastingType = 0x98;
    static constexpr std::uintptr_t ChargeOverride = 0x9C;
    static constexpr std::uintptr_t Delivery = 0xA0;
    static constexpr std::uintptr_t SpellType = 0xA4;
    static constexpr std::uintptr_t ChargeTime = 0xA8;
    static constexpr std::uintptr_t BaseEnchantment = 0xB0;
    static constexpr std::uintptr_t WornRestrictions = 0xB8;
    static constexpr std::uintptr_t Size = 0xC0;
};

struct EffectItemCommonLibNgOffsets
{
    static constexpr std::uintptr_t Magnitude = 0x0;
    static constexpr std::uintptr_t Area = 0x4;
    static constexpr std::uintptr_t Duration = 0x8;
    static constexpr std::uintptr_t EffectSetting = 0x10;
    static constexpr std::uintptr_t RawCost = 0x18;
    static constexpr std::uintptr_t Size = 0x28;
};

struct EffectItemLocalShimOffsets
{
    static constexpr std::uintptr_t Magnitude = 0x0;
    static constexpr std::uintptr_t Area = 0x4;
    static constexpr std::uintptr_t Duration = 0x8;
    static constexpr std::uintptr_t EffectSetting = 0x10;
    static constexpr std::uintptr_t RawCost = 0x18;
    static constexpr std::uintptr_t Size = 0x28;
};

struct MagicCasterCommonLibNgOffsets
{
    static constexpr std::uintptr_t Sounds = 0x8;
    static constexpr std::uintptr_t DesiredTarget = 0x20;
    static constexpr std::uintptr_t CurrentSpell = 0x28;
    static constexpr std::uintptr_t State = 0x30;
    static constexpr std::uintptr_t CastingTimer = 0x34;
    static constexpr std::uintptr_t CurrentSpellCost = 0x38;
    static constexpr std::uintptr_t MagnitudeOverride = 0x3C;
    static constexpr std::uintptr_t NextTargetUpdate = 0x40;
    static constexpr std::uintptr_t ProjectileTimer = 0x44;
    static constexpr std::uintptr_t Size = 0x48;
};

struct MagicCasterLocalShimOffsets
{
    static constexpr std::uintptr_t Sounds = 0x8;
    static constexpr std::uintptr_t DesiredTarget = 0x20;
    static constexpr std::uintptr_t CurrentSpell = 0x28;
    static constexpr std::uintptr_t State = 0x30;
    static constexpr std::uintptr_t CastingTimer = 0x34;
    static constexpr std::uintptr_t CurrentSpellCost = 0x38;
    static constexpr std::uintptr_t MagnitudeOverride = 0x3C;
    static constexpr std::uintptr_t NextTargetUpdate = 0x40;
    static constexpr std::uintptr_t ProjectileTimer = 0x44;
    static constexpr std::uintptr_t Size = 0x48;
};

struct ActorMagicCasterCommonLibNgOffsets
{
    static constexpr std::uintptr_t CasterActor = 0xB8;
    static constexpr std::uintptr_t MagicNode = 0xC0;
    static constexpr std::uintptr_t Light = 0xC8;
    static constexpr std::uintptr_t InterruptHandler = 0xD0;
    static constexpr std::uintptr_t LoadGameSubBuffer = 0xD8;
    static constexpr std::uintptr_t CastingArt = 0xE0;
    static constexpr std::uintptr_t WeaponEnchantmentController = 0xE8;
    static constexpr std::uintptr_t CostCharged = 0xF0;
    static constexpr std::uintptr_t CastingSource = 0xF4;
    static constexpr std::uintptr_t Flags = 0xF8;
    static constexpr std::uintptr_t Size = 0x100;
};

struct ActorMagicCasterLocalShimOffsets
{
    static constexpr std::uintptr_t CasterActor = 0xB8;
    static constexpr std::uintptr_t MagicNode = 0xC0;
    static constexpr std::uintptr_t Light = 0xC8;
    static constexpr std::uintptr_t InterruptHandler = 0xD0;
    static constexpr std::uintptr_t LoadGameSubBuffer = 0xD8;
    static constexpr std::uintptr_t CastingArt = 0xE0;
    static constexpr std::uintptr_t WeaponEnchantmentController = 0xE8;
    static constexpr std::uintptr_t CostCharged = 0xF0;
    static constexpr std::uintptr_t CastingSource = 0xF4;
    static constexpr std::uintptr_t Flags = 0xF8;
    static constexpr std::uintptr_t Size = 0x100;
};

struct ActiveEffectCommonLibNgOffsets
{
    static constexpr std::uintptr_t Caster = 0x34;
    static constexpr std::uintptr_t SourceNode = 0x38;
    static constexpr std::uintptr_t Spell = 0x40;
    static constexpr std::uintptr_t Effect = 0x48;
    static constexpr std::uintptr_t Target = 0x50;
    static constexpr std::uintptr_t Source = 0x58;
    static constexpr std::uintptr_t HitEffects = 0x60;
    static constexpr std::uintptr_t DisplacementSpell = 0x68;
    static constexpr std::uintptr_t ElapsedSeconds = 0x70;
    static constexpr std::uintptr_t Duration = 0x74;
    static constexpr std::uintptr_t Magnitude = 0x78;
    static constexpr std::uintptr_t Flags = 0x7C;
    static constexpr std::uintptr_t ConditionStatus = 0x80;
    static constexpr std::uintptr_t UniqueID = 0x84;
    static constexpr std::uintptr_t CastingSource = 0x88;
    static constexpr std::uintptr_t Size = 0x90;
};

struct ActiveEffectLocalShimOffsets
{
    static constexpr std::uintptr_t Caster = 0x34;
    static constexpr std::uintptr_t SourceNode = 0x38;
    static constexpr std::uintptr_t Spell = 0x40;
    static constexpr std::uintptr_t Effect = 0x48;
    static constexpr std::uintptr_t Target = 0x50;
    static constexpr std::uintptr_t Source = 0x58;
    static constexpr std::uintptr_t HitEffects = 0x60;
    static constexpr std::uintptr_t DisplacementSpell = 0x68;
    static constexpr std::uintptr_t ElapsedSeconds = 0x70;
    static constexpr std::uintptr_t Duration = 0x74;
    static constexpr std::uintptr_t Magnitude = 0x78;
    static constexpr std::uintptr_t Flags = 0x7C;
    static constexpr std::uintptr_t ConditionStatus = 0x80;
    static constexpr std::uintptr_t UniqueID = 0x84;
    static constexpr std::uintptr_t CastingSource = 0x88;
    static constexpr std::uintptr_t Size = 0x90;
};

struct ValueModifierEffectCommonLibNgOffsets
{
    static constexpr std::uintptr_t ActorValue = 0x90;
    static constexpr std::uintptr_t Value = 0x94;
    static constexpr std::uintptr_t Size = 0x98;
};

struct ValueModifierEffectLocalShimOffsets
{
    static constexpr std::uintptr_t ActorValue = 0x90;
    static constexpr std::uintptr_t Value = 0x94;
    static constexpr std::uintptr_t Size = 0x98;
};

struct MagicTargetAddTargetDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t Caster = 0x0;
    static constexpr std::uintptr_t MagicItem = 0x8;
    static constexpr std::uintptr_t Effect = 0x10;
    static constexpr std::uintptr_t Source = 0x18;
    static constexpr std::uintptr_t PostCreationCallback = 0x20;
    static constexpr std::uintptr_t ResultsCollector = 0x28;
    static constexpr std::uintptr_t ExplosionLocation = 0x30;
    static constexpr std::uintptr_t Magnitude = 0x3C;
    static constexpr std::uintptr_t Unk40 = 0x40;
    static constexpr std::uintptr_t CastingSource = 0x44;
    static constexpr std::uintptr_t AreaTarget = 0x48;
    static constexpr std::uintptr_t DualCast = 0x49;
    static constexpr std::uintptr_t Size = 0x50;
};

struct MagicTargetAddTargetDataLocalShimOffsets
{
    static constexpr std::uintptr_t Caster = 0x0;
    static constexpr std::uintptr_t MagicItem = 0x8;
    static constexpr std::uintptr_t Effect = 0x10;
    static constexpr std::uintptr_t Source = 0x18;
    static constexpr std::uintptr_t PostCreationCallback = 0x20;
    static constexpr std::uintptr_t ResultsCollector = 0x28;
    static constexpr std::uintptr_t ExplosionLocation = 0x30;
    static constexpr std::uintptr_t Magnitude = 0x3C;
    static constexpr std::uintptr_t Unk40 = 0x40;
    static constexpr std::uintptr_t CastingSource = 0x44;
    static constexpr std::uintptr_t AreaTarget = 0x48;
    static constexpr std::uintptr_t DualCast = 0x49;
    static constexpr std::uintptr_t Size = 0x50;
};

struct TESQuestCommonLibNgOffsets
{
    static constexpr std::uintptr_t FullName = 0x28;
    static constexpr std::uintptr_t Flags = 0xDC;
    static constexpr std::uintptr_t Priority = 0xDE;
    static constexpr std::uintptr_t Type = 0xDF;
    static constexpr std::uintptr_t CurrentStage = 0x228;
    static constexpr std::uintptr_t Size = 0x268;
};

struct TESQuestLocalShimOffsets
{
    static constexpr std::uintptr_t FullName = 0x28;
    static constexpr std::uintptr_t Flags = 0xDC;
    static constexpr std::uintptr_t Priority = 0xDE;
    static constexpr std::uintptr_t Type = 0xDF;
    static constexpr std::uintptr_t CurrentStage = 0x228;
    static constexpr std::uintptr_t Size = 0x268;
};

struct TESWorldSpaceCommonLibNgOffsets
{
    static constexpr std::uintptr_t FullName = 0x20;
    static constexpr std::uintptr_t Size = 0x358;
};

struct TESWorldSpaceLocalShimOffsets
{
    static constexpr std::uintptr_t FullName = 0x20;
};

struct TESObjectCELLCommonLibNgOffsets
{
    static constexpr std::uintptr_t CellFlags = 0x40;
    static constexpr std::uintptr_t References = 0x80;
    static constexpr std::uintptr_t WorldSpace = 0x120;
    static constexpr std::uintptr_t LoadedCellData = 0x128;
};

struct TESObjectCELLLocalShimOffsets
{
    static constexpr std::uintptr_t CellFlags = 0x40;
    static constexpr std::uintptr_t References = 0x88;
    static constexpr std::uintptr_t WorldSpace = 0x128;
    static constexpr std::uintptr_t LoadedCellData = 0x130;
};

struct LoadedCellDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t EncounterZone = 0x160;
    static constexpr std::uintptr_t Size = 0x180;
};

struct LoadedCellDataLocalShimOffsets
{
    static constexpr std::uintptr_t EncounterZone = 0x160;
    static constexpr std::uintptr_t Size = 0x180;
};

struct EquipDataLocalShimOffsets
{
    static constexpr std::uintptr_t ExtraDataList = 0x0;
    static constexpr std::uintptr_t Count = 0x8;
    static constexpr std::uintptr_t Slot = 0x10;
    static constexpr std::uintptr_t SlotToReplace = 0x18;
    static constexpr std::uintptr_t QueueEquip = 0x20;
    static constexpr std::uintptr_t ForceEquip = 0x21;
    static constexpr std::uintptr_t PlaySound = 0x22;
    static constexpr std::uintptr_t ApplyNow = 0x23;
    static constexpr std::uintptr_t Unk24 = 0x24;
    static constexpr std::uintptr_t Size = 0x28;
};

struct MagicEquipDataLocalShimOffsets
{
    static constexpr std::uintptr_t EquipSlot = 0x0;
    static constexpr std::uintptr_t QueueEquip = 0x8;
    static constexpr std::uintptr_t ForceEquip = 0x9;
    static constexpr std::uintptr_t Size = 0x10;
};

struct ShoutEquipDataLocalShimOffsets
{
    static constexpr std::uintptr_t Unk1 = 0x0;
    static constexpr std::uintptr_t Unk2 = 0x8;
    static constexpr std::uintptr_t Size = 0x10;
};

struct TESCommonLibNgOffsets
{
    static constexpr std::uintptr_t GridCells = 0x78;
    static constexpr std::uintptr_t CenterGridX = 0xB0;
    static constexpr std::uintptr_t CenterGridY = 0xB4;
    static constexpr std::uintptr_t CurrentGridX = 0xB8;
    static constexpr std::uintptr_t CurrentGridY = 0xBC;
    static constexpr std::uintptr_t InteriorCell = 0xC0;
    static constexpr std::uintptr_t InteriorBuffer = 0xC8;
    static constexpr std::uintptr_t ExteriorBuffer = 0xD0;
    static constexpr std::uintptr_t ActiveImageSpaceModifiers = 0x108;
};

struct TESLocalShimOffsets
{
    static constexpr std::uintptr_t GridCells = 0x78;
    static constexpr std::uintptr_t CenterGridX = 0xB0;
    static constexpr std::uintptr_t CenterGridY = 0xB4;
    static constexpr std::uintptr_t CurrentGridX = 0xB8;
    static constexpr std::uintptr_t CurrentGridY = 0xBC;
    static constexpr std::uintptr_t InteriorCell = 0xC0;
    static constexpr std::uintptr_t InteriorBuffer = 0xC8;
    static constexpr std::uintptr_t ExteriorBuffer = 0xD0;
    static constexpr std::uintptr_t ActiveImageSpaceModifiers = 0x108;
};

struct AIProcessCommonLibNgOffsets
{
    static constexpr std::uintptr_t MiddleProcess = 0x8;
};

struct AIProcessLocalShimOffsets
{
    static constexpr std::uintptr_t MiddleProcess = 0x8;
};

struct MiddleProcessCommonLibNgOffsets
{
    static constexpr std::uintptr_t Direction = 0xB8;
    static constexpr std::uintptr_t ActiveEffects = 0x1A0;
    static constexpr std::uintptr_t CommandingActor = 0x218;
    static constexpr std::uintptr_t LeftEquippedObject = 0x220;
    static constexpr std::uintptr_t RightEquippedObject = 0x260;
    static constexpr std::uintptr_t BothHandsEquippedObject = 0x268;
};

struct MiddleProcessLocalShimOffsets
{
    static constexpr std::uintptr_t Direction = 0xB8;
    static constexpr std::uintptr_t ActiveEffects = 0x1A0;
    static constexpr std::uintptr_t CommandingActor = 0x218;
    static constexpr std::uintptr_t LeftEquippedObject = 0x220;
    static constexpr std::uintptr_t RightEquippedObject = 0x260;
    static constexpr std::uintptr_t BothHandsEquippedObject = 0x268;
};

struct PlayerCharacterInfoRuntimeDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t Skills = 0xE0;
    static constexpr std::uintptr_t Size = 0x150;
};

struct PlayerCharacterGameStateDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t Difficulty = 0x0;
    static constexpr std::uintptr_t Size = 0xC;
};

struct PlayerSkillsCommonLibNgOffsets
{
    static constexpr std::uintptr_t GlobalXp = 0x0;
    static constexpr std::uintptr_t GlobalLevelThreshold = 0x4;
    static constexpr std::uintptr_t Skills = 0x8;
    static constexpr std::uintptr_t LegendaryLevels = 0xE0;
    static constexpr std::uintptr_t Size = 0x128;
};

struct PlayerSkillsLocalShimOffsets
{
    static constexpr std::uintptr_t GlobalXp = 0x0;
    static constexpr std::uintptr_t GlobalLevelThreshold = 0x4;
    static constexpr std::uintptr_t Skills = 0x8;
    static constexpr std::uintptr_t LegendaryLevels = 0xE0;
    static constexpr std::uintptr_t Size = 0x128;
};

struct PlayerCharacterVRCommonLibNgOffsets
{
    static constexpr std::uintptr_t VRNodeData = 0x3F0;
    static constexpr std::uintptr_t InfoRuntimeData = 0xFD0;
    static constexpr std::uintptr_t SkillsPointer = 0x10B0;
    static constexpr std::uintptr_t GameStateData = 0x11F4;
};

struct PlayerCharacterLocalShimOffsets
{
    static constexpr std::uintptr_t Objectives = 0x588;
    static constexpr std::uintptr_t SkillsPointer = 0x9B8;
    static constexpr std::uintptr_t LocationForm = 0xAD0;
    static constexpr std::uintptr_t Difficulty = 0xB00;
    static constexpr std::uintptr_t BaseTints = 0xB18;
    static constexpr std::uintptr_t OverlayTints = 0xB30;
};

struct ActorCommonLibNgOffsets
{
    static constexpr std::uintptr_t MagicTarget = 0x98;
    static constexpr std::uintptr_t ActorValueOwner = 0xB0;
    static constexpr std::uintptr_t ActorState = 0xB8;
    static constexpr std::uintptr_t BoolBits = 0xE0;
    static constexpr std::uintptr_t CurrentProcess = 0xF0;
    static constexpr std::uintptr_t CombatController = 0x158;
    static constexpr std::uintptr_t MagicCasters = 0x1A0;
    static constexpr std::uintptr_t SelectedSpells = 0x1C0;
    static constexpr std::uintptr_t SelectedPower = 0x1E0;
    static constexpr std::uintptr_t Race = 0x1F0;
    static constexpr std::uintptr_t BoolFlags = 0x1FC;
    static constexpr std::uintptr_t HealthModifiers = 0x228;
    static constexpr std::uintptr_t MagickaModifiers = 0x234;
    static constexpr std::uintptr_t StaminaModifiers = 0x240;
    static constexpr std::uintptr_t VoiceModifiers = 0x24C;
};

struct ActorLocalShimOffsets
{
    static constexpr std::uintptr_t MagicTarget = 0xA0;
    static constexpr std::uintptr_t ActorValueOwner = 0xB8;
    static constexpr std::uintptr_t ActorState = 0xC0;
    static constexpr std::uintptr_t BoolBits = 0xE8;
    static constexpr std::uintptr_t CurrentProcess = 0xF8;
    static constexpr std::uintptr_t CombatController = 0x160;
    static constexpr std::uintptr_t MagicCasters = 0x1A8;
    static constexpr std::uintptr_t SelectedSpells = 0x1C8;
    static constexpr std::uintptr_t SelectedPower = 0x1E8;
    static constexpr std::uintptr_t Race = 0x1F8;
    static constexpr std::uintptr_t BoolFlags = 0x204;
    static constexpr std::uintptr_t HealthModifiers = 0x230;
    static constexpr std::uintptr_t StaminaModifiers = 0x23C;
    static constexpr std::uintptr_t MagickaModifiers = 0x248;
    static constexpr std::uintptr_t VoiceModifiers = 0x254;
};

struct CombatControllerCommonLibNgOffsets
{
    static constexpr std::uintptr_t CombatGroup = 0x0;
    static constexpr std::uintptr_t Inventory = 0x10;
    static constexpr std::uintptr_t AttackerHandle = 0x28;
    static constexpr std::uintptr_t TargetHandle = 0x2C;
    static constexpr std::uintptr_t StartedCombat = 0x35;
    static constexpr std::uintptr_t TargetSelectors = 0x98;
    static constexpr std::uintptr_t ActiveTargetSelector = 0xB0;
    static constexpr std::uintptr_t CachedAttacker = 0xC8;
    static constexpr std::uintptr_t Size = 0xD8;
};

struct CombatControllerLocalShimOffsets
{
    static constexpr std::uintptr_t CombatGroup = 0x0;
    static constexpr std::uintptr_t Inventory = 0x10;
    static constexpr std::uintptr_t AttackerHandle = 0x28;
    static constexpr std::uintptr_t TargetHandle = 0x2C;
    static constexpr std::uintptr_t StartedCombat = 0x35;
    static constexpr std::uintptr_t TargetSelectors = 0xA0;
    static constexpr std::uintptr_t ActiveTargetSelector = 0xB8;
    static constexpr std::uintptr_t CachedAttacker = 0xD0;
    static constexpr std::uintptr_t Size = 0xE0;
};

struct CombatTargetCommonLibNgOffsets
{
    static constexpr std::uintptr_t TargetHandle = 0x0;
    static constexpr std::uintptr_t AttackerCount = 0xA4;
    static constexpr std::uintptr_t Flags = 0xA6;
    static constexpr std::uintptr_t Size = 0xA8;
};

struct CombatTargetLocalShimOffsets
{
    static constexpr std::uintptr_t TargetHandle = 0x0;
    static constexpr std::uintptr_t AttackerCount = 0xA4;
    static constexpr std::uintptr_t Flags = 0xA6;
    static constexpr std::uintptr_t Size = 0xA8;
};

struct CombatGroupCommonLibNgOffsets
{
    static constexpr std::uintptr_t Targets = 0x8;
    static constexpr std::uintptr_t Size = 0x168;
};

struct CombatGroupLocalShimOffsets
{
    static constexpr std::uintptr_t Targets = 0x8;
    static constexpr std::uintptr_t Size = 0x168;
};

struct CombatInventoryCommonLibNgOffsets
{
    static constexpr std::uintptr_t MaximumRange = 0x1B8;
    static constexpr std::uintptr_t Size = 0x1C8;
};

struct CombatInventoryLocalShimOffsets
{
    static constexpr std::uintptr_t MaximumRange = 0x1B8;
    static constexpr std::uintptr_t Size = 0x1C8;
};

struct CombatTargetSelectorLocalShimOffsets
{
    static constexpr std::uintptr_t Controller = 0x10;
    static constexpr std::uintptr_t TargetHandle = 0x18;
    static constexpr std::uintptr_t Priority = 0x1C;
    static constexpr std::uintptr_t Flags = 0x20;
    static constexpr std::uintptr_t Size = 0x28;
    static constexpr std::uintptr_t StandardSize = 0x30;
};

struct BSScriptIVirtualMachineCommonLibNgSlots
{
    static constexpr std::uintptr_t GetScriptObjectType1 = 0x9;
    static constexpr std::uintptr_t BindNativeMethod = 0x18;
    static constexpr std::uintptr_t SendEvent = 0x24;
    static constexpr std::uintptr_t GetObjectHandlePolicy = 0x2D;
    static constexpr std::uintptr_t Size = 0x10;
};

struct BSScriptIVirtualMachineLocalShimSlots
{
    static constexpr std::uintptr_t GetScriptObjectType1 = 0x9;
    static constexpr std::uintptr_t BindNativeMethod = 0x18;
    static constexpr std::uintptr_t SendEvent = 0x24;
    static constexpr std::uintptr_t GetObjectHandlePolicy = 0x2D;
    static constexpr std::uintptr_t Size = 0x10;
};

struct BSTEventSourceCommonLibNgOffsets
{
    static constexpr std::uintptr_t Size = 0x58;
};

struct BSTEventSourceLocalShimOffsets
{
    static constexpr std::uintptr_t Size = 0x58;
};

struct ScriptEventSourceHolderCommonLibNgOffsets
{
    static constexpr std::uintptr_t EventProcessed = 0x0;
    static constexpr std::uintptr_t Activate = 0x58;
    static constexpr std::uintptr_t GrabRelease = 0x580;
    static constexpr std::uintptr_t Hit = 0x5D8;
    static constexpr std::uintptr_t LoadGame = 0x688;
    static constexpr std::uintptr_t MagicEffectApply = 0x738;
    static constexpr std::uintptr_t QuestInit = 0x9F8;
    static constexpr std::uintptr_t QuestStage = 0xA50;
    static constexpr std::uintptr_t QuestStartStop = 0xB00;
    static constexpr std::uintptr_t SpellCast = 0xE18;
    static constexpr std::uintptr_t PlayerBowShot = 0xE70;
    static constexpr std::uintptr_t SwitchRaceComplete = 0x11E0;
    static constexpr std::uintptr_t SeFastTravelEnd = 0x1238;
    static constexpr std::uintptr_t SeSize = 0x1290;
    static constexpr std::uintptr_t VrSize = 0x1238;
};

struct EventDispatcherManagerLocalShimOffsets
{
    static constexpr std::uintptr_t EventProcessed = 0x0;
    static constexpr std::uintptr_t Activate = 0x58;
    static constexpr std::uintptr_t GrabRelease = 0x580;
    static constexpr std::uintptr_t Hit = 0x5D8;
    static constexpr std::uintptr_t LoadGame = 0x688;
    static constexpr std::uintptr_t MagicEffectApply = 0x738;
    static constexpr std::uintptr_t QuestInit = 0x9F8;
    static constexpr std::uintptr_t QuestStage = 0xA50;
    static constexpr std::uintptr_t QuestStartStop = 0xB00;
    static constexpr std::uintptr_t SpellCast = 0xE18;
    static constexpr std::uintptr_t PlayerBowShot = 0xE70;
    static constexpr std::uintptr_t SwitchRaceComplete = 0x11E0;
    static constexpr std::uintptr_t SeFastTravelEnd = 0x1238;
    static constexpr std::uintptr_t SeSize = 0x1290;
    static constexpr std::uintptr_t VrSize = 0x1238;
};

struct ProjectileLaunchDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t Origin = 0x8;
    static constexpr std::uintptr_t ContactNormal = 0x14;
    static constexpr std::uintptr_t ProjectileBase = 0x20;
    static constexpr std::uintptr_t Shooter = 0x28;
    static constexpr std::uintptr_t CombatController = 0x30;
    static constexpr std::uintptr_t WeaponSource = 0x38;
    static constexpr std::uintptr_t AmmoSource = 0x40;
    static constexpr std::uintptr_t AngleZ = 0x48;
    static constexpr std::uintptr_t AngleX = 0x4C;
    static constexpr std::uintptr_t Unk50 = 0x50;
    static constexpr std::uintptr_t DesiredTarget = 0x58;
    static constexpr std::uintptr_t Unk60 = 0x60;
    static constexpr std::uintptr_t Unk64 = 0x64;
    static constexpr std::uintptr_t ParentCell = 0x68;
    static constexpr std::uintptr_t Spell = 0x70;
    static constexpr std::uintptr_t CastingSource = 0x78;
    static constexpr std::uintptr_t Pad7C = 0x7C;
    static constexpr std::uintptr_t Enchantment = 0x80;
    static constexpr std::uintptr_t Poison = 0x88;
    static constexpr std::uintptr_t Area = 0x90;
    static constexpr std::uintptr_t Power = 0x94;
    static constexpr std::uintptr_t Scale = 0x98;
    static constexpr std::uintptr_t AlwaysHit = 0x9C;
    static constexpr std::uintptr_t NoDamageOutsideCombat = 0x9D;
    static constexpr std::uintptr_t AutoAim = 0x9E;
    static constexpr std::uintptr_t ChainShatter = 0x9F;
    static constexpr std::uintptr_t UseOrigin = 0xA0;
    static constexpr std::uintptr_t DeferInitialization = 0xA1;
    static constexpr std::uintptr_t ForceConeOfFire = 0xA2;
    static constexpr std::uintptr_t Size = 0xA8;
};

struct ProjectileLaunchDataLocalShimOffsets
{
    static constexpr std::uintptr_t Origin = 0x8;
    static constexpr std::uintptr_t ContactNormal = 0x14;
    static constexpr std::uintptr_t ProjectileBase = 0x20;
    static constexpr std::uintptr_t Shooter = 0x28;
    static constexpr std::uintptr_t CombatController = 0x30;
    static constexpr std::uintptr_t WeaponSource = 0x38;
    static constexpr std::uintptr_t AmmoSource = 0x40;
    static constexpr std::uintptr_t AngleZ = 0x48;
    static constexpr std::uintptr_t AngleX = 0x4C;
    static constexpr std::uintptr_t Unk50 = 0x50;
    static constexpr std::uintptr_t DesiredTarget = 0x58;
    static constexpr std::uintptr_t Unk60 = 0x60;
    static constexpr std::uintptr_t Unk64 = 0x64;
    static constexpr std::uintptr_t ParentCell = 0x68;
    static constexpr std::uintptr_t Spell = 0x70;
    static constexpr std::uintptr_t CastingSource = 0x78;
    static constexpr std::uintptr_t Pad7C = 0x7C;
    static constexpr std::uintptr_t Enchantment = 0x80;
    static constexpr std::uintptr_t Poison = 0x88;
    static constexpr std::uintptr_t Area = 0x90;
    static constexpr std::uintptr_t Power = 0x94;
    static constexpr std::uintptr_t Scale = 0x98;
    static constexpr std::uintptr_t AlwaysHit = 0x9C;
    static constexpr std::uintptr_t NoDamageOutsideCombat = 0x9D;
    static constexpr std::uintptr_t AutoAim = 0x9E;
    static constexpr std::uintptr_t ChainShatter = 0x9F;
    static constexpr std::uintptr_t UseOrigin = 0xA0;
    static constexpr std::uintptr_t DeferInitialization = 0xA1;
    static constexpr std::uintptr_t ForceConeOfFire = 0xA2;
    static constexpr std::uintptr_t Size = 0xA8;
};

struct ProjectileCommonLibNgOffsets
{
    static constexpr std::uintptr_t Power = 0x188;
};

struct ProjectileLocalShimOffsets
{
    static constexpr std::uintptr_t Power = 0x190;
};

struct IMenuCommonLibNgOffsets
{
    static constexpr std::uintptr_t MenuFlags = 0x1C;
    static constexpr std::uintptr_t InputContext = 0x20;
    static constexpr std::uintptr_t FxDelegate = 0x28;
    static constexpr std::uintptr_t VrUnk30 = 0x30;
    static constexpr std::uintptr_t VrUnk34 = 0x34;
    static constexpr std::uintptr_t VrUnk38 = 0x38;
    static constexpr std::uintptr_t SeSize = 0x30;
    static constexpr std::uintptr_t VrSize = 0x40;
};

struct IMenuLocalShimOffsets
{
    static constexpr std::uintptr_t MenuFlags = 0x1C;
    static constexpr std::uintptr_t InputContext = 0x20;
    static constexpr std::uintptr_t FxDelegate = 0x28;
    static constexpr std::uintptr_t VrUnk30 = 0x30;
    static constexpr std::uintptr_t VrUnk34 = 0x34;
    static constexpr std::uintptr_t VrUnk38 = 0x38;
    static constexpr std::uintptr_t SeSize = 0x30;
    static constexpr std::uintptr_t VrSize = 0x40;
};

struct UICommonLibNgOffsets
{
    static constexpr std::uintptr_t MenuStack = 0x110;
    static constexpr std::uintptr_t MenuMap = 0x128;
    static constexpr std::uintptr_t NumPausesGame = 0x160;
    static constexpr std::uintptr_t Size = 0x1C8;
};

struct UILocalShimOffsets
{
    static constexpr std::uintptr_t MenuStack = 0x110;
    static constexpr std::uintptr_t MenuMap = 0x128;
    static constexpr std::uintptr_t NumPausesGame = 0x160;
    static constexpr std::uintptr_t Size = 0x1C8;
};

struct MenuControlsCommonLibNgOffsets
{
    static constexpr std::uintptr_t IsProcessing = 0x80;
    static constexpr std::uintptr_t BeastForm = 0x81;
    static constexpr std::uintptr_t RemapMode = 0x82;
    static constexpr std::uintptr_t Toggle = 0x83;
    static constexpr std::uintptr_t Unk84 = 0x84;
    static constexpr std::uintptr_t Size = 0x88;
};

struct MenuControlsLocalShimOffsets
{
    static constexpr std::uintptr_t IsProcessing = 0x80;
    static constexpr std::uintptr_t BeastForm = 0x81;
    static constexpr std::uintptr_t RemapMode = 0x82;
    static constexpr std::uintptr_t Toggle = 0x83;
    static constexpr std::uintptr_t Unk84 = 0x84;
    static constexpr std::uintptr_t Size = 0x88;
};

struct ExtraContainerChangesCommonLibNgOffsets
{
    static constexpr std::uintptr_t Size = 0x18;
    static constexpr std::uintptr_t Data = 0x10;
    static constexpr std::uintptr_t EntrySize = 0x18;
    static constexpr std::uintptr_t EntryObject = 0x0;
    static constexpr std::uintptr_t EntryExtraLists = 0x8;
    static constexpr std::uintptr_t EntryCountDelta = 0x10;
    static constexpr std::uintptr_t DataSize = 0x20;
    static constexpr std::uintptr_t DataEntries = 0x0;
    static constexpr std::uintptr_t DataParent = 0x8;
    static constexpr std::uintptr_t DataTotalWeight = 0x10;
    static constexpr std::uintptr_t DataArmorWeight = 0x14;
    static constexpr std::uintptr_t DataUnk18 = 0x18;
};

struct ExtraContainerChangesLocalShimOffsets
{
    static constexpr std::uintptr_t Size = 0x18;
    static constexpr std::uintptr_t Data = 0x10;
    static constexpr std::uintptr_t EntrySize = 0x18;
    static constexpr std::uintptr_t EntryObject = 0x0;
    static constexpr std::uintptr_t EntryExtraLists = 0x8;
    static constexpr std::uintptr_t EntryCountDelta = 0x10;
    static constexpr std::uintptr_t DataSize = 0x20;
    static constexpr std::uintptr_t DataEntries = 0x0;
    static constexpr std::uintptr_t DataParent = 0x8;
    static constexpr std::uintptr_t DataTotalWeight = 0x10;
    static constexpr std::uintptr_t DataArmorWeight = 0x14;
    static constexpr std::uintptr_t DataUnk18 = 0x18;
};

struct TESContainerCommonLibNgOffsets
{
    static constexpr std::uintptr_t EntrySize = 0x18;
    static constexpr std::uintptr_t EntryCount = 0x0;
    static constexpr std::uintptr_t EntryForm = 0x8;
    static constexpr std::uintptr_t EntryData = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
    static constexpr std::uintptr_t Entries = 0x8;
    static constexpr std::uintptr_t Count = 0x10;
};

struct TESContainerLocalShimOffsets
{
    static constexpr std::uintptr_t EntrySize = 0x18;
    static constexpr std::uintptr_t EntryCount = 0x0;
    static constexpr std::uintptr_t EntryForm = 0x8;
    static constexpr std::uintptr_t EntryData = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
    static constexpr std::uintptr_t Entries = 0x8;
    static constexpr std::uintptr_t Count = 0x10;
};

struct InventoryEntryCommonLibNgOffsets
{
    static constexpr std::uintptr_t Size = 0x18;
    static constexpr std::uintptr_t Object = 0x0;
    static constexpr std::uintptr_t ExtraLists = 0x8;
    static constexpr std::uintptr_t Count = 0x10;
};

struct InventoryEntryLocalShimOffsets
{
    static constexpr std::uintptr_t Size = 0x18;
    static constexpr std::uintptr_t Object = 0x0;
    static constexpr std::uintptr_t ExtraLists = 0x8;
    static constexpr std::uintptr_t Count = 0x10;
};

struct ExtraDataListCommonLibNgOffsets
{
    static constexpr std::uintptr_t BitfieldSize = 0x18;
    static constexpr std::uintptr_t Data = 0x8;
    static constexpr std::uintptr_t Bitfield = 0x10;
    static constexpr std::uintptr_t Lock = 0x18;
};

struct ExtraDataListLocalShimOffsets
{
    static constexpr std::uintptr_t BitfieldSize = 0x18;
    static constexpr std::uintptr_t Data = 0x8;
    static constexpr std::uintptr_t Bitfield = 0x10;
    static constexpr std::uintptr_t Lock = 0x18;
};

struct ExtraTextDisplayDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t DisplayName = 0x10;
    static constexpr std::uintptr_t DisplayNameText = 0x18;
    static constexpr std::uintptr_t OwnerQuest = 0x20;
    static constexpr std::uintptr_t OwnerInstance = 0x28;
    static constexpr std::uintptr_t TemperFactor = 0x2C;
    static constexpr std::uintptr_t CustomNameLength = 0x30;
    static constexpr std::uintptr_t Size = 0x38;
};

struct ExtraTextDisplayDataLocalShimOffsets
{
    static constexpr std::uintptr_t DisplayName = 0x10;
    static constexpr std::uintptr_t DisplayNameText = 0x18;
    static constexpr std::uintptr_t OwnerQuest = 0x20;
    static constexpr std::uintptr_t OwnerInstance = 0x28;
    static constexpr std::uintptr_t TemperFactor = 0x2C;
    static constexpr std::uintptr_t CustomNameLength = 0x30;
    static constexpr std::uintptr_t Size = 0x38;
};

struct ExtraCountCommonLibNgOffsets
{
    static constexpr std::uintptr_t Count = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraCountLocalShimOffsets
{
    static constexpr std::uintptr_t Count = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraChargeCommonLibNgOffsets
{
    static constexpr std::uintptr_t Charge = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraChargeLocalShimOffsets
{
    static constexpr std::uintptr_t Charge = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraHealthCommonLibNgOffsets
{
    static constexpr std::uintptr_t Health = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraHealthLocalShimOffsets
{
    static constexpr std::uintptr_t Health = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraEnchantmentCommonLibNgOffsets
{
    static constexpr std::uintptr_t Enchantment = 0x10;
    static constexpr std::uintptr_t Charge = 0x18;
    static constexpr std::uintptr_t RemoveOnUnequip = 0x1A;
    static constexpr std::uintptr_t Size = 0x20;
};

struct ExtraEnchantmentLocalShimOffsets
{
    static constexpr std::uintptr_t Enchantment = 0x10;
    static constexpr std::uintptr_t Charge = 0x18;
    static constexpr std::uintptr_t RemoveOnUnequip = 0x1A;
    static constexpr std::uintptr_t Size = 0x20;
};

struct ExtraPoisonCommonLibNgOffsets
{
    static constexpr std::uintptr_t Poison = 0x10;
    static constexpr std::uintptr_t Count = 0x18;
    static constexpr std::uintptr_t Size = 0x20;
};

struct ExtraPoisonLocalShimOffsets
{
    static constexpr std::uintptr_t Poison = 0x10;
    static constexpr std::uintptr_t Count = 0x18;
    static constexpr std::uintptr_t Size = 0x20;
};

struct ExtraSoulCommonLibNgOffsets
{
    static constexpr std::uintptr_t Soul = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraSoulLocalShimOffsets
{
    static constexpr std::uintptr_t Soul = 0x10;
    static constexpr std::uintptr_t Size = 0x18;
};

struct ExtraFactionChangesCommonLibNgOffsets
{
    static constexpr std::uintptr_t Entries = 0x10;
    static constexpr std::uintptr_t CrimeFaction = 0x28;
    static constexpr std::uintptr_t RemoveCrimeFaction = 0x30;
    static constexpr std::uintptr_t Size = 0x38;
};

struct ExtraFactionChangesLocalShimOffsets
{
    static constexpr std::uintptr_t Entries = 0x10;
    static constexpr std::uintptr_t Size = 0x28;
};

struct BGSSaveGameBufferCommonLibNgOffsets
{
    static constexpr std::uintptr_t Buffer = 0x8;
    static constexpr std::uintptr_t BufferSize = 0x10;
    static constexpr std::uintptr_t BufferPosition = 0x14;
    static constexpr std::uintptr_t Size = 0x18;
};

struct BGSSaveGameBufferLocalShimOffsets
{
    static constexpr std::uintptr_t Buffer = 0x8;
    static constexpr std::uintptr_t BufferSize = 0x10;
    static constexpr std::uintptr_t BufferPosition = 0x14;
    static constexpr std::uintptr_t Size = 0x18;
};

struct BGSSaveFormBufferCommonLibNgOffsets
{
    static constexpr std::uintptr_t FormIdIndex = 0x18;
    static constexpr std::uintptr_t ChangeFlags = 0x1B;
    static constexpr std::uintptr_t FormInfo = 0x1F;
    static constexpr std::uintptr_t Version = 0x20;
    static constexpr std::uintptr_t Form = 0x28;
    static constexpr std::uintptr_t Size = 0x30;
};

struct BGSSaveFormBufferLocalShimOffsets
{
    static constexpr std::uintptr_t FormIdIndex = 0x18;
    static constexpr std::uintptr_t ChangeFlags = 0x1B;
    static constexpr std::uintptr_t FormInfo = 0x1F;
    static constexpr std::uintptr_t Version = 0x20;
    static constexpr std::uintptr_t Form = 0x28;
    static constexpr std::uintptr_t Size = 0x30;
};

struct BGSLoadGameBufferCommonLibNgOffsets
{
    static constexpr std::uintptr_t Buffer = 0x8;
    static constexpr std::uintptr_t BufferSize = 0x10;
    static constexpr std::uintptr_t Unk18 = 0x18;
    static constexpr std::uintptr_t Unk1C = 0x1C;
    static constexpr std::uintptr_t DataSize = 0x20;
    static constexpr std::uintptr_t BufferPosition = 0x24;
    static constexpr std::uintptr_t Size = 0x28;
};

struct BGSLoadGameBufferLocalShimOffsets
{
    static constexpr std::uintptr_t Buffer = 0x8;
    static constexpr std::uintptr_t BufferSize = 0x10;
    static constexpr std::uintptr_t Unk18 = 0x18;
    static constexpr std::uintptr_t Unk1C = 0x1C;
    static constexpr std::uintptr_t DataSize = 0x20;
    static constexpr std::uintptr_t BufferPosition = 0x24;
    static constexpr std::uintptr_t Size = 0x28;
};

struct BGSLoadFormBufferCommonLibNgOffsets
{
    static constexpr std::uintptr_t FormId = 0x28;
    static constexpr std::uintptr_t DataSize = 0x2C;
    static constexpr std::uintptr_t UncompressedSize = 0x30;
    static constexpr std::uintptr_t Pad34 = 0x34;
    static constexpr std::uintptr_t Form = 0x38;
    static constexpr std::uintptr_t ChangeFlags = 0x40;
    static constexpr std::uintptr_t OldChangeFlags = 0x44;
    static constexpr std::uintptr_t Flags = 0x48;
    static constexpr std::uintptr_t Version = 0x4B;
    static constexpr std::uintptr_t Size = 0x50;
};

struct BGSLoadFormBufferLocalShimOffsets
{
    static constexpr std::uintptr_t FormId = 0x28;
    static constexpr std::uintptr_t DataSize = 0x2C;
    static constexpr std::uintptr_t UncompressedSize = 0x30;
    static constexpr std::uintptr_t Pad34 = 0x34;
    static constexpr std::uintptr_t Form = 0x38;
    static constexpr std::uintptr_t ChangeFlags = 0x40;
    static constexpr std::uintptr_t OldChangeFlags = 0x44;
    static constexpr std::uintptr_t Flags = 0x48;
    static constexpr std::uintptr_t Version = 0x4B;
    static constexpr std::uintptr_t Size = 0x50;
};

struct BSGraphicsRendererDataCommonLibNgOffsets
{
    static constexpr std::uintptr_t Forwarder = 0x38;
    static constexpr std::uintptr_t Context = 0x40;
    static constexpr std::uintptr_t RenderWindows = 0x48;
    static constexpr std::uintptr_t RenderTargets = 0xA48;
    static constexpr std::uintptr_t DepthStencilTargets = 0x1FA8;
    static constexpr std::uintptr_t CubeMapRenderTargets = 0x26C8;
    static constexpr std::uintptr_t Texture3DRenderTargets = 0x2708;
    static constexpr std::uintptr_t ClearColor = 0x2768;
    static constexpr std::uintptr_t ClearStencil = 0x2778;
    static constexpr std::uintptr_t Lock = 0x2780;
    static constexpr std::uintptr_t ClassName = 0x27A8;
    static constexpr std::uintptr_t HInstance = 0x27B0;
};

struct BSGraphicsRendererDataLocalShimOffsets
{
    static constexpr std::uintptr_t Forwarder = 0x38;
    static constexpr std::uintptr_t Context = 0x40;
    static constexpr std::uintptr_t RenderWindows = 0x48;
    static constexpr std::uintptr_t RenderTargets = 0xA48;
    static constexpr std::uintptr_t DepthStencilTargets = 0x1FA8;
    static constexpr std::uintptr_t CubeMapRenderTargets = 0x26C8;
};

struct BSGraphicsRendererCommonLibNgOffsets
{
    static constexpr std::uintptr_t Data = 0x10;
};

struct BSGraphicsRendererLocalShimOffsets
{
    static constexpr std::uintptr_t Data = 0x10;
};
} // namespace Skyrim::RuntimeLayout
