#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct RequestQuestUpdate final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestQuestUpdate;

    RequestQuestUpdate()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestQuestUpdate& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && OwnerPlayerId == acRhs.OwnerPlayerId && Id == acRhs.Id && Stage == acRhs.Stage && Status == acRhs.Status && ClientQuestType == acRhs.ClientQuestType; }

    enum StatusCode : uint8_t
    {
        StageUpdate,
        Started,
        Stopped
    };

    // The server binds a nonzero claim to the authenticated sender. Zero is
    // retained for legacy local producers; it is never forwarded as authority.
    uint32_t OwnerPlayerId{};
    GameId Id;
    uint16_t Stage;
    uint8_t Status;
    uint8_t ClientQuestType;
};
