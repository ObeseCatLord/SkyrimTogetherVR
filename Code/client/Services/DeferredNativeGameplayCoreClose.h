#pragma once

#include <cstdint>

// A deferred close may only affect the authenticated transport instance that
// observed the fault. Server nonce plus connection generation is the
// protocol-level identity that survives the event-dispatch boundary.
struct DeferredNativeGameplayCoreCloseToken
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
};

[[nodiscard]] constexpr bool IsCurrentDeferredNativeGameplayCoreClose(
    const DeferredNativeGameplayCoreCloseToken& acToken,
    const bool aConnected,
    const std::uint64_t aServerInstanceNonce,
    const std::uint64_t aConnectionGeneration) noexcept
{
    return aConnected && acToken.ServerInstanceNonce != 0 && acToken.ConnectionGeneration != 0 &&
           acToken.ServerInstanceNonce == aServerInstanceNonce &&
           acToken.ConnectionGeneration == aConnectionGeneration;
}
