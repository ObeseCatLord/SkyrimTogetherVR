#pragma once

#include "Message.h"

#include <cstdint>

struct RequestActorResync final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActorResync;
    static constexpr std::uint8_t kActorSnapshot = 1u << 0;
    static constexpr std::uint8_t kEquipmentSnapshot = 1u << 1;
    static constexpr std::uint8_t kKnownScopes = kActorSnapshot | kEquipmentSnapshot;

    RequestActorResync()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return ServerId != 0 && RequestId != 0 && Scope != 0 && (Scope & ~kKnownScopes) == 0;
    }

    bool operator==(const RequestActorResync& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId &&
               RequestId == acRhs.RequestId && Scope == acRhs.Scope &&
               KnownActorRevision == acRhs.KnownActorRevision &&
               KnownEquipmentRevision == acRhs.KnownEquipmentRevision;
    }

    std::uint32_t ServerId{};
    std::uint32_t RequestId{};
    std::uint64_t KnownActorRevision{};
    std::uint64_t KnownEquipmentRevision{};
    std::uint8_t Scope{};
    bool IsDecodedValid{true};
};
