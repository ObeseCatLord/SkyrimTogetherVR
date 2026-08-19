#pragma once

#include <cstdint>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter::EquipmentPostconditionPolicy
{
struct WornItem
{
    std::uint32_t FormId{};
    std::uint32_t Count{};
    bool Worn{};
    bool WornLeft{};
    bool Weapon{};
    bool Ammo{};

    [[nodiscard]] constexpr bool operator==(const WornItem&) const noexcept = default;
};

enum ObservedField : std::uint8_t
{
    WornItems = 1u << 0,
    HandObjects = 1u << 1,
    SelectedMagic = 1u << 2,
};

inline constexpr std::uint8_t kCompleteStateFields = WornItems | HandObjects | SelectedMagic;
inline constexpr std::uint8_t kWornAndHandObjectFields = WornItems | HandObjects;

struct State
{
    std::uint8_t AvailableFields{};
    std::vector<WornItem> Worn{};
    std::uint32_t LeftObject{};
    std::uint32_t RightObject{};
    std::uint32_t LeftSpell{};
    std::uint32_t RightSpell{};
    std::uint32_t Shout{};
};

[[nodiscard]] constexpr bool HasFields(const State& a_state, const std::uint8_t a_required) noexcept
{
    return (a_state.AvailableFields & a_required) == a_required;
}

[[nodiscard]] inline bool MatchesFinalState(const State& a_requested, const State& a_observed) noexcept
{
    return HasFields(a_observed, kCompleteStateFields) &&
           a_requested.Worn == a_observed.Worn &&
           a_requested.LeftObject == a_observed.LeftObject &&
           a_requested.RightObject == a_observed.RightObject &&
           a_requested.LeftSpell == a_observed.LeftSpell &&
           a_requested.RightSpell == a_observed.RightSpell &&
           a_requested.Shout == a_observed.Shout;
}

[[nodiscard]] inline bool IsObjectWorn(
    const State& a_observed,
    const std::uint32_t a_formId,
    const bool a_leftHand) noexcept
{
    if (!HasFields(a_observed, WornItems))
        return false;
    for (const auto& item : a_observed.Worn) {
        if (item.FormId == a_formId && (a_leftHand ? item.WornLeft : item.Worn))
            return true;
    }
    return false;
}

[[nodiscard]] inline bool IsObjectUnequipped(
    const State& a_observed,
    const std::uint32_t a_formId) noexcept
{
    if (!HasFields(a_observed, kWornAndHandObjectFields))
        return false;
    if (a_observed.LeftObject == a_formId || a_observed.RightObject == a_formId)
        return false;
    for (const auto& item : a_observed.Worn) {
        if (item.FormId == a_formId && (item.Worn || item.WornLeft))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool IsObjectUnequipped(
    const State& a_observed,
    const std::uint32_t a_formId,
    const bool a_leftHand) noexcept
{
    if (!HasFields(a_observed, kWornAndHandObjectFields))
        return false;
    if ((a_leftHand ? a_observed.LeftObject : a_observed.RightObject) == a_formId)
        return false;
    for (const auto& item : a_observed.Worn) {
        if (item.FormId == a_formId && (a_leftHand ? item.WornLeft : item.Worn))
            return false;
    }
    return true;
}

[[nodiscard]] constexpr bool MatchesSelectedSpell(
    const State& a_observed,
    const std::uint32_t a_formId,
    const bool a_leftHand,
    const bool a_equipped) noexcept
{
    if (!HasFields(a_observed, SelectedMagic))
        return false;
    const auto selected = a_leftHand ? a_observed.LeftSpell : a_observed.RightSpell;
    return a_equipped ? selected == a_formId : selected != a_formId;
}

[[nodiscard]] constexpr bool MatchesSelectedShout(
    const State& a_observed,
    const std::uint32_t a_formId,
    const bool a_equipped) noexcept
{
    return HasFields(a_observed, SelectedMagic) &&
           (a_equipped ? a_observed.Shout == a_formId : a_observed.Shout != a_formId);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::EquipmentPostconditionPolicy
