#pragma once

#include <cstddef>
#include "Message.h"
#include <Structs/ReferenceUpdate.h>

using TiltedPhoques::String;

struct ServerReferencesMoveRequest final : ServerMessage
{
    static constexpr std::size_t kMaximumUpdates = 512;

    static constexpr ServerOpcode Opcode = kServerReferencesMoveRequest;

    ServerReferencesMoveRequest()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ServerReferencesMoveRequest& acRhs) const noexcept { return Updates == acRhs.Updates && Tick == acRhs.Tick && GetOpcode() == acRhs.GetOpcode(); }

    uint64_t Tick{};
    TiltedPhoques::Map<uint32_t, ReferenceUpdate> Updates{};
    bool IsDecodedValid{true};
};
