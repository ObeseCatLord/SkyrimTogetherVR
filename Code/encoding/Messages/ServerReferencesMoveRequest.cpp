#include <Messages/ServerReferencesMoveRequest.h>
#include <TiltedCore/Serialization.hpp>

void ServerReferencesMoveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Updates.size());

    for (const auto& kvp : Updates)
    {
        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ServerReferencesMoveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Updates.clear();
    IsDecodedValid = true;
    ServerMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);
    if (count > kMaximumUpdates)
    {
        IsDecodedValid = false;
        return;
    }

    try
    {
        for (uint64_t i = 0; i < count; ++i)
        {
            const uint32_t serverId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
            if (serverId == 0 || Updates.contains(serverId))
            {
                IsDecodedValid = false;
                Updates.clear();
                return;
            }
            ReferenceUpdate update{};
            update.Deserialize(aReader);
            if (!update.IsDecodedValid)
            {
                IsDecodedValid = false;
                Updates.clear();
                return;
            }
            Updates.emplace(serverId, std::move(update));
        }
    }
    catch (...)
    {
        Updates.clear();
        IsDecodedValid = false;
    }
}
