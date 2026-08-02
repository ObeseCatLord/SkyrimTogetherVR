#pragma once

#include <cstddef>
#include <cstdint>
#include "MagicEquipment.h"

using TiltedPhoques::Buffer;
using TiltedPhoques::String;
using TiltedPhoques::Vector;

struct Inventory
{
    static constexpr std::size_t kMaximumWireEntries = 4096;
    static constexpr std::size_t kMaximumWireEffects = 4096;

    struct EffectItem
    {
        float Magnitude{};
        int32_t Area{};
        int32_t Duration{};
        float RawCost{};
        GameId EffectId{};

        void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
        void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

        bool operator==(const EffectItem& acRhs) const noexcept;
        bool operator!=(const EffectItem& acRhs) const noexcept;
    };

    struct EnchantmentData
    {
        bool IsWeapon{};
        Vector<EffectItem> Effects{};
    };

    struct Entry
    {
        static constexpr std::int32_t kMaximumMutationCount = 1'000'000;
        static constexpr float kMaximumMutationScalarMagnitude = 1'000'000.0F;
        static constexpr std::size_t kMaximumMutationEffects = kMaximumWireEffects;

        enum EquipmentFlag : std::uint8_t
        {
            kEquipmentWeapon = 1u << 0,
            kEquipmentAmmo = 1u << 1,
        };

        GameId BaseId{};
        int32_t Count{};

        float ExtraCharge{};

        GameId ExtraEnchantId{};
        uint16_t ExtraEnchantCharge{};
        EnchantmentData EnchantData{};

        float ExtraHealth{};

        GameId ExtraPoisonId{};
        uint32_t ExtraPoisonCount{};

        int32_t ExtraSoulLevel{};

        bool ExtraEnchantRemoveUnequip{};
        bool ExtraWorn{};
        bool ExtraWornLeft{};
        bool IsQuestItem{};
        // Classification is populated by final-equipment snapshots. It remains
        // zero for ordinary inventory entries and lets the server translate an
        // atomic snapshot into the original incremental equipment protocol.
        std::uint8_t EquipmentFlags{};
        bool IsDecodedValid{true};

        bool operator==(const Entry& acRhs) const noexcept;
        bool operator!=(const Entry& acRhs) const noexcept;

        void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
        void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

        // Validates the complete payload accepted by AddOrRemoveEntry.
        [[nodiscard]] bool IsValidMutation() const noexcept;

        bool ContainsExtraData() const noexcept { return !IsExtraDataEquals(Entry{}); }

        bool CanBeMerged(const Entry& acRhs) const noexcept { return BaseId == acRhs.BaseId && IsExtraDataEquals(acRhs); }

        bool IsExtraDataEquals(const Entry& acRhs) const noexcept
        {
            // TODO: the whole server side state thing is very flawed
            // since many of these things can and will change, like poison id or charge
            // or the fact that the enchant id can be temp
            return ExtraCharge == acRhs.ExtraCharge && ExtraEnchantId == acRhs.ExtraEnchantId && ExtraEnchantCharge == acRhs.ExtraEnchantCharge && EnchantData.IsWeapon == acRhs.EnchantData.IsWeapon && EnchantData.Effects == acRhs.EnchantData.Effects && ExtraEnchantRemoveUnequip == acRhs.ExtraEnchantRemoveUnequip && ExtraHealth == acRhs.ExtraHealth && ExtraPoisonId == acRhs.ExtraPoisonId &&
                   ExtraPoisonCount == acRhs.ExtraPoisonCount && ExtraSoulLevel == acRhs.ExtraSoulLevel && ExtraWorn == acRhs.ExtraWorn && ExtraWornLeft == acRhs.ExtraWornLeft && IsQuestItem == acRhs.IsQuestItem && EquipmentFlags == acRhs.EquipmentFlags;
        }

        bool IsWorn() const noexcept { return ExtraWorn || ExtraWornLeft; }
    };

    bool operator==(const Inventory& acRhs) const noexcept;
    bool operator!=(const Inventory& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    std::optional<Entry> GetEntryById(GameId& aItemId) const noexcept;
    int32_t GetEntryCountById(GameId& aItemId) const noexcept;

    void RemoveByFilter(std::function<bool(const Entry&)> aFilter) noexcept;
    [[nodiscard]] bool AddOrRemoveEntry(const Entry& acEntry) noexcept;
    void UpdateEquipment(const Inventory& acNewInventory) noexcept;
    bool ContainsQuestItems() const noexcept;

    Vector<Entry> Entries{};
    MagicEquipment CurrentMagicEquipment{};
    bool IsDecodedValid{true};
};
