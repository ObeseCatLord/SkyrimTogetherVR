#include <Structs/ReferenceUpdate.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool ReferenceUpdate::operator==(const ReferenceUpdate& acRhs) const noexcept
{
    return UpdatedMovement == acRhs.UpdatedMovement && ActionEvents == acRhs.ActionEvents;
}

bool ReferenceUpdate::operator!=(const ReferenceUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ReferenceUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    UpdatedMovement.Serialize(aWriter);

    const auto count = std::min(ActionEvents.size(), kMaximumActionEvents);
    Serialization::WriteVarInt(aWriter, count);

    for (std::size_t index = 0; index < count; ++index)
        ActionEvents[index].GenerateDifferential(ActionEvent{}, aWriter);
}

void ReferenceUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    *this = {};
    UpdatedMovement.Deserialize(aReader);
    if (!UpdatedMovement.IsDecodedValid)
    {
        IsDecodedValid = false;
        return;
    }

    const auto count = Serialization::ReadVarInt(aReader);
    if (count > kMaximumActionEvents)
    {
        IsDecodedValid = false;
        return;
    }
    try
    {
        ActionEvents.resize(static_cast<std::size_t>(count));
        for (auto& action : ActionEvents)
        {
            action.ApplyDifferential(aReader);
            if (!action.IsDecodedValid)
            {
                ActionEvents.clear();
                IsDecodedValid = false;
                return;
            }
        }
    }
    catch (...)
    {
        ActionEvents.clear();
        IsDecodedValid = false;
    }
}
