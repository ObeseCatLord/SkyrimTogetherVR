#pragma once

#include <cstddef>

namespace SkyrimTogetherVR::VRAssignmentLimits
{
// Bootstrap records are paged over the bridge, but TotalRecords describes the
// complete logical record sequence rather than its physical ring residency.
inline constexpr std::size_t kMaximumQuestEntries = 4096;
inline constexpr std::size_t kMaximumLogicalBootstrapRecords = 8192;
inline constexpr std::size_t kBootstrapPageRecords = 64;
static_assert(kBootstrapPageRecords <= kMaximumLogicalBootstrapRecords);
}
