#include <Structs/ActorValues.h>
#include <TiltedCore/Serialization.hpp>
#include <cmath>

using TiltedPhoques::Serialization;

bool ActorValues::operator==(const ActorValues& acRhs) const noexcept
{
    return ActorValuesList == acRhs.ActorValuesList;
}

bool ActorValues::operator!=(const ActorValues& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ActorValues::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorValuesList.size());
    for (auto& value : ActorValuesList)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }

    Serialization::WriteVarInt(aWriter, ActorMaxValuesList.size());
    for (auto& value : ActorMaxValuesList)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void ActorValues::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    const auto count = Serialization::ReadVarInt(aReader);
    if (count > kMaximumWireValues)
    {
        IsDecodedValid = false;
        return;
    }
    try
    {
        for (uint64_t i = 0; i < count; i++)
        {
            const auto key = Serialization::ReadVarInt(aReader);
            const auto value = Serialization::ReadFloat(aReader);
            if (key >= kMaximumWireValues || !std::isfinite(value) ||
                !ActorValuesList.insert({static_cast<std::uint32_t>(key), value}).second)
            {
                ActorValuesList.clear();
                IsDecodedValid = false;
                return;
            }
        }

        const auto maxCount = Serialization::ReadVarInt(aReader);
        if (maxCount > kMaximumWireValues)
        {
            ActorValuesList.clear();
            IsDecodedValid = false;
            return;
        }
        for (uint64_t i = 0; i < maxCount; i++)
        {
            const auto key = Serialization::ReadVarInt(aReader);
            const auto value = Serialization::ReadFloat(aReader);
            if (key >= kMaximumWireValues || !std::isfinite(value) ||
                !ActorMaxValuesList.insert({static_cast<std::uint32_t>(key), value}).second)
            {
                ActorValuesList.clear();
                ActorMaxValuesList.clear();
                IsDecodedValid = false;
                return;
            }
        }
    }
    catch (...)
    {
        ActorValuesList.clear();
        ActorMaxValuesList.clear();
        IsDecodedValid = false;
    }
}
