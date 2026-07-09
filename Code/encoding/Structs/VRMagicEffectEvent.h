#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>

struct VRMagicEffectEvent
{
    bool operator==(const VRMagicEffectEvent& acRhs) const noexcept;
    bool operator!=(const VRMagicEffectEvent& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    GameId EffectId{};
    GameId CasterId{};
    GameId TargetId{};
    Vector3_NetQuantize CasterPosition{};
    Vector3_NetQuantize TargetPosition{};
    uint8_t CasterFormType{0};
    uint8_t TargetFormType{0};
    bool CasterIsPlayer{false};
    bool TargetIsPlayer{false};
};
