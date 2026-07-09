#pragma once

#include <cstdint>

#include <Structs/GameId.h>
#include <TiltedCore/Buffer.hpp>

struct VREquipmentUpdate
{
    bool operator==(const VREquipmentUpdate& acRhs) const noexcept;
    bool operator!=(const VREquipmentUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Sequence{0};
    bool WeaponDrawn{false};
    bool WeaponFullyDrawn{false};
    GameId LeftWeapon{};
    GameId RightWeapon{};
    GameId Ammo{};
    GameId LeftSpell{};
    GameId RightSpell{};
    GameId PowerOrShout{};
};
