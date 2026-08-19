#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace RE
{
class Actor;
class ActorEquipManager;
class AIProcess;
class BGSEquipSlot;
class SpellItem;
class TESShout;
}

namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHookPolicy
{
enum class Operation : std::uint8_t
{
    EquipObject,
    UnequipObject,
    EquipSpell,
    UnequipSpell,
    EquipShout,
    UnequipShout,
};

enum class Disposition : std::uint8_t
{
    CallOriginal,
    Suppress,
};

// All targets are direct RVAs verified against the pinned Skyrim VR 1.4.15.0
// executable. Generated desktop aliases are deliberately not used here.
inline constexpr std::uintptr_t kEquipObjectVrRva = 0x0642E30;
inline constexpr std::array<std::uint8_t, 18> kEquipObjectVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x38, 0x48, 0x3B, 0x15, 0x66, 0x18, 0x98, 0x02,
};
inline constexpr std::uintptr_t kUnequipObjectVrRva = 0x06436C0;
inline constexpr std::array<std::uint8_t, 37> kUnequipObjectVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x3B, 0x15, 0xC3, 0x0F, 0x98, 0x02,
};
inline constexpr std::uintptr_t kEquipSpellVrRva = 0x0642B80;
inline constexpr std::array<std::uint8_t, 18> kEquipSpellVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x38, 0x48, 0x3B, 0x15, 0x16, 0x1B, 0x98, 0x02,
};
inline constexpr std::uintptr_t kUnequipSpellVrRva = 0x0643470;
inline constexpr std::array<std::uint8_t, 37> kUnequipSpellVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x3B, 0x15, 0x13, 0x12, 0x98, 0x02,
};
inline constexpr std::uintptr_t kEquipShoutVrRva = 0x06430E0;
inline constexpr std::array<std::uint8_t, 18> kEquipShoutVrPrologue{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x83,
    0xEC, 0x38, 0x48, 0x3B, 0x15, 0xB6, 0x15, 0x98, 0x02,
};
inline constexpr std::uintptr_t kUnequipShoutVrRva = 0x0643910;
inline constexpr std::array<std::uint8_t, 37> kUnequipShoutVrPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x3B, 0x15, 0x73, 0x0D, 0x98, 0x02,
};

// The public synchronous wrappers build their native payloads then dispatch
// through the low authority targets above. CommonLib does not expose them.
inline constexpr std::uintptr_t kPublicUnequipSpellVrRva = 0x0641350;
inline constexpr std::array<std::uint8_t, 16> kPublicUnequipSpellVrPrologue{
    0x48, 0x85, 0xD2, 0x74, 0x56, 0x48, 0x89, 0x5C,
    0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
};
inline constexpr std::uintptr_t kPublicUnequipShoutVrRva = 0x0641430;
inline constexpr std::array<std::uint8_t, 16> kPublicUnequipShoutVrPrologue{
    0x48, 0x83, 0xEC, 0x38, 0x48, 0x85, 0xD2, 0x74,
    0x1D, 0x4D, 0x85, 0xC0, 0x74, 0x18, 0x4C, 0x8D,
};

inline constexpr std::array<Operation, 6> kInstallOrder{
    Operation::EquipObject,
    Operation::UnequipObject,
    Operation::EquipSpell,
    Operation::UnequipSpell,
    Operation::EquipShout,
    Operation::UnequipShout,
};
inline constexpr std::array<Operation, 6> kUninstallOrder{
    Operation::UnequipShout,
    Operation::EquipShout,
    Operation::UnequipSpell,
    Operation::EquipSpell,
    Operation::UnequipObject,
    Operation::EquipObject,
};

[[nodiscard]] constexpr bool HasPinnedTargetConfiguration() noexcept
{
    return kEquipObjectVrRva == 0x0642E30 && kEquipObjectVrPrologue.size() == 18 &&
           kUnequipObjectVrRva == 0x06436C0 && kUnequipObjectVrPrologue.size() == 37 &&
           kEquipSpellVrRva == 0x0642B80 && kEquipSpellVrPrologue.size() == 18 &&
           kUnequipSpellVrRva == 0x0643470 && kUnequipSpellVrPrologue.size() == 37 &&
           kEquipShoutVrRva == 0x06430E0 && kEquipShoutVrPrologue.size() == 18 &&
           kUnequipShoutVrRva == 0x0643910 && kUnequipShoutVrPrologue.size() == 37 &&
           kPublicUnequipSpellVrRva == 0x0641350 && kPublicUnequipSpellVrPrologue.size() == 16 &&
           kPublicUnequipShoutVrRva == 0x0641430 && kPublicUnequipShoutVrPrologue.size() == 16;
}

[[nodiscard]] constexpr Disposition Classify(
    const Operation a_operation,
    const bool a_managedRemoteActor,
    const bool a_retiringActor,
    const bool a_authoritativeReplay,
    const bool a_admittedInventoryRemoval) noexcept
{
    if (a_retiringActor)
        return Disposition::Suppress;
    if (!a_managedRemoteActor || a_authoritativeReplay)
        return Disposition::CallOriginal;
    if (a_operation == Operation::UnequipObject && a_admittedInventoryRemoval)
        return Disposition::CallOriginal;
    return Disposition::Suppress;
}

[[nodiscard]] constexpr bool RequiresSynchronousUnequip(const Operation a_operation) noexcept
{
    return a_operation == Operation::UnequipSpell || a_operation == Operation::UnequipShout;
}

[[nodiscard]] constexpr bool CanEnterScope(const std::uint32_t a_depth) noexcept
{
    return a_depth != std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] constexpr std::uint32_t EnterScope(const std::uint32_t a_depth) noexcept
{
    return CanEnterScope(a_depth) ? a_depth + 1 : a_depth;
}

[[nodiscard]] constexpr std::uint32_t LeaveScope(const std::uint32_t a_depth) noexcept
{
    return a_depth != 0 ? a_depth - 1 : 0;
}

[[nodiscard]] constexpr bool ShouldLogAggregate(const std::uint64_t a_count) noexcept
{
    return a_count != 0 && (a_count & (a_count - 1)) == 0;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHookPolicy

namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHooks
{
class ScopedAuthoritativeEquipmentReplay final
{
public:
    ScopedAuthoritativeEquipmentReplay() noexcept;
    ~ScopedAuthoritativeEquipmentReplay() noexcept;

    ScopedAuthoritativeEquipmentReplay(const ScopedAuthoritativeEquipmentReplay&) = delete;
    ScopedAuthoritativeEquipmentReplay& operator=(const ScopedAuthoritativeEquipmentReplay&) = delete;

private:
    bool _entered{};
};

class ScopedAdmittedInventoryRemoval final
{
public:
    ScopedAdmittedInventoryRemoval() noexcept;
    ~ScopedAdmittedInventoryRemoval() noexcept;

    ScopedAdmittedInventoryRemoval(const ScopedAdmittedInventoryRemoval&) = delete;
    ScopedAdmittedInventoryRemoval& operator=(const ScopedAdmittedInventoryRemoval&) = delete;

private:
    bool _entered{};
};

[[nodiscard]] bool IsAuthoritativeEquipmentReplay() noexcept;
[[nodiscard]] bool IsAdmittedInventoryRemoval() noexcept;

// These calls are synchronous dispatches and require
// ScopedAuthoritativeEquipmentReplay to remain alive so their detoured low
// entries retain remote authority. A successful return proves dispatch only;
// callers must read native state before reporting command success.
[[nodiscard]] bool UnequipSpellSynchronously(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::SpellItem* a_spell,
    const RE::BGSEquipSlot* a_slot) noexcept;
[[nodiscard]] bool UnequipShoutSynchronously(
    RE::ActorEquipManager* a_manager,
    RE::Actor* a_actor,
    RE::TESShout* a_shout) noexcept;

[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::EquipmentAuthorityHooks
