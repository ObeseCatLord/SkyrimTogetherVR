#include "ProjectileHooks.h"

#include "AvatarManager.h"
#include "BridgeEndpoint.h"

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
thread_local std::uint32_t g_remoteLaunchAllowance{};

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
    // Concentration spells are replicated by the spell-cast path. Preserve
    // the original launch hook ordering and do not suppress them here.
    if (a_data.spell && a_data.spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration)
        return g_originalLaunch ? g_originalLaunch(a_result, a_data) : a_result;

    const auto* shooter = a_data.shooter ? a_data.shooter->As<RE::Actor>() : nullptr;
    if (shooter && AvatarManager::Get().IsManagedRemoteActor(shooter) && g_remoteLaunchAllowance == 0) {
        if (a_result)
            a_result->reset();
        return a_result;
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
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return g_originalLaunch != nullptr;

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
        g_hookTarget = nullptr;
        g_originalLaunch = nullptr;
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    const auto enable = MH_EnableHook(g_hookTarget);
    if (enable != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: Projectile::Launch hook enable failed ({})",
                         static_cast<int>(enable));
        MH_RemoveHook(g_hookTarget);
        g_hookTarget = nullptr;
        g_originalLaunch = nullptr;
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed exact Projectile::Launch hook at VR address ID 42928");
    return true;
}

void Uninstall() noexcept
{
    if (g_hookTarget) {
        MH_DisableHook(g_hookTarget);
        MH_RemoveHook(g_hookTarget);
    }
    g_hookTarget = nullptr;
    g_originalLaunch = nullptr;
    g_installAttempted.store(false, std::memory_order_release);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ProjectileHooks
