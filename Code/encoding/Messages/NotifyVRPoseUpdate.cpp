#include <Messages/NotifyVRPoseUpdate.h>
#include <TiltedCore/Serialization.hpp>

void NotifyVRPoseUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, PlayerId);
    Pose.Serialize(aWriter);
}

void NotifyVRPoseUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Pose.Deserialize(aReader);
}
