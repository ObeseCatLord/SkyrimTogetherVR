#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/VREquipmentUpdate.h>

struct RemoteVREquipmentComponent
{
    RemoteVREquipmentComponent(uint32_t aPlayerId, const VREquipmentUpdate& acEquipment) noexcept
        : PlayerId(aPlayerId)
        , Equipment(acEquipment)
    {
    }

    uint32_t PlayerId{};
    VREquipmentUpdate Equipment{};
};
