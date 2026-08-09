#pragma once

#include <cstdint>

namespace SkyrimTogetherVR
{
namespace AssignmentCookie
{
// AssignCharacterResponse has no request kind, so local-player and NPC flows
// use disjoint non-zero cookie cycles.
constexpr std::uint32_t kFirstLocalPlayer = 1u;
constexpr std::uint32_t kLastLocalPlayer = 0xFFFFFFFFu;
constexpr std::uint32_t kFirstNpc = 2u;
constexpr std::uint32_t kLastNpc = 0xFFFFFFFEu;

[[nodiscard]] constexpr std::uint32_t TakeLocalPlayer(std::uint32_t& arNextCookie) noexcept
{
    const auto cookie = arNextCookie;
    arNextCookie = cookie == kLastLocalPlayer ? kFirstLocalPlayer : cookie + 2u;
    return cookie;
}

[[nodiscard]] constexpr std::uint32_t TakeNpc(std::uint32_t& arNextCookie) noexcept
{
    const auto cookie = arNextCookie;
    arNextCookie = cookie == kLastNpc ? kFirstNpc : cookie + 2u;
    return cookie;
}
} // namespace AssignmentCookie
} // namespace SkyrimTogetherVR
