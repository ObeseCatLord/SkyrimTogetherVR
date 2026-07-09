#pragma once

#include "Message.h"

#include <Structs/VRHiggsState.h>

struct RequestVRHiggsState final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestVRHiggsState;

    RequestVRHiggsState()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestVRHiggsState& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && State == acRhs.State;
    }

    VRHiggsState State{};
};
