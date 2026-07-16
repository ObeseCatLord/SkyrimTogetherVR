#pragma once

#include <type_traits>

#include <vr_common/VRGameplayBridge.h>

namespace SkyrimTogetherVR
{
struct RemoteGameplayBridgeResultEvent final
{
    GameplayBridge::EventRecord Record;
};

static_assert(std::is_standard_layout_v<RemoteGameplayBridgeResultEvent>);
static_assert(std::is_trivially_copyable_v<RemoteGameplayBridgeResultEvent>);
} // namespace SkyrimTogetherVR
