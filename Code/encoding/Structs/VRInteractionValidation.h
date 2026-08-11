#pragma once

#include <cmath>

#include <Structs/Vector3_NetQuantize.h>

namespace SkyrimTogether::VR
{
// This matches the gameplay bridge's native HIGGS interaction guard. Keep
// relays from authorizing values that the bridge will necessarily reject.
inline constexpr float kMaximumHiggsInteractionMagnitude = 1'000'000.0F;

[[nodiscard]] inline bool IsHiggsInteractionScalarValid(const float aValue) noexcept
{
    return std::isfinite(aValue) && std::abs(aValue) <= kMaximumHiggsInteractionMagnitude;
}

[[nodiscard]] inline bool IsHiggsMutationPayloadValid(const float aMass,
                                                       const float aSeparatingVelocity) noexcept
{
    return IsHiggsInteractionScalarValid(aMass) && IsHiggsInteractionScalarValid(aSeparatingVelocity);
}

[[nodiscard]] inline bool IsVRGrabPositionValid(const Vector3_NetQuantize& acPosition) noexcept
{
    return IsHiggsInteractionScalarValid(acPosition.x) && IsHiggsInteractionScalarValid(acPosition.y) &&
           IsHiggsInteractionScalarValid(acPosition.z);
}
} // namespace SkyrimTogether::VR
