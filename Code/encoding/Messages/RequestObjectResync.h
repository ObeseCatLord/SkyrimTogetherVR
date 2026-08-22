#pragma once

#include "Message.h"

#include <cstdint>

struct RequestObjectResync final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestObjectResync;

    RequestObjectResync()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return ServerId != 0 && RequestId != 0;
    }

    std::uint32_t ServerId{};
    std::uint32_t RequestId{};
    std::uint64_t KnownRevision{};
    bool IsDecodedValid{true};
};
