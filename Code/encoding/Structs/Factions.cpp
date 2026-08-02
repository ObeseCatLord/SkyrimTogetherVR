#include <Structs/Factions.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool Factions::operator==(const Factions& acRhs) const noexcept
{
    return NpcFactions == acRhs.NpcFactions && ExtraFactions == acRhs.ExtraFactions;
}

bool Factions::operator!=(const Factions& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Factions::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto npcCount = std::min(NpcFactions.size(), kMaximumWireEntries);
    Serialization::WriteVarInt(aWriter, npcCount);

    for (std::size_t index = 0; index < npcCount; ++index)
        NpcFactions[index].Serialize(aWriter);

    const auto extraCount = std::min(ExtraFactions.size(), kMaximumWireEntries);
    Serialization::WriteVarInt(aWriter, extraCount);

    for (std::size_t index = 0; index < extraCount; ++index)
        ExtraFactions[index].Serialize(aWriter);
}

void Factions::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    const auto npcCount = Serialization::ReadVarInt(aReader);
    if (npcCount > kMaximumWireEntries)
    {
        IsDecodedValid = false;
        return;
    }
    try
    {
        NpcFactions.resize(static_cast<std::size_t>(npcCount));
        for (auto& entry : NpcFactions)
            entry.Deserialize(aReader);

        const auto extraCount = Serialization::ReadVarInt(aReader);
        if (extraCount > kMaximumWireEntries)
        {
            NpcFactions.clear();
            IsDecodedValid = false;
            return;
        }
        ExtraFactions.resize(static_cast<std::size_t>(extraCount));
        for (auto& entry : ExtraFactions)
            entry.Deserialize(aReader);
    }
    catch (...)
    {
        NpcFactions.clear();
        ExtraFactions.clear();
        IsDecodedValid = false;
    }
}
