#pragma once

#include <array>
#include <cstdint>

namespace RE
{
class TESObjectREFR;
}

namespace SkyrimTogetherVR::GameplayAdapter::RemoteSaveExclusion
{
// The VR-only TESObjectREFR operation below sets RecordFlags::kTemporary.
// Do not replace this pinned direct RVA with the CommonLib relocation ID.
inline constexpr std::uintptr_t kSetTemporaryVrRva = 0x01A4A50;
inline constexpr std::array<std::uint8_t, 11> kSetTemporaryVrPrologue{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xD2, 0x48, 0x8B, 0xD9,
};
inline constexpr std::uint32_t kTemporaryFormFlag = 0x00004000U;

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kSetTemporaryVrRva == 0x01A4A50 && kSetTemporaryVrPrologue.size() == 11;
}

// This policy is only for validating the typed TESForm flag read after the
// engine operation. It must not be used to write form flags directly.
[[nodiscard]] constexpr bool HasTemporaryFormFlag(const std::uint32_t a_formFlags) noexcept
{
    return (a_formFlags & kTemporaryFormFlag) != 0;
}

[[nodiscard]] constexpr bool ShouldLogValidationFailure(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}

[[nodiscard]] bool ValidateTarget() noexcept;
[[nodiscard]] bool MarkTemporary(RE::TESObjectREFR& a_reference) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::RemoteSaveExclusion
