#include <Structs/Tints.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>
#include <cstring>

using TiltedPhoques::Serialization;

bool Tints::Entry::operator==(const Entry& acRhs) const noexcept
{
    return Alpha == acRhs.Alpha && Type == acRhs.Type && Color == acRhs.Color && Name == acRhs.Name;
}

bool Tints::Entry::operator!=(const Entry& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool Tints::operator==(const Tints& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool Tints::operator!=(const Tints& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Tints::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min(Entries.size(), kMaximumWireEntries);
    aWriter.WriteBits(count, 8);

    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& entry = Entries[index];
        Serialization::WriteVarInt(aWriter, entry.Type);
        aWriter.WriteBits(entry.Color, 32);
        entry.Name.Serialize(aWriter);
        std::uint32_t alphaBits{};
        std::memcpy(&alphaBits, &entry.Alpha, sizeof(alphaBits));
        aWriter.WriteBits(alphaBits, 32);
    }
}

void Tints::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    uint64_t tmp = 0;
    if (!aReader.ReadBits(tmp, 8))
    {
        IsDecodedValid = false;
        return;
    }

    const auto cCount = tmp & 0xFF;
    try
    {
        Entries.reserve(static_cast<std::size_t>(cCount));
        for (auto i = 0u; i < cCount; ++i)
        {
            uint64_t buffer = 0;

            Entry entry;
            entry.Type = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

            if (!aReader.ReadBits(buffer, 32))
            {
                IsDecodedValid = false;
                Entries.clear();
                return;
            }
            entry.Color = buffer & 0xFFFFFFFF;
            entry.Name.Deserialize(aReader);
            if (!entry.Name.IsDecodedValid)
            {
                IsDecodedValid = false;
                Entries.clear();
                return;
            }

            if (!aReader.ReadBits(buffer, 32))
            {
                IsDecodedValid = false;
                Entries.clear();
                return;
            }
            const auto alphaBits = static_cast<std::uint32_t>(buffer);
            std::memcpy(&entry.Alpha, &alphaBits, sizeof(entry.Alpha));

            Entries.push_back(entry);
        }
    }
    catch (...)
    {
        Entries.clear();
        IsDecodedValid = false;
    }
}
