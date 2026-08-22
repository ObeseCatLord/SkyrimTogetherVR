#pragma once

#include "Message.h"

#include <Structs/ObjectData.h>

#include <cstdint>

struct NotifyObjectResync final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyObjectResync;

    NotifyObjectResync()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return ServerId != 0 && RequestId != 0 && CanonicalRevision != 0 && Snapshot.IsDecodedValid &&
               Snapshot.ServerId == ServerId && Snapshot.Id && Snapshot.CellId &&
               Snapshot.HasValidCurrentOpenState();
    }

    std::uint32_t ServerId{};
    std::uint32_t RequestId{};
    std::uint64_t CanonicalRevision{};
    ObjectData Snapshot{};
    bool IsDecodedValid{true};
};
