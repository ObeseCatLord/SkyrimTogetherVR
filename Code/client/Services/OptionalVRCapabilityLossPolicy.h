#pragma once

#include <cstdint>

#include <Structs/GameplayCapabilities.h>

namespace SkyrimTogetherVR
{
enum class OptionalVRCapabilityLoss : std::uint8_t
{
    None,
    VRHiggsRelay,
    PlanckPhysicsInterface002,
};

inline constexpr SkyrimTogether::Protocol::GameplayCapabilityMask kRuntimeMonitoredOptionalVRCapabilities =
    SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRHiggsRelay) |
    SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::PlanckPhysicsInterface002);

struct DeferredOptionalVRCapabilityCloseToken
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
};

[[nodiscard]] constexpr SkyrimTogether::Protocol::GameplayCapabilityMask ExtractNegotiatedOptionalVRCapabilities(
    const SkyrimTogether::Protocol::GameplayCapabilityMask aNegotiatedCapabilities) noexcept
{
    return aNegotiatedCapabilities & kRuntimeMonitoredOptionalVRCapabilities;
}

[[nodiscard]] constexpr SkyrimTogether::Protocol::GameplayCapabilityMask GetUnavailableNegotiatedOptionalVRCapabilities(
    const SkyrimTogether::Protocol::GameplayCapabilityMask aNegotiatedOptionalCapabilities,
    const bool aHiggsOperational, const bool aPlanckOperational) noexcept
{
    using namespace SkyrimTogether::Protocol;
    auto unavailable = GameplayCapabilityMask{};
    if (HasCapability(aNegotiatedOptionalCapabilities, GameplayCapability::VRHiggsRelay) && !aHiggsOperational)
        unavailable |= ToMask(GameplayCapability::VRHiggsRelay);
    if (HasCapability(aNegotiatedOptionalCapabilities, GameplayCapability::PlanckPhysicsInterface002) && !aPlanckOperational)
        unavailable |= ToMask(GameplayCapability::PlanckPhysicsInterface002);
    return unavailable;
}

[[nodiscard]] constexpr OptionalVRCapabilityLoss DetectOptionalVRCapabilityLoss(
    const SkyrimTogether::Protocol::GameplayCapabilityMask aNegotiatedOptionalCapabilities,
    const bool aHiggsOperational, const bool aPlanckOperational) noexcept
{
    using namespace SkyrimTogether::Protocol;
    const auto unavailableCapabilities = GetUnavailableNegotiatedOptionalVRCapabilities(
        aNegotiatedOptionalCapabilities, aHiggsOperational, aPlanckOperational);
    if (HasCapability(unavailableCapabilities, GameplayCapability::VRHiggsRelay))
        return OptionalVRCapabilityLoss::VRHiggsRelay;
    if (HasCapability(unavailableCapabilities, GameplayCapability::PlanckPhysicsInterface002))
        return OptionalVRCapabilityLoss::PlanckPhysicsInterface002;
    return OptionalVRCapabilityLoss::None;
}

[[nodiscard]] constexpr SkyrimTogether::Protocol::GameplayCapabilityMask ToMask(
    const OptionalVRCapabilityLoss aLoss) noexcept
{
    using namespace SkyrimTogether::Protocol;
    switch (aLoss)
    {
    case OptionalVRCapabilityLoss::VRHiggsRelay: return ToMask(GameplayCapability::VRHiggsRelay);
    case OptionalVRCapabilityLoss::PlanckPhysicsInterface002: return ToMask(GameplayCapability::PlanckPhysicsInterface002);
    case OptionalVRCapabilityLoss::None: return 0;
    }
    return 0;
}

[[nodiscard]] constexpr const char* OptionalVRCapabilityLossName(const OptionalVRCapabilityLoss aLoss) noexcept
{
    switch (aLoss)
    {
    case OptionalVRCapabilityLoss::VRHiggsRelay: return "VRHiggsRelay";
    case OptionalVRCapabilityLoss::PlanckPhysicsInterface002: return "PlanckPhysicsInterface002";
    case OptionalVRCapabilityLoss::None: return "none";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool IsCurrentDeferredOptionalVRCapabilityClose(
    const DeferredOptionalVRCapabilityCloseToken& acToken, const bool aConnected,
    const std::uint64_t aServerInstanceNonce, const std::uint64_t aConnectionGeneration) noexcept
{
    return aConnected && acToken.ServerInstanceNonce != 0 && acToken.ConnectionGeneration != 0 &&
           acToken.ServerInstanceNonce == aServerInstanceNonce &&
           acToken.ConnectionGeneration == aConnectionGeneration;
}
} // namespace SkyrimTogetherVR
