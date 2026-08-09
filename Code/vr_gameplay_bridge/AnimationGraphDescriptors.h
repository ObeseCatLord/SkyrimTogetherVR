#pragma once

#include <span>
#include <string_view>

#include <vr_common/VRAnimationGraphProtocol.h>

namespace RE { class Actor; class BSAnimationGraphManager; }

namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
{
struct AnimationGraphDescriptor
{
    std::uint64_t Key{};
    std::span<const std::string_view> Booleans{};
    std::span<const std::string_view> Floats{};
    std::span<const std::string_view> Integers{};
    [[nodiscard]] constexpr std::size_t VariableCount() const noexcept
    { return Booleans.size() + Floats.size() + Integers.size(); }
};

struct ResolvedDescriptor
{
    const AnimationGraphDescriptor* Descriptor{};
    const RE::BSAnimationGraphManager* Manager{};
    [[nodiscard]] explicit operator bool() const noexcept { return Descriptor != nullptr && Manager != nullptr; }
};

[[nodiscard]] std::span<const AnimationGraphDescriptor> Catalog() noexcept;
void ResetCache() noexcept;
[[nodiscard]] bool Resolve(RE::Actor& a_actor, ResolvedDescriptor& ar_result) noexcept;
[[nodiscard]] bool ManagerMatches(RE::Actor& a_actor, const ResolvedDescriptor& a_descriptor) noexcept;
[[nodiscard]] bool Validate(RE::Actor& a_actor, const ResolvedDescriptor& a_descriptor) noexcept;
[[nodiscard]] bool Capture(RE::Actor& a_actor, AnimationGraphProtocol::SnapshotBuffer& ar_snapshot) noexcept;
[[nodiscard]] bool MatchesCounts(const ResolvedDescriptor& a_descriptor,
                                 const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
