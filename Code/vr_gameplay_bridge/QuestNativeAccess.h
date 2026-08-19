#pragma once

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

[[nodiscard]] bool SetStage(RE::TESQuest& a_quest, std::uint16_t a_stage) noexcept;
void SetActive(RE::TESQuest& a_quest, bool a_active) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
