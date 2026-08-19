#pragma once

#include <array>
#include <cstdint>

namespace RE
{
class TESQuest;
}

namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
{
// These are the desktop address-library IDs and the audited Skyrim VR 1.4.15
// RVA for TESQuest::SetStage. Keep the ABI dependency bridge-owned rather than
// carrying a local CommonLib fork for one method.
inline constexpr std::uint64_t kSetStageSeId = 24482;
inline constexpr std::uint64_t kSetStageAeId = 25004;
inline constexpr std::uint64_t kSetStageVrRva = 0x03803D0;
inline constexpr std::array<std::uint8_t, 24> kSetStageVrPrologue{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81,
    0xDC, 0x00, 0x00, 0x00, 0x01, 0x44, 0x0F, 0xB7,
    0xC2, 0x48, 0x8B, 0xF9, 0x74, 0x72, 0x48, 0x8D,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kSetStageVrRva == 0x03803D0 && kSetStageVrPrologue.size() == 24;
}

[[nodiscard]] constexpr bool IsStageRecordDone(
    const std::uint16_t a_recordStage,
    const std::uint8_t a_recordFlags,
    const std::uint16_t a_requestedStage) noexcept
{
    return a_recordStage == a_requestedStage && (a_recordFlags & 0x01U) != 0;
}

[[nodiscard]] bool ValidateTarget() noexcept;
[[nodiscard]] bool SetStage(RE::TESQuest& a_quest, std::uint16_t a_stage) noexcept;
[[nodiscard]] bool IsStageDone(const RE::TESQuest& a_quest, std::uint16_t a_stage) noexcept;
void SetActive(RE::TESQuest& a_quest, bool a_active) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
