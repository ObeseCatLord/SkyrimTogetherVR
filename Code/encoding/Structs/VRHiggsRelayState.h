#pragma once

#include <cstdint>

struct VRHiggsRelayState
{
    bool BridgeLoaded{false};
    bool Detected{false};
    bool InterfaceAvailable{false};
    bool CallbacksRegistered{false};
    bool SnapshotAvailable{false};
    std::uint32_t SnapshotSequence{0};
};

[[nodiscard]] constexpr bool IsVRHiggsRelayOperational(const VRHiggsRelayState& acState) noexcept
{
    return acState.BridgeLoaded && acState.Detected && acState.InterfaceAvailable &&
           acState.CallbacksRegistered && acState.SnapshotAvailable && acState.SnapshotSequence != 0;
}
