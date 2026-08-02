#pragma once

#include <cstddef>

#include <Structs/Movement.h>
#include <Structs/ActionEvent.h>

using TiltedPhoques::Buffer;
using TiltedPhoques::Vector;

struct ReferenceUpdate
{
    static constexpr std::size_t kMaximumActionEvents = 0xFF;

    ReferenceUpdate() = default;
    ~ReferenceUpdate() = default;

    bool operator==(const ReferenceUpdate& acRhs) const noexcept;
    bool operator!=(const ReferenceUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    Movement UpdatedMovement{};
    Vector<ActionEvent> ActionEvents{};
    bool IsDecodedValid{true};
};
