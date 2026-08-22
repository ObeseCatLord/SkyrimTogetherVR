#pragma once

using TiltedPhoques::Buffer;

//! Network optimized 3d vector
struct Vector3_NetQuantize : glm::vec3
{
    // Each signed component owns one sign bit and twenty magnitude bits.
    // Keep this public so bridge-facing producers can reject values that
    // would otherwise be saturated while being serialized.
    static constexpr float kMaximumComponentMagnitude = 1'048'575.0F;

    Vector3_NetQuantize() = default;
    ~Vector3_NetQuantize() = default;

    bool operator==(const Vector3_NetQuantize& acRhs) const noexcept;
    bool operator!=(const Vector3_NetQuantize& acRhs) const noexcept;

    Vector3_NetQuantize& operator=(const glm::vec3& acRhs) noexcept;

    [[nodiscard]] static bool IsInRange(const glm::vec3& acValue) noexcept;

    /**
     * Serialize to a buffer.
     * @param aWriter Writer wrapping the buffer.
     */
    void Serialize(Buffer::Writer& aWriter) const noexcept;
    /**
     * Deserialize from a buffer.
     * @param aReader Reader wrapping the buffer.
     */
    void Deserialize(Buffer::Reader& aReader) noexcept;
    /**
     * Packs the vector into a 64 bits representation of the network vector
     */
    [[nodiscard]] uint64_t Pack() const noexcept;
    /**
     * Unpack a 64 bits representation of a network vector
     * @param aValue The 64bits representation of a vector.
     */
    void Unpack(uint64_t aValue) noexcept;
};
