#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>

struct VRActivationEvent
{
    bool operator==(const VRActivationEvent& acRhs) const noexcept;
    bool operator!=(const VRActivationEvent& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    GameId ObjectId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    uint8_t FormType{0};
    uint8_t OpenState{0};
};
