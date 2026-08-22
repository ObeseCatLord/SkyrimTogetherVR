#pragma once

#include "Message.h"

#include <cstdint>

struct NotifyMountResync final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyMountResync;

    NotifyMountResync()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return RiderId != 0 && RequestId != 0 && CanonicalRevision != 0;
    }

    std::uint32_t RiderId{};
    std::uint32_t RequestId{};
    std::uint64_t CanonicalRevision{};
    // Zero is the authoritative unmounted sentinel.
    std::uint32_t MountId{};
    bool IsDecodedValid{true};
};
