#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/LockData.h>
#include <Game/Player.h>

struct ObjectComponent
{
    ObjectComponent(Player* apLastSender)
        : pLastSender(apLastSender)
    {
    }

    Player* pLastSender;
    LockData CurrentLockData{};
    // Canonical current state is authenticated only by snapshots or an
    // accepted activation's observed post-state; None is represented absent.
    bool HasCurrentOpenState{};
    std::uint8_t CurrentOpenState{};
};
