#pragma once

#include "Message.h"

#include <Structs/QuestLog.h>

#include <cstdint>
#include <cstddef>

struct NotifyQuestResync final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuestResync;
    static constexpr std::size_t kMaximumEntries = 4096;

    NotifyQuestResync()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        if (OwnerPlayerId == 0 || RequestId == 0 || CanonicalRevision == 0 || !Snapshot.IsDecodedValid ||
            Snapshot.Entries.size() > kMaximumEntries || (HasParty ? PartyId == 0 : PartyId != 0))
            return false;
        for (std::size_t index = 0; index < Snapshot.Entries.size(); ++index)
        {
            if (!Snapshot.Entries[index].Id)
                return false;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (Snapshot.Entries[prior].Id == Snapshot.Entries[index].Id)
                    return false;
        }
        return true;
    }

    std::uint32_t OwnerPlayerId{};
    std::uint32_t RequestId{};
    std::uint64_t CanonicalRevision{};
    bool HasParty{};
    std::uint32_t PartyId{};
    QuestLog Snapshot{};
    bool IsDecodedValid{true};
};
