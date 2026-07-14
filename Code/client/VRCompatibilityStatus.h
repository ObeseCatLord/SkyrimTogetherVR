#pragma once

#include <filesystem>

struct VRCompatibilityStatus
{
    bool HiggsInstalled{false};
    bool HiggsLoaded{false};
    bool PlanckInstalled{false};
    bool PlanckLoaded{false};
    bool FbtInstalled{false};
    bool FbtLoaded{false};
    bool VRPhysicsCompatibilityModInstalled{false};
    bool BringupHooksCompiled{false};
    bool UnvalidatedHooksCompiled{false};
    bool ConnectionOnly{false};
    bool FlatOverlay{false};
    bool ValidatedInlinePatches{false};
    bool UnvalidatedGameplayHooksSuppressed{false};
    bool RemoteAvatarSync{false};
    bool RemoteAvatarActorTargets{false};
    bool RemoteAvatarSkeletonWrites{false};
    bool DiscoveryService{false};
    bool PlayerCellService{false};
    bool PoseService{false};
    bool BodyPoseCapture{false};
    bool MovementObservationService{false};
    bool InventoryObservationService{false};
    bool ActivationObservationService{false};
    bool MagicObservationService{false};
    bool CombatObservationService{false};
    bool ProjectileObservationService{false};
    bool GrabObservationService{false};
    bool HiggsObservationService{false};
    bool SaveLoadObservationService{false};
    bool RemotePlayerProxyService{false};
};

VRCompatibilityStatus BuildVRCompatibilityStatus(
    const std::filesystem::path& acGamePath,
    bool aHiggsLoaded = false,
    bool aPlanckLoaded = false) noexcept;

void WriteVRCompatibilityStatusFile(
    const std::filesystem::path& acGamePath,
    const VRCompatibilityStatus& acStatus) noexcept;
