#pragma once

#include "Message.h"

#include <Structs/ActionEvent.h>

struct ClientActorActionRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kClientActorActionRequest;

    ClientActorActionRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ClientActorActionRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Action == acRhs.Action;
    }

    uint32_t ServerId{};
    ActionEvent Action{};
};
