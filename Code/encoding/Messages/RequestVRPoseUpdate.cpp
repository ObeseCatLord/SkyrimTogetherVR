#include <Messages/RequestVRPoseUpdate.h>

void RequestVRPoseUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Pose.Serialize(aWriter);
}

void RequestVRPoseUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Pose.Deserialize(aReader);
}
