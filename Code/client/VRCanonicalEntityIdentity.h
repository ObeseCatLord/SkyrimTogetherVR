#pragma once

#include <vr_common/VRCanonicalEntity.h>

#include <cstdint>

#include <entt/entity/entity.hpp>

namespace SkyrimTogetherVR::CanonicalEntity
{
struct BridgeIdentity
{
    std::uint64_t EntityId{};
    std::uint32_t EntityGeneration{};
};

using EntityTraits = entt::entt_traits<entt::entity>;

static_assert(entt::to_entity(static_cast<entt::entity>(entt::null)) == kMaximumEntityId);
static_assert(entt::to_version(static_cast<entt::entity>(entt::tombstone)) == kMaximumEntityGeneration);

[[nodiscard]] constexpr bool TrySplitServerId(const std::uint32_t aServerId, BridgeIdentity& arIdentity) noexcept
{
    const auto entity = static_cast<entt::entity>(aServerId);
    const auto slot = entt::to_entity(entity);
    const auto version = entt::to_version(entity);
    if (slot == kMaximumEntityId || version == kMaximumEntityGeneration)
        return false;

    arIdentity.EntityId = static_cast<std::uint64_t>(slot) + 1;
    arIdentity.EntityGeneration = static_cast<std::uint32_t>(version) + 1;
    return IsValid(arIdentity.EntityId, arIdentity.EntityGeneration);
}

[[nodiscard]] constexpr bool TryJoinServerId(
    const std::uint64_t aEntityId,
    const std::uint32_t aEntityGeneration,
    std::uint32_t& arServerId) noexcept
{
    if (!IsValid(aEntityId, aEntityGeneration))
        return false;

    const auto entity = EntityTraits::construct(
        static_cast<EntityTraits::entity_type>(aEntityId - 1),
        static_cast<EntityTraits::version_type>(aEntityGeneration - 1));
    arServerId = entt::to_integral(entity);
    return true;
}
} // namespace SkyrimTogetherVR::CanonicalEntity
