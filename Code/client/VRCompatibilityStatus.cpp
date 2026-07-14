#include <TiltedOnlinePCH.h>

#include <VRCompatibilityStatus.h>

#include <cstddef>
#include <cwctype>
#include <fstream>
#include <initializer_list>
#include <string>

#ifndef TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS
#define TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
#define TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY
#define TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES
#define TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE
#define TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE
#define TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_POSE_SERVICE
#define TP_SKYRIM_VR_ENABLE_POSE_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE
#define TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE
#define TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE 0
#endif

namespace
{
constexpr char kCompatibilityStatusFileName[] = "SkyrimTogetherVR.compatibility";

const char* BoolValue(bool aValue) noexcept
{
    return aValue ? "1" : "0";
}

bool EqualsFilenameInsensitive(const std::wstring& acName, const wchar_t* apExpected) noexcept
{
    if (!apExpected)
        return false;

    std::size_t index = 0;
    for (; index < acName.size() && apExpected[index] != L'\0'; ++index)
    {
        if (std::towlower(acName[index]) != std::towlower(apExpected[index]))
            return false;
    }

    return index == acName.size() && apExpected[index] == L'\0';
}

bool IsSksePluginInstalled(const std::filesystem::path& acGamePath, std::initializer_list<const wchar_t*> aPluginNames) noexcept
{
    const auto pluginDir = acGamePath / L"Data" / L"SKSE" / L"Plugins";
    std::error_code ec;
    if (!std::filesystem::is_directory(pluginDir, ec))
        return false;

    std::filesystem::directory_iterator it(pluginDir, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && it != end)
    {
        const auto name = it->path().filename().wstring();
        for (const auto* pExpectedName : aPluginNames)
        {
            if (EqualsFilenameInsensitive(name, pExpectedName))
                return true;
        }

        it.increment(ec);
    }

    return false;
}

const char* GetHookMode(const VRCompatibilityStatus& acStatus) noexcept
{
    if (acStatus.UnvalidatedHooksCompiled && !acStatus.UnvalidatedGameplayHooksSuppressed)
        return "unvalidated_gameplay_hooks";

    if (acStatus.UnvalidatedGameplayHooksSuppressed)
        return "physics_compatibility_bringup_hooks";

    if (acStatus.BringupHooksCompiled)
        return "bringup_hooks";

    return "no_hooks";
}

const char* GetGameplayMode(const VRCompatibilityStatus& acStatus) noexcept
{
    if (!acStatus.ConnectionOnly)
        return "gameplay_services";

    if (!acStatus.RemoteAvatarSync)
        return "connection_only";

    if (!acStatus.RemoteAvatarActorTargets)
        return "connection_only+avatar_cache";

    return acStatus.RemoteAvatarSkeletonWrites ? "connection_only+avatar_actors" : "connection_only+avatar_actor_targets_suppressed";
}

const char* GetGameplayServicePolicy(const VRCompatibilityStatus& acStatus, bool aObserverEnabled) noexcept
{
    if (!aObserverEnabled)
        return "disabled";

    return acStatus.ConnectionOnly ? "observation_only" : "normal_service_enabled_with_vr_relay";
}

const char* GetObservationPolicy(bool aObserverEnabled) noexcept
{
    return aObserverEnabled ? "observation_only" : "disabled";
}

const char* GetPlayerCellPolicy(const VRCompatibilityStatus& acStatus) noexcept
{
    if (!acStatus.PlayerCellService)
        return "disabled";

    return acStatus.ConnectionOnly ? "network_only" : "normal_player_service_with_vr_status";
}

const char* GetRemoteAvatarPolicy(const VRCompatibilityStatus& acStatus) noexcept
{
    if (!acStatus.RemoteAvatarSync)
        return "disabled";

    if (acStatus.RemoteAvatarActorTargets && !acStatus.RemoteAvatarSkeletonWrites)
        return "pose_cache_only_actor_writes_suppressed";

    if (acStatus.RemoteAvatarActorTargets)
        return "guarded_actor_targets";

    return "component_cache_only";
}

const char* GetRemotePlayerProxyPolicy(const VRCompatibilityStatus& acStatus) noexcept
{
    return acStatus.RemotePlayerProxyService ? "readout_only" : "disabled";
}

const char* GetInventoryPolicy(const VRCompatibilityStatus& acStatus) noexcept
{
    if (!acStatus.InventoryObservationService)
        return "disabled";

    return acStatus.ConnectionOnly ? "equipment_snapshot_only" : "normal_inventory_service_enabled_with_vr_equipment_snapshot";
}
}

VRCompatibilityStatus BuildVRCompatibilityStatus(
    const std::filesystem::path& acGamePath,
    bool aHiggsLoaded,
    bool aPlanckLoaded) noexcept
{
    VRCompatibilityStatus status{};

    status.HiggsInstalled = IsSksePluginInstalled(acGamePath, {L"higgs_vr.dll", L"higgs.dll"});
    status.HiggsLoaded = aHiggsLoaded;
    status.PlanckInstalled = IsSksePluginInstalled(acGamePath, {L"activeragdoll.dll"});
    status.PlanckLoaded = aPlanckLoaded;
    status.FbtInstalled = IsSksePluginInstalled(acGamePath, {L"SkyrimVR-FBT.dll"});
    status.FbtLoaded = GetModuleHandleW(L"SkyrimVR-FBT.dll") != nullptr;
    status.VRPhysicsCompatibilityModInstalled = status.HiggsInstalled || status.PlanckInstalled || status.HiggsLoaded || status.PlanckLoaded;
    status.BringupHooksCompiled = TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS != 0;
    status.UnvalidatedHooksCompiled = TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS != 0;
    status.ConnectionOnly = TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY != 0;
    status.FlatOverlay = TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY != 0;
    status.ValidatedInlinePatches = TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES != 0;
    status.UnvalidatedGameplayHooksSuppressed = status.UnvalidatedHooksCompiled && status.VRPhysicsCompatibilityModInstalled;
    status.RemoteAvatarSync = TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC != 0;
    status.RemoteAvatarActorTargets = TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS != 0;
    status.RemoteAvatarSkeletonWrites = TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SKELETON_WRITES != 0;
    status.DiscoveryService = TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE != 0;
    status.PlayerCellService = TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE != 0;
    status.PoseService = TP_SKYRIM_VR_ENABLE_POSE_SERVICE != 0;
    status.BodyPoseCapture = TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE != 0;
    status.MovementObservationService = TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE != 0;
    status.InventoryObservationService = TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE != 0;
    status.ActivationObservationService = TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE != 0;
    status.MagicObservationService = TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE != 0;
    status.CombatObservationService = TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE != 0;
    status.ProjectileObservationService = TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE != 0;
    status.GrabObservationService = TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE != 0;
    status.HiggsObservationService = TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE != 0;
    status.SaveLoadObservationService = TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE != 0;
    status.RemotePlayerProxyService = TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE != 0;

    return status;
}

void WriteVRCompatibilityStatusFile(
    const std::filesystem::path& acGamePath,
    const VRCompatibilityStatus& acStatus) noexcept
{
    std::error_code ec;
    const auto handoffDir = acGamePath / L"Data" / L"SkyrimTogetherReborn";
    std::filesystem::create_directories(handoffDir, ec);

    std::ofstream file(handoffDir / kCompatibilityStatusFileName, std::ios::trunc);
    if (!file)
        return;

    file << "ready=1\n";
    file << "higgs.installed=" << BoolValue(acStatus.HiggsInstalled) << "\n";
    file << "higgs.loaded=" << BoolValue(acStatus.HiggsLoaded) << "\n";
    file << "planck.installed=" << BoolValue(acStatus.PlanckInstalled) << "\n";
    file << "planck.loaded=" << BoolValue(acStatus.PlanckLoaded) << "\n";
    file << "fbt.installed=" << BoolValue(acStatus.FbtInstalled) << "\n";
    file << "fbt.loaded=" << BoolValue(acStatus.FbtLoaded) << "\n";
    file << "vrPhysicsCompatibilityModInstalled=" << BoolValue(acStatus.VRPhysicsCompatibilityModInstalled) << "\n";
    file << "bringupHooksCompiled=" << BoolValue(acStatus.BringupHooksCompiled) << "\n";
    file << "unvalidatedHooksCompiled=" << BoolValue(acStatus.UnvalidatedHooksCompiled) << "\n";
    file << "connectionOnly=" << BoolValue(acStatus.ConnectionOnly) << "\n";
    file << "flatOverlay=" << BoolValue(acStatus.FlatOverlay) << "\n";
    file << "validatedInlinePatches=" << BoolValue(acStatus.ValidatedInlinePatches) << "\n";
    file << "unvalidatedGameplayHooksSuppressed=" << BoolValue(acStatus.UnvalidatedGameplayHooksSuppressed) << "\n";
    file << "hookMode=" << GetHookMode(acStatus) << "\n";
    file << "gameplayMode=" << GetGameplayMode(acStatus) << "\n";
    file << "remoteAvatarSync=" << BoolValue(acStatus.RemoteAvatarSync) << "\n";
    file << "remoteAvatarActorTargets=" << BoolValue(acStatus.RemoteAvatarActorTargets) << "\n";
    file << "remoteAvatarSkeletonWrites=" << BoolValue(acStatus.RemoteAvatarSkeletonWrites) << "\n";
    file << "remoteAvatarPolicy=" << GetRemoteAvatarPolicy(acStatus) << "\n";
    file << "discoveryPolicy=" << GetObservationPolicy(acStatus.DiscoveryService) << "\n";
    file << "playerCellPolicy=" << GetPlayerCellPolicy(acStatus) << "\n";
    file << "posePolicy=" << GetObservationPolicy(acStatus.PoseService) << "\n";
    file << "bodyPoseCapturePolicy=" << GetObservationPolicy(acStatus.BodyPoseCapture) << "\n";
    file << "remotePlayerProxyPolicy=" << GetRemotePlayerProxyPolicy(acStatus) << "\n";
    file << "movementPolicy=" << GetGameplayServicePolicy(acStatus, acStatus.MovementObservationService) << "\n";
    file << "equipmentPolicy=" << GetGameplayServicePolicy(acStatus, acStatus.InventoryObservationService) << "\n";
    file << "activationPolicy=" << GetGameplayServicePolicy(acStatus, acStatus.ActivationObservationService) << "\n";
    file << "inventoryPolicy=" << GetInventoryPolicy(acStatus) << "\n";
    file << "magicPolicy=" << GetGameplayServicePolicy(acStatus, acStatus.MagicObservationService) << "\n";
    file << "combatPolicy=" << GetGameplayServicePolicy(acStatus, acStatus.CombatObservationService) << "\n";
    file << "projectilePolicy=" << GetGameplayServicePolicy(acStatus, acStatus.ProjectileObservationService) << "\n";
    file << "grabPolicy=" << GetObservationPolicy(acStatus.GrabObservationService) << "\n";
    file << "higgsRelayPolicy=" << GetObservationPolicy(acStatus.HiggsObservationService) << "\n";
    file << "saveLoadPolicy=" << GetObservationPolicy(acStatus.SaveLoadObservationService) << "\n";
    file << "higgsPolicy=observation_only\n";
    file << "planckPolicy=observation_only\n";
    file << "fbtPolicy=local_post_higgs_capture_and_network_cache_only\n";
}
