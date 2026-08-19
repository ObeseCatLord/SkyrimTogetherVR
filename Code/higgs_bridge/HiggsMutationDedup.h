#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::HiggsBridge
{
enum class MutationKind : std::uint8_t
{
    Pulled,
    Grabbed,
    Dropped,
    Stashed,
    Consumed,
};

// HIGGS has no callback event id. Treat repeat callbacks for the same
// transition inside a single short frame window as one physical gesture.
class MutationDeduplicator
{
public:
    static constexpr std::uint64_t kWindowMilliseconds = 25;

    [[nodiscard]] bool Accept(const MutationKind a_kind, const bool a_isLeft,
                              const std::uint32_t a_formId,
                              const std::uint64_t a_nowMilliseconds) noexcept
    {
        // A missing form cannot identify a physical object. Let the existing
        // validation path reject it rather than merging unrelated callbacks.
        if (a_formId == 0)
            return true;

        for (const auto& entry : m_entries) {
            if (!entry.Valid || entry.Kind != a_kind || entry.IsLeft != a_isLeft ||
                entry.FormId != a_formId || a_nowMilliseconds < entry.TimestampMilliseconds ||
                a_nowMilliseconds - entry.TimestampMilliseconds > kWindowMilliseconds)
                continue;
            return false;
        }

        m_entries[m_next] = {
            .Kind = a_kind,
            .IsLeft = a_isLeft,
            .FormId = a_formId,
            .TimestampMilliseconds = a_nowMilliseconds,
            .Valid = true,
        };
        m_next = (m_next + 1) % m_entries.size();
        return true;
    }

private:
    struct Entry
    {
        MutationKind Kind{};
        bool IsLeft{};
        std::uint32_t FormId{};
        std::uint64_t TimestampMilliseconds{};
        bool Valid{};
    };

    std::array<Entry, 16> m_entries{};
    std::size_t m_next{};
};
} // namespace SkyrimTogetherVR::HiggsBridge
