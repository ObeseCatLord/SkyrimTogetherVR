#pragma once

#include "Message.h"

#include <Structs/VRHiggsState.h>

struct NotifyVRHiggsState final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyVRHiggsState;

    NotifyVRHiggsState()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyVRHiggsState& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && State == acRhs.State;
    }

    uint32_t PlayerId{0};
    VRHiggsState State{};
};
