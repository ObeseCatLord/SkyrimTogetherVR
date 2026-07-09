#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>

struct VRProjectileEvent
{
    enum class Kind : uint8_t
    {
        kBowShot = 0,
        kSpellCast = 1,
    };

    bool operator==(const VRProjectileEvent& acRhs) const noexcept;
    bool operator!=(const VRProjectileEvent& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    Kind EventKind{Kind::kBowShot};
    GameId SourceId{};
    GameId WeaponId{};
    GameId AmmoId{};
    GameId SpellId{};
    Vector3_NetQuantize Origin{};
    Vector3_NetQuantize Destination{};
    float Power{0.0f};
    bool OriginValid{false};
    bool DestinationValid{false};
    bool IsSunGazing{false};
};
