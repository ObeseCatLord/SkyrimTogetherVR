#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/ObjectData.h>

struct NotifyActivate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActivate;
    static constexpr uint8_t kMaximumOpenState = ObjectData::kMaximumOpenState;
    NotifyActivate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Id && ActivatorId != 0 && PreActivationOpenState <= kMaximumOpenState &&
               (!HasPostActivationOpenState ? PostActivationOpenState == 0 :
                                              PostActivationOpenState > 0 && PostActivationOpenState <= kMaximumOpenState);
    }

    bool operator==(const NotifyActivate& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id && ActivatorId == acRhs.ActivatorId && PreActivationOpenState == acRhs.PreActivationOpenState && HasPostActivationOpenState == acRhs.HasPostActivationOpenState && PostActivationOpenState == acRhs.PostActivationOpenState; }

    GameId Id;
    uint32_t ActivatorId;
    uint8_t PreActivationOpenState;
    bool HasPostActivationOpenState{};
    uint8_t PostActivationOpenState{};
    bool IsDecodedValid{true};
};
