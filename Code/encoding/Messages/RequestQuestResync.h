#pragma once

#include "Message.h"

#include <cstdint>

// The requester never supplies quest contents. The server authorizes this
// owner against the authenticated requester and snapshots that owner's log.
struct RequestQuestResync final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestQuestResync;

    RequestQuestResync()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept { return OwnerPlayerId != 0 && RequestId != 0; }

    std::uint32_t OwnerPlayerId{};
    std::uint32_t RequestId{};
    std::uint64_t KnownRevision{};
    bool IsDecodedValid{true};
};
