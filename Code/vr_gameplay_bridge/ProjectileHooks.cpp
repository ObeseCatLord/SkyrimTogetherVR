#include "ProjectileHooks.h"

#include "AvatarManager.h"
#include "BridgeEndpoint.h"
#include "VrHookDetachPolicy.h"
#include "VrNoThrow.h"

#include <MinHook.h>

#include <array>
#include <cmath>
#include <cstring>

namespace SkyrimTogetherVR::GameplayAdapter::ProjectileHooks
{
namespace
{
using Launch = RE::ProjectileHandle* (*)(RE::ProjectileHandle*, RE::Projectile::LaunchData&) noexcept;

constexpr std::uint64_t kProjectileLaunchVrRva = 0x0776440;
constexpr std::array<std::uint8_t, 16> kProjectileLaunchVrPrologue{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56,
    0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
};

Launch g_originalLaunch{};
void* g_hookTarget{};
std::atomic<bool> g_installAttempted{};
VrHookDetachPolicy::HookState g_hookState{};
thread_local std::uint32_t g_remoteLaunchAllowance{};

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void*) noexcept
{
    const auto status = MH_DisableHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: Projectile::Launch hook disable failed ({})",
                                                static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void*) noexcept
{
    const auto status = MH_RemoveHook(g_hookTarget);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    NoThrow::BestEffort([&] { SKSE::log::error("SkyrimTogetherVRGameplayBridge: Projectile::Launch hook remove failed ({})",
                                                static_cast<int>(status)); });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool DetachHook() noexcept
{
    return VrHookDetachPolicy::Detach(
        g_hookState, {DisableHook, RemoveHook, nullptr});
}

void ForgetDetachedHook() noexcept
{
    g_hookTarget = nullptr;
    g_originalLaunch = nullptr;
    g_hookState = {};
}

void LogRetainedHook(const char* a_operation) noexcept
{
    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("Projectile::Launch hook rollback could not prove detachment"); });
    NoThrow::BestEffort([&] { SKSE::log::error(
        "SkyrimTogetherVRGameplayBridge: Projectile::Launch {} could not prove detachment; retaining target and "
        "trampoline so a possible live detour remains callable and the bridge stays loaded",
        a_operation); });
}

[[nodiscard]] bool IsBounded(const float a_value, const float a_limit) noexcept
{
    return std::isfinite(a_value) && a_value >= -a_limit && a_value <= a_limit;
}

[[nodiscard]] bool PreparePayload(
    const RE::Projectile::LaunchData& a_data,
    ApplyProjectileLaunchPayload& a_payload) noexcept
{
    auto& endpoint = BridgeEndpoint::Get();
    const auto* player = RE::PlayerCharacter::GetSingleton();
    const auto* shooter = a_data.shooter ? a_data.shooter->As<RE::Actor>() : nullptr;
    if (!player || !shooter || !endpoint.IsOperational() ||
        AvatarManager::Get().IsManagedRemoteActor(shooter) ||
        !HasCapability(endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire),
                       Capability::CombatAndMagic))
        return false;

    if (a_data.spell && a_data.spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration)
        return false;

    const auto projectile = a_data.projectileBase ? a_data.projectileBase->GetFormID() : 0;
    const auto parentCell = a_data.parentCell ? a_data.parentCell->GetFormID() : 0;
    const auto castingSource = static_cast<std::int32_t>(a_data.castingSource);
    if (projectile == 0 || parentCell == 0 ||
        !IsBounded(a_data.origin.x, kMaximumProjectileCoordinate) ||
        !IsBounded(a_data.origin.y, kMaximumProjectileCoordinate) ||
        !IsBounded(a_data.origin.z, kMaximumProjectileCoordinate) ||
        !IsBounded(a_data.angleX, kMaximumProjectileAngle) ||
        !IsBounded(a_data.angleZ, kMaximumProjectileAngle) ||
        !std::isfinite(a_data.power) || a_data.power < 0.0F || a_data.power > kMaximumProjectilePower ||
        !std::isfinite(a_data.scale) || a_data.scale < 0.0F || a_data.scale > kMaximumProjectileScale ||
        castingSource < static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kLeftHand) ||
        castingSource > static_cast<std::int32_t>(RE::MagicSystem::CastingSource::kInstant) ||
        a_data.area < 0 || a_data.area > kMaximumProjectileArea)
        return false;

    if (shooter == player)
        a_payload.TargetHandle = kLocalPlayerHandle;
    else
        a_payload.LocalShooterFormId = shooter->GetFormID();
    a_payload.LocalProjectileBaseFormId = projectile;
    a_payload.LocalWeaponFormId = a_data.weaponSource ? a_data.weaponSource->GetFormID() : 0;
    a_payload.LocalAmmoFormId = a_data.ammoSource ? a_data.ammoSource->GetFormID() : 0;
    a_payload.LocalSpellFormId = a_data.spell ? a_data.spell->GetFormID() : 0;
    a_payload.LocalParentCellFormId = parentCell;
    a_payload.OriginX = a_data.origin.x;
    a_payload.OriginY = a_data.origin.y;
    a_payload.OriginZ = a_data.origin.z;
    a_payload.AngleX = a_data.angleX;
    a_payload.AngleZ = a_data.angleZ;
    a_payload.Power = a_data.power;
    a_payload.Scale = a_data.scale;
    a_payload.CastingSource = castingSource;
    a_payload.Area = a_data.area;
    a_payload.LaunchFlags = (a_data.alwaysHit ? ProjectileAlwaysHit : 0u) |
                            (a_data.noDamageOutsideCombat ? ProjectileNoDamageOutsideCombat : 0u) |
                            (a_data.autoAim ? ProjectileAutoAim : 0u) |
                            (a_data.chainShatter ? ProjectileChainShatter : 0u) |
                            (a_data.deferInitialization ? ProjectileDeferInitialization : 0u) |
                            (a_data.forceConeOfFire ? ProjectileForceConeOfFire : 0u);
    return true;
}

RE::ProjectileHandle* HookLaunch(
    RE::ProjectileHandle* a_result,
    RE::Projectile::LaunchData& a_data) noexcept
{
    try {
    // Concentration spells are replicated by the spell-cast path. Preserve
    // the original launch hook ordering and do not suppress them here.
    if (a_data.spell && a_data.spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration)
        return g_originalLaunch ? g_originalLaunch(a_result, a_data) : a_result;

    const auto* shooter = a_data.shooter ? a_data.shooter->As<RE::Actor>() : nullptr;
    const bool remoteShooter = shooter && AvatarManager::Get().IsManagedRemoteActor(shooter);
    if (remoteShooter) {
        if (g_remoteLaunchAllowance == 0) {
            if (a_result)
                a_result->reset();
            return a_result;
        }

        // A replayed remote projectile is permitted to execute once, but it
        // must never re-enter the local projectile publication path.
        return g_originalLaunch ? g_originalLaunch(a_result, a_data) : a_result;
    }

    ApplyProjectileLaunchPayload payload{};
    const bool publish = PreparePayload(a_data, payload);
    auto* result = g_originalLaunch ? g_originalLaunch(a_result, a_data) : a_result;
    if (!publish || !result)
        return result;

    const auto& projectileHandle = *result;
    if (const auto projectile = projectileHandle.get())
        payload.Power = projectile->GetProjectileRuntimeData().power;
    if (!std::isfinite(payload.Power) || payload.Power < 0.0F || payload.Power > kMaximumProjectilePower)
        return result;

    auto& endpoint = BridgeEndpoint::Get();
    EventRecord record{};
    record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalProjectileLaunch);
    record.Header.PayloadSize = kFixedPayloadBytes;
    record.Header.Identity = endpoint.SnapshotIdentity(endpoint.NextEventSequence());
    record.Payload.LocalProjectileLaunch = payload;
    endpoint.TryPushEvent(record);
    return result;
    } catch (...) {
        return a_result;
    }
}
} // namespace

ScopedRemoteLaunch::ScopedRemoteLaunch() noexcept
{
    ++g_remoteLaunchAllowance;
}

ScopedRemoteLaunch::~ScopedRemoteLaunch() noexcept
{
    if (g_remoteLaunchAllowance != 0)
        --g_remoteLaunchAllowance;
}

bool Install() noexcept
{
    try {
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return g_hookState.Created && g_originalLaunch != nullptr;

    const auto initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for Projectile::Launch ({})",
                         static_cast<int>(initialize));
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    REL::Relocation<Launch> target{RELOCATION_ID(42928, 44108)};
    if (target.offset() != kProjectileLaunchVrRva ||
        std::memcmp(reinterpret_cast<const void*>(target.address()),
                    kProjectileLaunchVrPrologue.data(), kProjectileLaunchVrPrologue.size()) != 0) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: Projectile::Launch VR address or prologue mismatch at RVA 0x{:X}",
            target.offset());
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
    g_hookTarget = reinterpret_cast<void*>(target.address());
    const auto create = MH_CreateHook(
        g_hookTarget, reinterpret_cast<void*>(&HookLaunch),
        reinterpret_cast<void**>(&g_originalLaunch));
    if (create != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: Projectile::Launch hook creation failed ({})",
                         static_cast<int>(create));
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
    g_hookState.Created = true;

    // A failed MinHook enable does not prove that no target bytes changed.
    // Treat it as potentially live until DisableHook confirms otherwise.
    g_hookState.Enabled = true;
    const auto enable = MH_EnableHook(g_hookTarget);
    if (enable != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: Projectile::Launch hook enable failed ({})",
                         static_cast<int>(enable));
        if (DetachHook()) {
            ForgetDetachedHook();
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }
        LogRetainedHook("install rollback");
        return true;
    }

    SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed exact Projectile::Launch hook at VR address ID 42928");
    return true;
    } catch (...) {
        if (g_hookState.Created && !DetachHook()) {
            LogRetainedHook("exception rollback");
            return false;
        }
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
}

bool Uninstall() noexcept
{
    try {
    if (!g_hookState.Created) {
        ForgetDetachedHook();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }
    if (!DetachHook()) {
        LogRetainedHook("uninstall");
        return false;
    }
    ForgetDetachedHook();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
    } catch (...) {
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ProjectileHooks
