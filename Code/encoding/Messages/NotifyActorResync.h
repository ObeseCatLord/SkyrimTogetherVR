#pragma once

#include "Message.h"

#include <Messages/CharacterSpawnRequest.h>

#include <cstdint>

struct NotifyActorResync final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActorResync;

    NotifyActorResync()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return ServerId != 0 && RequestId != 0 && CanonicalRevision != 0 && Snapshot.IsDecodedValid &&
               Snapshot.ServerId == ServerId;
    }

    bool operator==(const NotifyActorResync& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId &&
               RequestId == acRhs.RequestId && CanonicalRevision == acRhs.CanonicalRevision &&
               Snapshot == acRhs.Snapshot;
    }

    std::uint32_t ServerId{};
    std::uint32_t RequestId{};
    std::uint64_t CanonicalRevision{};
    CharacterSpawnRequest Snapshot{};
    bool IsDecodedValid{true};
};
