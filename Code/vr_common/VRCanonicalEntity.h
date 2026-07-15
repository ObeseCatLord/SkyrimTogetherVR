#pragma once

#include <cstdint>

namespace SkyrimTogetherVR::CanonicalEntity
{
constexpr std::uint64_t kMaximumEntityId = 0xFFFFFull;
constexpr std::uint32_t kMaximumEntityGeneration = 0xFFFu;
constexpr std::uint32_t kGenerationCycle = kMaximumEntityGeneration;
constexpr std::uint32_t kMaximumUnseenForwardGenerations = kGenerationCycle / 2;

enum class GenerationOrder : std::uint8_t
{
    Invalid,
    Same,
    Newer,
    OlderOrAmbiguous,
};

[[nodiscard]] constexpr bool IsValid(const std::uint64_t aEntityId, const std::uint32_t aGeneration) noexcept
{
    return aEntityId != 0 && aEntityId <= kMaximumEntityId && aGeneration != 0 && aGeneration <= kMaximumEntityGeneration;
}

[[nodiscard]] constexpr GenerationOrder CompareGenerations(
    const std::uint32_t aCandidate,
    const std::uint32_t aCurrent) noexcept
{
    if (aCandidate == 0 || aCandidate > kMaximumEntityGeneration || aCurrent == 0 || aCurrent > kMaximumEntityGeneration)
        return GenerationOrder::Invalid;
    if (aCandidate == aCurrent)
        return GenerationOrder::Same;

    const auto delta = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(aCandidate) + kGenerationCycle - aCurrent) % kGenerationCycle);
    return delta != 0 && delta <= kMaximumUnseenForwardGenerations ? GenerationOrder::Newer : GenerationOrder::OlderOrAmbiguous;
}

[[nodiscard]] constexpr bool CanCreateAfterDestroyedGeneration(
    const std::uint32_t aCandidateGeneration,
    const std::uint64_t aCandidateAction,
    const std::uint32_t aDestroyedGeneration,
    const std::uint64_t aDestroyAction) noexcept
{
    const auto order = CompareGenerations(aCandidateGeneration, aDestroyedGeneration);
    return order == GenerationOrder::Newer ||
           (order == GenerationOrder::Same && aCandidateAction > aDestroyAction);
}
} // namespace SkyrimTogetherVR::CanonicalEntity
