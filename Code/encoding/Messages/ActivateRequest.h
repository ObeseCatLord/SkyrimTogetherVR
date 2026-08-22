#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/ObjectData.h>

struct ActivateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kActivateRequest;
    static constexpr uint8_t kMaximumOpenState = ObjectData::kMaximumOpenState;

    ActivateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Id && CellId && ActivatorId != 0 && PreActivationOpenState <= kMaximumOpenState &&
               (!HasPostActivationOpenState ? PostActivationOpenState == 0 :
                                              PostActivationOpenState > 0 && PostActivationOpenState <= kMaximumOpenState);
    }

    bool operator==(const ActivateRequest& acRhs) const noexcept { return Id == acRhs.Id && ActivatorId == acRhs.ActivatorId && CellId == acRhs.CellId && PreActivationOpenState == acRhs.PreActivationOpenState && HasPostActivationOpenState == acRhs.HasPostActivationOpenState && PostActivationOpenState == acRhs.PostActivationOpenState && GetOpcode() == acRhs.GetOpcode(); }

    GameId Id;
    GameId CellId;
    uint32_t ActivatorId;
    uint8_t PreActivationOpenState;
    bool HasPostActivationOpenState{};
    uint8_t PostActivationOpenState{};
    bool IsDecodedValid{true};
};
