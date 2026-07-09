#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Rotator2_NetQuantize.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>

struct VRMovementUpdate
{
    bool operator==(const VRMovementUpdate& acRhs) const noexcept;
    bool operator!=(const VRMovementUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    Rotator2_NetQuantize Rotation{};
    float Direction{0.0f};
};
