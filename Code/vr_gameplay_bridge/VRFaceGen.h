#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace RE
{
class Actor;
}

namespace SkyrimTogetherVR::GameplayAdapter::VRFaceGen
{
struct Tint
{
    std::string TexturePath{};
    std::uint32_t Color{};
    float Alpha{};
    std::uint8_t Type{};
};

enum class CompositionResult : std::uint8_t
{
    Success,
    Unavailable,
    Inactive,
    Rejected,
};

// Verifies the mapped compositor entry points and renderer before the caller
// changes a dynamic NPC base form.
[[nodiscard]] bool HasVerifiedTargets() noexcept;

// The compositor creates and installs the rendered tint texture on the
// actor's rebuilt face material. It does not change the actor base form.
[[nodiscard]] CompositionResult Compose(RE::Actor& a_actor, std::span<const Tint> a_tints) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::VRFaceGen
