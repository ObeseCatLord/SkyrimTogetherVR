#pragma once

#include "pch.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrActorAction
{
using PerformAction = std::uint8_t(__fastcall*)(void*, RE::TESActionData*) noexcept;

[[nodiscard]] bool Initialize() noexcept;
void Reset() noexcept;
[[nodiscard]] bool IsReady() noexcept;

[[nodiscard]] PerformAction GetPerformAction() noexcept;
[[nodiscard]] void* GetActorMediator() noexcept;
[[nodiscard]] bool IsActorMediator(const void* a_candidate) noexcept;
[[nodiscard]] bool IsTesActionData(const RE::TESActionData* a_data) noexcept;

// This calls VR's verified complex-action branch. That branch performs the
// trailing animation-variable application before returning.
[[nodiscard]] std::uint8_t ForceAction(void* a_mediator, RE::TESActionData* a_data) noexcept;

class ReplayActionData final
{
public:
    ReplayActionData() = default;
    ~ReplayActionData() noexcept;

    ReplayActionData(const ReplayActionData&) = delete;
    ReplayActionData& operator=(const ReplayActionData&) = delete;

    [[nodiscard]] RE::TESActionData* Construct() noexcept;

private:
    alignas(RE::TESActionData) std::array<std::byte, sizeof(RE::TESActionData)> _storage{};
    bool _constructed{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrActorAction
