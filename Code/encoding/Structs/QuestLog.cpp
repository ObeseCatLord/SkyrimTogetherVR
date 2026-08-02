
#include <Structs/QuestLog.h>

#include <TiltedCore/Serialization.hpp>
#include <Structs/Mods.h>
#include <algorithm>

using TiltedPhoques::Serialization;

bool QuestLog::operator==(const QuestLog& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool QuestLog::operator!=(const QuestLog& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void QuestLog::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min(Entries.size(), kMaximumWireEntries);
    aWriter.WriteBits(count, 16);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& e = Entries[index];
        e.Id.Serialize(aWriter);

        // id's can be in the full 16 bit range
        aWriter.WriteBits(e.Stage, 16);
    }
}

void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    uint64_t temp{};
    if (!aReader.ReadBits(temp, 16))
    {
        IsDecodedValid = false;
        return;
    }

    const size_t count = temp & 0xFFFF;
    try
    {
        Entries.resize(count);
        for (auto& e : Entries)
        {
            e.Id.Deserialize(aReader);
            if (!aReader.ReadBits(temp, 16))
            {
                Entries.clear();
                IsDecodedValid = false;
                return;
            }

            e.Stage = temp & 0xFFFF;
        }
    }
    catch (...)
    {
        Entries.clear();
        IsDecodedValid = false;
    }
}
