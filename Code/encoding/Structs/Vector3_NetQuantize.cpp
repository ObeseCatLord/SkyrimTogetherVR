#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Serialization.hpp>

#include <algorithm>
#include <cmath>

using TiltedPhoques::Serialization;

namespace
{
constexpr uint64_t kComponentMask = (uint64_t{1} << 20) - 1;

uint64_t PackMagnitude(const float aValue) noexcept
{
    // Converting a non-finite or out-of-range float to an integer is either
    // undefined or implementation-defined. Encode non-finite values as zero
    // and saturate finite values to the exact twenty-bit wire magnitude.
    if (!std::isfinite(aValue))
        return 0;

    const auto magnitude = std::min(std::fabs(aValue), Vector3_NetQuantize::kMaximumComponentMagnitude);
    return static_cast<uint64_t>(magnitude) & kComponentMask;
}
} // namespace

bool Vector3_NetQuantize::operator==(const Vector3_NetQuantize& acRhs) const noexcept
{
    return Pack() == acRhs.Pack();
}

bool Vector3_NetQuantize::operator!=(const Vector3_NetQuantize& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

Vector3_NetQuantize& Vector3_NetQuantize::operator=(const glm::vec3& acRhs) noexcept
{
    glm::vec3::operator=(acRhs);
    return *this;
}

bool Vector3_NetQuantize::IsInRange(const glm::vec3& acValue) noexcept
{
    return std::isfinite(acValue.x) && std::isfinite(acValue.y) && std::isfinite(acValue.z) && std::fabs(acValue.x) <= kMaximumComponentMagnitude &&
           std::fabs(acValue.y) <= kMaximumComponentMagnitude && std::fabs(acValue.z) <= kMaximumComponentMagnitude;
}

void Vector3_NetQuantize::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Pack(), 64);
}

void Vector3_NetQuantize::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t data;
    aReader.ReadBits(data, 64);

    Unpack(data);
}

void Vector3_NetQuantize::Unpack(uint64_t aValue) noexcept
{
    int32_t xSign = (aValue & 1) != 0;
    int32_t ySign = (aValue & 2) != 0;
    int32_t zSign = (aValue & 4) != 0;

    auto xValue = static_cast<float>((aValue >> 3) & kComponentMask);
    auto yValue = static_cast<float>((aValue >> 23) & kComponentMask);
    auto zValue = static_cast<float>((aValue >> 43) & kComponentMask);

    x = xSign ? -xValue : xValue;
    y = ySign ? -yValue : yValue;
    z = zSign ? -zValue : zValue;
}

uint64_t Vector3_NetQuantize::Pack() const noexcept
{
    uint64_t data = 0;

    const auto xMagnitude = PackMagnitude(x);
    const auto yMagnitude = PackMagnitude(y);
    const auto zMagnitude = PackMagnitude(z);

    data |= std::isfinite(x) && x < 0.0F ? 1 : 0;
    data |= std::isfinite(y) && y < 0.0F ? 2 : 0;
    data |= std::isfinite(z) && z < 0.0F ? 4 : 0;
    data |= xMagnitude << 3;
    data |= yMagnitude << 23;
    data |= zMagnitude << 43;

    return data;
}
