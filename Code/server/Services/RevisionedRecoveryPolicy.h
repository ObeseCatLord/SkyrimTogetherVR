#pragma once

#include <cstdint>
#include <limits>

namespace SkyrimTogether::RevisionedRecovery
{
// A request may acknowledge only a revision the server has issued. This makes
// a forged future revision fail closed and guarantees each issued response has
// a strictly increasing sequence number for its target.
[[nodiscard]] constexpr bool CanIssueSnapshot(
    const std::uint64_t aCurrentRevision, const std::uint64_t aKnownRevision) noexcept
{
    return aKnownRevision <= aCurrentRevision &&
           aCurrentRevision != std::numeric_limits<std::uint64_t>::max();
}

// Receivers bind a response to the outstanding request and never replace an
// already applied snapshot with an equal or older server revision.
[[nodiscard]] constexpr bool IsLateOrStaleResponse(
    const std::uint32_t aExpectedRequestId, const std::uint32_t aResponseRequestId,
    const std::uint64_t aAppliedRevision, const std::uint64_t aResponseRevision) noexcept
{
    return aExpectedRequestId == 0 || aExpectedRequestId != aResponseRequestId ||
           aResponseRevision == 0 || aResponseRevision <= aAppliedRevision;
}
} // namespace SkyrimTogether::RevisionedRecovery
