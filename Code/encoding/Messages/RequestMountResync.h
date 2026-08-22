#pragma once

#include "Message.h"

#include <cstdint>

struct RequestMountResync final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestMountResync;

    RequestMountResync()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return RiderId != 0 && RequestId != 0;
    }

    std::uint32_t RiderId{};
    std::uint32_t RequestId{};
    std::uint64_t KnownRevision{};
    bool IsDecodedValid{true};
};
