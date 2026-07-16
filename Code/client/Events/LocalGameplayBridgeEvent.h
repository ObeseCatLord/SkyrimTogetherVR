#pragma once

#include <type_traits>

#include <vr_common/VRGameplayBridge.h>

namespace SkyrimTogetherVR
{
// Dispatcher payload for a validated, fixed-size action received from the
// mapped CommonLib bridge. It deliberately carries the wire record by value:
// no game pointers, ownership, or variable-sized appearance data cross this
// boundary.
struct LocalGameplayBridgeEvent final
{
    GameplayBridge::EventRecord Record;
};

static_assert(sizeof(LocalGameplayBridgeEvent) == sizeof(GameplayBridge::EventRecord));
static_assert(alignof(LocalGameplayBridgeEvent) == alignof(GameplayBridge::EventRecord));
static_assert(std::is_standard_layout_v<LocalGameplayBridgeEvent>);
static_assert(std::is_trivially_copyable_v<LocalGameplayBridgeEvent>);
} // namespace SkyrimTogetherVR
