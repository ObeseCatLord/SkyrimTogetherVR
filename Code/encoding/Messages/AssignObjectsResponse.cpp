#include <Messages/AssignObjectsResponse.h>

#include <algorithm>

void AssignObjectsResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min(Objects.size(), kMaximumWireObjects);
    aWriter.WriteBits(count, 8);

    for (std::size_t index = 0; index < count; ++index)
        Objects[index].Serialize(aWriter);
}

void AssignObjectsResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Objects.clear();
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    if (!aReader.ReadBits(count, 8) || count > kMaximumWireObjects)
    {
        IsDecodedValid = false;
        return;
    }

    std::size_t totalEntries{};
    std::size_t totalEffects{};
    try
    {
        Objects.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = 0; index < count; ++index)
        {
            ObjectData object{};
            object.Deserialize(aReader);
            if (!object.IsDecodedValid ||
                object.CurrentInventory.Entries.size() > kMaximumTotalInventoryEntries - totalEntries)
            {
                Objects.clear();
                IsDecodedValid = false;
                return;
            }
            totalEntries += object.CurrentInventory.Entries.size();
            for (const auto& entry : object.CurrentInventory.Entries)
            {
                if (entry.EnchantData.Effects.size() > kMaximumTotalInventoryEffects - totalEffects)
                {
                    Objects.clear();
                    IsDecodedValid = false;
                    return;
                }
                totalEffects += entry.EnchantData.Effects.size();
            }
            Objects.push_back(std::move(object));
        }
    }
    catch (...)
    {
        Objects.clear();
        IsDecodedValid = false;
    }
}
