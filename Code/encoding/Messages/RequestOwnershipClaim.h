#pragma once

#include "Message.h"
#include "Structs/ActorData.h"

struct RequestOwnershipClaim final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestOwnershipClaim;

    RequestOwnershipClaim()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestOwnershipClaim() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestOwnershipClaim& achRhs) const noexcept
    {
        return ServerId == achRhs.ServerId && GrantToken == achRhs.GrantToken &&
               NewActorData == achRhs.NewActorData && GetOpcode() == achRhs.GetOpcode();
    }

    uint32_t ServerId{};
    uint64_t GrantToken{};
    ActorData NewActorData{};
};
