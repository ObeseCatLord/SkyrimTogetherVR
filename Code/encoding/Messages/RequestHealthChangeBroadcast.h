#pragma once

#include "Message.h"

#include <cstdint>

struct RequestHealthChangeBroadcast final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestHealthChangeBroadcast;

    RequestHealthChangeBroadcast()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestHealthChangeBroadcast& acRhs) const noexcept
    {
        return Id == acRhs.Id && DeltaHealth == acRhs.DeltaHealth && AttackerId == acRhs.AttackerId &&
               ActionNonce == acRhs.ActionNonce && GetOpcode() == acRhs.GetOpcode();
    }

    uint32_t Id{};
    float DeltaHealth{};
    // Zero values retain the desktop health-delta wire semantics. VR physical
    // damage supplies the sender-owned attacker and a strictly increasing nonce.
    uint32_t AttackerId{};
    uint64_t ActionNonce{};
};
