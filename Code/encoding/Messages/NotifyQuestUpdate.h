#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct NotifyQuestUpdate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuestUpdate;

    NotifyQuestUpdate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyQuestUpdate& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && OwnerPlayerId == acRhs.OwnerPlayerId && CanonicalRevision == acRhs.CanonicalRevision && Id == acRhs.Id && Stage == acRhs.Stage && Status == acRhs.Status && ClientQuestType == acRhs.ClientQuestType; }

    enum StatusCode : uint8_t
    {
        StageUpdate,
        Started,
        Stopped
    };

    [[nodiscard]] bool IsValid() const noexcept
    {
        return OwnerPlayerId != 0 && CanonicalRevision != 0 && Id && ClientQuestType <= 11 &&
               (Status == StageUpdate || Status == Started || Status == Stopped);
    }

    uint32_t OwnerPlayerId{};
    uint64_t CanonicalRevision{};
    GameId Id;
    uint16_t Stage;
    uint8_t Status;
    uint8_t ClientQuestType;
};
