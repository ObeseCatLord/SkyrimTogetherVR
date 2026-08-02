#include <Structs/Inventory.h>
#include <TiltedCore/Serialization.hpp>

#include <cmath>
#include <limits>

using TiltedPhoques::Serialization;

void Inventory::EffectItem::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Magnitude);
    Serialization::WriteVarInt(aWriter, Area);
    Serialization::WriteVarInt(aWriter, Duration);
    Serialization::WriteFloat(aWriter, RawCost);
    EffectId.Serialize(aWriter);
}

void Inventory::EffectItem::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Magnitude = Serialization::ReadFloat(aReader);
    Area = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Duration = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RawCost = Serialization::ReadFloat(aReader);
    EffectId.Deserialize(aReader);
}

bool Inventory::EffectItem::operator==(const Inventory::EffectItem& acRhs) const noexcept
{
    return Magnitude == acRhs.Magnitude && Area == acRhs.Area && Duration == acRhs.Duration &&
           RawCost == acRhs.RawCost && EffectId == acRhs.EffectId;
}

bool Inventory::EffectItem::operator!=(const Inventory::EffectItem& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Inventory::Entry::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    BaseId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Count);

    Serialization::WriteFloat(aWriter, ExtraCharge);

    ExtraEnchantId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ExtraEnchantCharge);
    Serialization::WriteVarInt(aWriter, EnchantData.Effects.size());
    for (const EffectItem& effect : EnchantData.Effects)
    {
        effect.Serialize(aWriter);
    }

    Serialization::WriteFloat(aWriter, ExtraHealth);

    ExtraPoisonId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ExtraPoisonCount);

    Serialization::WriteVarInt(aWriter, ExtraSoulLevel);

    Serialization::WriteBool(aWriter, EnchantData.IsWeapon);
    Serialization::WriteBool(aWriter, ExtraEnchantRemoveUnequip);
    Serialization::WriteBool(aWriter, ExtraWorn);
    Serialization::WriteBool(aWriter, ExtraWornLeft);
    Serialization::WriteBool(aWriter, IsQuestItem);
    Serialization::WriteVarInt(aWriter, EquipmentFlags);
}

void Inventory::Entry::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    BaseId.Deserialize(aReader);
    Count = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    ExtraCharge = Serialization::ReadFloat(aReader);

    ExtraEnchantId.Deserialize(aReader);
    ExtraEnchantCharge = Serialization::ReadVarInt(aReader) & 0xFFFF;
    const uint64_t effectCount = Serialization::ReadVarInt(aReader);
    if (effectCount > Inventory::kMaximumWireEffects)
    {
        IsDecodedValid = false;
        return;
    }
    try
    {
        EnchantData.Effects.reserve(static_cast<std::size_t>(effectCount));
        for (uint64_t i = 0; i < effectCount; i++)
        {
            EffectItem effect;
            effect.Deserialize(aReader);
            EnchantData.Effects.push_back(effect);
        }
    }
    catch (...)
    {
        EnchantData.Effects.clear();
        IsDecodedValid = false;
        return;
    }

    ExtraHealth = Serialization::ReadFloat(aReader);

    ExtraPoisonId.Deserialize(aReader);
    ExtraPoisonCount = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    ExtraSoulLevel = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    EnchantData.IsWeapon = Serialization::ReadBool(aReader);
    ExtraEnchantRemoveUnequip = Serialization::ReadBool(aReader);
    ExtraWorn = Serialization::ReadBool(aReader);
    ExtraWornLeft = Serialization::ReadBool(aReader);
    IsQuestItem = Serialization::ReadBool(aReader);
    EquipmentFlags = Serialization::ReadVarInt(aReader) & 0xFF;
}

bool Inventory::Entry::IsValidMutation() const noexcept
{
    constexpr auto knownEquipmentFlags = kEquipmentWeapon | kEquipmentAmmo;
    const auto validRequiredId = [](const GameId& acId) noexcept { return acId.BaseId != 0; };
    const auto validOptionalId = [&validRequiredId](const GameId& acId) noexcept {
        return !acId || validRequiredId(acId);
    };

    if (!IsDecodedValid || !validRequiredId(BaseId) || Count == 0 || Count < -kMaximumMutationCount ||
        Count > kMaximumMutationCount || !std::isfinite(ExtraCharge) || ExtraCharge < 0.0F ||
        ExtraCharge > kMaximumMutationScalarMagnitude || !std::isfinite(ExtraHealth) || ExtraHealth < 0.0F ||
        ExtraHealth > kMaximumMutationScalarMagnitude || !validOptionalId(ExtraEnchantId) ||
        !validOptionalId(ExtraPoisonId) || ExtraSoulLevel < 0 || ExtraSoulLevel > 5 ||
        EnchantData.Effects.size() > kMaximumMutationEffects ||
        (EquipmentFlags & ~knownEquipmentFlags) != 0 ||
        (EquipmentFlags & knownEquipmentFlags) == knownEquipmentFlags ||
        ((EquipmentFlags & kEquipmentAmmo) != 0 && ExtraWornLeft) ||
        (!ExtraEnchantId &&
         (ExtraEnchantCharge != 0 || !EnchantData.Effects.empty() || EnchantData.IsWeapon ||
          ExtraEnchantRemoveUnequip)) ||
        (!ExtraPoisonId && ExtraPoisonCount != 0) ||
        (ExtraPoisonId && ExtraPoisonCount == 0) ||
        ExtraPoisonCount > static_cast<std::uint32_t>(kMaximumMutationCount))
        return false;

    for (const auto& effect : EnchantData.Effects)
    {
        if (!validRequiredId(effect.EffectId) || effect.Area < 0 || effect.Area > kMaximumMutationCount ||
            effect.Duration < 0 || effect.Duration > kMaximumMutationCount || !std::isfinite(effect.Magnitude) ||
            std::abs(effect.Magnitude) > kMaximumMutationScalarMagnitude || !std::isfinite(effect.RawCost) ||
            effect.RawCost < 0.0F || effect.RawCost > kMaximumMutationScalarMagnitude)
            return false;
    }
    return true;
}

bool Inventory::operator==(const Inventory& acRhs) const noexcept
{
    return Entries == acRhs.Entries && CurrentMagicEquipment == acRhs.CurrentMagicEquipment;
}

bool Inventory::operator!=(const Inventory& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool Inventory::Entry::operator==(const Inventory::Entry& acRhs) const noexcept
{
    return BaseId == acRhs.BaseId && Count == acRhs.Count && IsExtraDataEquals(acRhs);
}

bool Inventory::Entry::operator!=(const Inventory::Entry& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Inventory::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Entries.size());
    for (const Entry& entry : Entries)
    {
        entry.Serialize(aWriter);
    }

    CurrentMagicEquipment.Serialize(aWriter);
}

void Inventory::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaximumWireEntries)
    {
        IsDecodedValid = false;
        return;
    }

    std::size_t totalEffects{};
    try
    {
        Entries.reserve(static_cast<std::size_t>(count));
        for (uint64_t i = 0; i < count; i++)
        {
            Entry entry;
            entry.Deserialize(aReader);
            if (!entry.IsDecodedValid ||
                entry.EnchantData.Effects.size() > kMaximumWireEffects - totalEffects)
            {
                Entries.clear();
                IsDecodedValid = false;
                return;
            }
            totalEffects += entry.EnchantData.Effects.size();
            Entries.push_back(std::move(entry));
        }
    }
    catch (...)
    {
        Entries.clear();
        IsDecodedValid = false;
        return;
    }

    CurrentMagicEquipment.Deserialize(aReader);
}

std::optional<Inventory::Entry> Inventory::GetEntryById(GameId& aItemId) const noexcept
{
    auto entry = std::find_if(Entries.begin(), Entries.end(), [aItemId](auto entry) { return entry.BaseId == aItemId; });
    if (entry == Entries.end())
        return std::nullopt;

    return {*entry};
}

int32_t Inventory::GetEntryCountById(GameId& aItemId) const noexcept
{
    auto entry = GetEntryById(aItemId);
    if (!entry)
        return 0;

    return entry->Count;
}

bool Inventory::AddOrRemoveEntry(const Entry& acEntry) noexcept
{
    if (!acEntry.IsValidMutation())
        return false;

    try
    {
        // Copy first so allocation, copy, and erase failures cannot alter the
        // authoritative inventory. The swap below is the single commit point.
        auto staged = Entries;
        const auto duplicate = std::find_if(staged.begin(), staged.end(), [&acEntry](const Entry& acExisting) {
            return acExisting.CanBeMerged(acEntry);
        });

        if (duplicate == staged.end())
        {
            if (acEntry.Count < 0)
                return false;
            staged.push_back(acEntry);
        }
        else
        {
            const auto count = static_cast<std::int64_t>(duplicate->Count) + acEntry.Count;
            if (count < std::numeric_limits<std::int32_t>::min() ||
                count > std::numeric_limits<std::int32_t>::max())
                return false;

            if (count <= 0)
                staged.erase(duplicate);
            else
                duplicate->Count = static_cast<std::int32_t>(count);
        }

        Entries.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void Inventory::UpdateEquipment(const Inventory& acNewInventory) noexcept
{
    while (true)
    {
        auto wornEntry = std::find_if(Entries.begin(), Entries.end(), [](auto& aEntry) { return aEntry.IsWorn(); });
        if (wornEntry == Entries.end())
            break;

        wornEntry->ExtraWorn = wornEntry->ExtraWornLeft = false;
    }

    for (const auto& newEntry : acNewInventory.Entries)
    {
        if (!newEntry.IsWorn())
            continue;

        auto entry = std::find_if(Entries.begin(), Entries.end(), [&newEntry](auto& aEntry) { return aEntry.BaseId == newEntry.BaseId; });

        // This shouldn't happen
        if (entry == Entries.end())
            continue;

        entry->ExtraWorn = newEntry.ExtraWorn;
        entry->ExtraWornLeft = newEntry.ExtraWornLeft;
    }

    CurrentMagicEquipment = acNewInventory.CurrentMagicEquipment;
}

void Inventory::RemoveByFilter(std::function<bool(const Entry&)> aFilter) noexcept
{
    Entries.erase(std::remove_if(Entries.begin(), Entries.end(), aFilter), Entries.end());
}

bool Inventory::ContainsQuestItems() const noexcept
{
    for (const auto& entry : Entries)
    {
        if (entry.IsQuestItem)
            return true;
    }
    return false;
}
