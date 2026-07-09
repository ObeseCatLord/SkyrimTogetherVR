#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Buffer.hpp>

struct VRCombatHitEvent
{
    bool operator==(const VRCombatHitEvent& acRhs) const noexcept;
    bool operator!=(const VRCombatHitEvent& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    GameId HitterId{};
    GameId HitteeId{};
    GameId SourceId{};
    GameId ProjectileId{};
    Vector3_NetQuantize HitterPosition{};
    Vector3_NetQuantize HitteePosition{};
    uint32_t RawHitFlags{0};
    uint8_t HitterFormType{0};
    uint8_t HitteeFormType{0};
    bool PlanckHit{false};
    bool HitterIsPlayer{false};
    bool HitteeIsPlayer{false};
};
