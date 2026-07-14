#include "PapyrusFunctions.h"

#include <Games/ActorExtension.h>
#include <Services/VRActivationService.h>
#include <Services/VRCombatService.h>
#include <Services/VRConnectionService.h>
#include <Services/VRGrabService.h>
#include <Services/VRHiggsService.h>
#include <Services/VRInventoryService.h>
#include <Services/VRMagicService.h>
#include <Services/VRMovementService.h>
#include <Services/VRPoseService.h>
#include <Services/VRProjectileService.h>
#include <Services/VRSaveLoadService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
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

namespace PapyrusFunctions
{
namespace
{
thread_local std::string s_statusSummary;
thread_local std::string s_telemetryReadout;
thread_local std::string s_configuredEndpoint;
thread_local std::string s_configuredPassword;

constexpr char kDefaultEndpoint[] = "127.0.0.1:10578";
constexpr char kConnectionFileName[] = "SkyrimTogetherVR.connection";

using KeyValueMap = std::unordered_map<std::string, std::string>;

const char* YesNo(bool aValue) noexcept
{
    return aValue ? "Yes" : "No";
}

std::string Trim(std::string aValue)
{
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    aValue.erase(aValue.begin(), std::find_if_not(aValue.begin(), aValue.end(), isSpace));
    aValue.erase(std::find_if_not(aValue.rbegin(), aValue.rend(), isSpace).base(), aValue.end());
    return aValue;
}

template <class T> void AppendPosition(std::string& aOut, const T& acPosition)
{
    aOut += std::to_string(static_cast<int32_t>(acPosition.x));
    aOut += ",";
    aOut += std::to_string(static_cast<int32_t>(acPosition.y));
    aOut += ",";
    aOut += std::to_string(static_cast<int32_t>(acPosition.z));
}

void AppendGameId(std::string& aOut, const GameId& acId)
{
    aOut += std::to_string(acId.ModId);
    aOut += ":";
    aOut += std::to_string(acId.BaseId);
}

std::filesystem::path GetHandoffPath(const char* apFileName)
{
    return SkyrimTogetherVR::Handoff::GetFile(apFileName);
}

bool WriteConnectionConfig(const std::string& acEndpoint, const std::string& acPassword)
{
    const auto path = GetHandoffPath(kConnectionFileName);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::trunc);
    if (!file)
        return false;

    file << "endpoint=" << acEndpoint << "\n";
    if (!acPassword.empty())
        file << "password=" << acPassword << "\n";

    return true;
}

KeyValueMap ReadKeyValueFile(const std::filesystem::path& acPath)
{
    KeyValueMap values;
    std::ifstream file(acPath);
    if (!file)
        return values;

    std::string line;
    while (std::getline(file, line))
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0)
            continue;

        values.emplace(line.substr(0, separator), line.substr(separator + 1));
    }

    return values;
}

const std::string& GetValue(const KeyValueMap& acValues, const char* apKey)
{
    static const std::string s_missing = "?";
    const auto it = acValues.find(apKey);
    return it != acValues.end() ? it->second : s_missing;
}

void AppendCompatibilitySummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.compatibility"));

    aOut += "\nVR compat: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "higgs=";
    aOut += GetValue(values, "higgs.installed");
    aOut += "/";
    aOut += GetValue(values, "higgs.loaded");
    aOut += " planck=";
    aOut += GetValue(values, "planck.installed");
    aOut += "/";
    aOut += GetValue(values, "planck.loaded");
    aOut += " mode=";
    aOut += GetValue(values, "hookMode");
    aOut += " gameplay=";
    aOut += GetValue(values, "gameplayMode");
    aOut += " avatar=";
    aOut += GetValue(values, "remoteAvatarPolicy");
    aOut += " suppressed=";
    aOut += GetValue(values, "unvalidatedGameplayHooksSuppressed");
}

void AppendCompatibilityTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.compatibility"));

    aOut += "\n\nVR compatibility:";
    if (values.empty())
    {
        aOut += "\nmissing";
        return;
    }

    aOut += "\nready=";
    aOut += GetValue(values, "ready");
    aOut += " hookMode=";
    aOut += GetValue(values, "hookMode");
    aOut += "\nhiggs installed=";
    aOut += GetValue(values, "higgs.installed");
    aOut += " loaded=";
    aOut += GetValue(values, "higgs.loaded");
    aOut += " policy=";
    aOut += GetValue(values, "higgsPolicy");
    aOut += "\nplanck installed=";
    aOut += GetValue(values, "planck.installed");
    aOut += " loaded=";
    aOut += GetValue(values, "planck.loaded");
    aOut += " policy=";
    aOut += GetValue(values, "planckPolicy");
    aOut += "\nphysicsCompat=";
    aOut += GetValue(values, "vrPhysicsCompatibilityModInstalled");
    aOut += " unvalidatedHooks=";
    aOut += GetValue(values, "unvalidatedHooksCompiled");
    aOut += " suppressed=";
    aOut += GetValue(values, "unvalidatedGameplayHooksSuppressed");
    aOut += "\nbringupHooks=";
    aOut += GetValue(values, "bringupHooksCompiled");
    aOut += " connectionOnly=";
    aOut += GetValue(values, "connectionOnly");
    aOut += " flatOverlay=";
    aOut += GetValue(values, "flatOverlay");
    aOut += " validatedInlinePatches=";
    aOut += GetValue(values, "validatedInlinePatches");
    aOut += "\ngameplayMode=";
    aOut += GetValue(values, "gameplayMode");
    aOut += " remoteAvatar=";
    aOut += GetValue(values, "remoteAvatarPolicy");
    aOut += " proxy=";
    aOut += GetValue(values, "remotePlayerProxyPolicy");
    aOut += "\nlanes movement=";
    aOut += GetValue(values, "movementPolicy");
    aOut += " equipment=";
    aOut += GetValue(values, "equipmentPolicy");
    aOut += " activation=";
    aOut += GetValue(values, "activationPolicy");
    aOut += "\nlanes inventory=";
    aOut += GetValue(values, "inventoryPolicy");
    aOut += " magic=";
    aOut += GetValue(values, "magicPolicy");
    aOut += " combat=";
    aOut += GetValue(values, "combatPolicy");
    aOut += "\nlanes projectile=";
    aOut += GetValue(values, "projectilePolicy");
    aOut += " grab=";
    aOut += GetValue(values, "grabPolicy");
    aOut += " saveLoad=";
    aOut += GetValue(values, "saveLoadPolicy");
    aOut += "\nlanes higgsRelay=";
    aOut += GetValue(values, "higgsRelayPolicy");
    aOut += " discovery=";
    aOut += GetValue(values, "discoveryPolicy");
    aOut += " playerCell=";
    aOut += GetValue(values, "playerCellPolicy");
}

KeyValueMap ReadConnectionConfig()
{
    return ReadKeyValueFile(GetHandoffPath(kConnectionFileName));
}

std::string GetConnectionConfigValue(const char* apKey, const char* apFallback)
{
    const auto values = ReadConnectionConfig();
    const auto it = values.find(apKey);
    if (it == values.end())
        return apFallback;

    const auto value = Trim(it->second);
    return value.empty() ? apFallback : value;
}

std::string GetConfiguredEndpointString()
{
    return GetConnectionConfigValue("endpoint", kDefaultEndpoint);
}

std::string GetConfiguredPasswordString()
{
    const auto values = ReadConnectionConfig();
    const auto it = values.find("password");
    return it != values.end() ? it->second : "";
}

void AppendHiggsSummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.higgs"));

    aOut += "\nHIGGS bridge: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "detected=";
    aOut += GetValue(values, "higgs.detected");
    aOut += " api=";
    aOut += GetValue(values, "higgs.interfaceAvailable");
    aOut += " callbacks=";
    aOut += GetValue(values, "higgs.callbacksRegistered");
    aOut += " snapshot=";
    aOut += GetValue(values, "higgs.snapshotAvailable");
    aOut += " twoHanding=";
    aOut += GetValue(values, "higgs.twoHanding");
    aOut += " events=";
    aOut += GetValue(values, "recentEventCount");
}

void AppendHiggsTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.higgs"));

    aOut += "\n\nHIGGS:";
    if (values.empty())
    {
        aOut += "\nbridge=missing";
        return;
    }

    aOut += "\napi=";
    aOut += GetValue(values, "higgs.interfaceAvailable");
    aOut += " bridgeSeq=";
    aOut += GetValue(values, "bridge.sequence");
    aOut += " callbacks=";
    aOut += GetValue(values, "higgs.callbacksRegistered");
    aOut += " snapshot=";
    aOut += GetValue(values, "higgs.snapshotAvailable");
    aOut += " seq=";
    aOut += GetValue(values, "higgs.snapshotSequence");
    aOut += " twoHanding=";
    aOut += GetValue(values, "higgs.twoHanding");
    aOut += " events=";
    aOut += GetValue(values, "recentEventCount");
    aOut += "\nleft holding=";
    aOut += GetValue(values, "left.holdingObject");
    aOut += " grabbed=";
    aOut += GetValue(values, "left.grabbedObjectFormId");
    aOut += "\nright holding=";
    aOut += GetValue(values, "right.holdingObject");
    aOut += " grabbed=";
    aOut += GetValue(values, "right.grabbedObjectFormId");
}

void AppendPlanckSummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.planck"));

    aOut += "\nPLANCK bridge: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "detected=";
    aOut += GetValue(values, "planck.detected");
    aOut += " seq=";
    aOut += GetValue(values, "bridge.sequence");
    aOut += " api=";
    aOut += GetValue(values, "planck.interfaceAvailable");
    aOut += " build=";
    aOut += GetValue(values, "planck.buildNumber");
    aOut += " hit=";
    aOut += GetValue(values, "planck.currentHitEventAvailable");
    aOut += " hitRead=";
    aOut += GetValue(values, "planck.currentHitEventObservationOnly");
    aOut += " lastHitProbe=";
    aOut += GetValue(values, "planck.lastHitDataProbeEnabled");
    aOut += " policy=";
    aOut += GetValue(values, "planck.policy");
}

void AppendPlanckTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.planck"));

    aOut += "\n\nPLANCK:";
    if (values.empty())
    {
        aOut += "\nbridge=missing";
        return;
    }

    aOut += "\nbridge=";
    aOut += GetValue(values, "bridge.loaded");
    aOut += " seq=";
    aOut += GetValue(values, "bridge.sequence");
    aOut += " detected=";
    aOut += GetValue(values, "planck.detected");
    aOut += " request=";
    aOut += GetValue(values, "planck.interfaceRequestAttempted");
    aOut += " api=";
    aOut += GetValue(values, "planck.interfaceAvailable");
    aOut += " build=";
    aOut += GetValue(values, "planck.buildNumber");
    aOut += "\ncurrentHitAddress=";
    aOut += GetValue(values, "planck.currentHitEventAddress");
    aOut += " currentHit=";
    aOut += GetValue(values, "planck.currentHitEventAvailable");
    aOut += " currentHitObservationOnly=";
    aOut += GetValue(values, "planck.currentHitEventObservationOnly");
    aOut += " lastHitData=";
    aOut += GetValue(values, "planck.lastHitDataAvailable");
    aOut += " lastHitProbe=";
    aOut += GetValue(values, "planck.lastHitDataProbeEnabled");
    aOut += "\nlastHitDataReason=";
    aOut += GetValue(values, "planck.lastHitDataReason");
    aOut += "\nlastHitDataBoundary=";
    aOut += GetValue(values, "planck.lastHitDataBoundary");
    aOut += " policy=";
    aOut += GetValue(values, "planck.policy");
}

void AppendAvatarSummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.avatar"));

    aOut += "\nRemote avatar: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "targets=";
    aOut += GetValue(values, "actorTargetsEnabled");
    aOut += " poses=";
    aOut += GetValue(values, "remotePoseMatchCount");
    aOut += " attempts=";
    aOut += GetValue(values, "actorTargetAttemptCount");
    aOut += " sameSpace=";
    aOut += GetValue(values, "sameSpaceCount");
    aOut += " skipCell=";
    aOut += GetValue(values, "actorTargetSkippedDifferentCellCount");
    aOut += " hmd=";
    aOut += GetValue(values, "hmdCopiedCount");
    aOut += " lh=";
    aOut += GetValue(values, "leftHandCopiedCount");
    aOut += " rh=";
    aOut += GetValue(values, "rightHandCopiedCount");
    aOut += " vrikApi=";
    aOut += GetValue(values, "vrikInterfaceAvailableCount");
    aOut += " invalidVrik=";
    aOut += GetValue(values, "invalidVrikCount");
    aOut += " invalid=";
    aOut += GetValue(values, "invalidTransformCount");
    aOut += " invalidMove=";
    aOut += GetValue(values, "invalidMovementCount");
    aOut += " equip=";
    aOut += GetValue(values, "remoteEquipmentMatchCount");
    aOut += " drawQ=";
    aOut += GetValue(values, "equipmentWeaponDrawQueuedCount");
    aOut += " spell=";
    aOut += GetValue(values, "spellOriginValidCount");
    aOut += " arrow=";
    aOut += GetValue(values, "arrowOriginValidCount");
    aOut += " bow=";
    aOut += GetValue(values, "bowAimValidCount");
}

void AppendAvatarTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.avatar"));

    aOut += "\n\nAvatar targets:";
    if (values.empty())
    {
        aOut += "\nmissing";
        return;
    }

    aOut += "\nready=";
    aOut += GetValue(values, "ready");
    aOut += " enabled=";
    aOut += GetValue(values, "actorTargetsEnabled");
    aOut += " players=";
    aOut += GetValue(values, "remotePlayerCount");
    aOut += " poseMatches=";
    aOut += GetValue(values, "remotePoseMatchCount");
    aOut += " components=";
    aOut += GetValue(values, "componentUpsertCount");
    aOut += " attempts=";
    aOut += GetValue(values, "actorTargetAttemptCount");
    aOut += " sameSpace=";
    aOut += GetValue(values, "sameSpaceCount");
    aOut += "\nspace sameCell=";
    aOut += GetValue(values, "sameCellCount");
    aOut += " sameWorld=";
    aOut += GetValue(values, "sameWorldSpaceCount");
    aOut += " noLocalMove=";
    aOut += GetValue(values, "actorTargetSkippedNoLocalMovementCount");
    aOut += " noRemoteMove=";
    aOut += GetValue(values, "actorTargetSkippedNoRemoteMovementCount");
    aOut += "\nskip differentCell=";
    aOut += GetValue(values, "actorTargetSkippedDifferentCellCount");
    aOut += " differentWorld=";
    aOut += GetValue(values, "actorTargetSkippedDifferentWorldSpaceCount");
    aOut += "\nmissing form=";
    aOut += GetValue(values, "missingFormIdCount");
    aOut += " actor=";
    aOut += GetValue(values, "missingActorCount");
    aOut += " root=";
    aOut += GetValue(values, "missingRootCount");
    aOut += "\nnodes head=";
    aOut += GetValue(values, "headNodeFoundCount");
    aOut += " lh=";
    aOut += GetValue(values, "leftHandNodeFoundCount");
    aOut += " rh=";
    aOut += GetValue(values, "rightHandNodeFoundCount");
    aOut += "\ncopied hmd=";
    aOut += GetValue(values, "hmdCopiedCount");
    aOut += " lh=";
    aOut += GetValue(values, "leftHandCopiedCount");
    aOut += " rh=";
    aOut += GetValue(values, "rightHandCopiedCount");
    aOut += "\nvrik detected=";
    aOut += GetValue(values, "vrikDetectedCount");
    aOut += " api=";
    aOut += GetValue(values, "vrikInterfaceAvailableCount");
    aOut += " lf=";
    aOut += GetValue(values, "vrikLeftFingersValidCount");
    aOut += " rf=";
    aOut += GetValue(values, "vrikRightFingersValidCount");
    aOut += " camera=";
    aOut += GetValue(values, "vrikCameraOffsetsValidCount");
    aOut += " invalidVrik=";
    aOut += GetValue(values, "invalidVrikCount");
    aOut += "\nmovement=";
    aOut += GetValue(values, "movementAppliedCount");
    aOut += " fallback=";
    aOut += GetValue(values, "hmdFallbackMovementCount");
    aOut += " invalidTransforms=";
    aOut += GetValue(values, "invalidTransformCount");
    aOut += " invalidMovement=";
    aOut += GetValue(values, "invalidMovementCount");
    aOut += " staleRemoved=";
    aOut += GetValue(values, "stalePoseRemovedCount");
    aOut += "\npose lanes spell=";
    aOut += GetValue(values, "spellOriginValidCount");
    aOut += "/";
    aOut += GetValue(values, "spellDestinationValidCount");
    aOut += " arrow=";
    aOut += GetValue(values, "arrowOriginValidCount");
    aOut += "/";
    aOut += GetValue(values, "arrowDestinationValidCount");
    aOut += " bow=";
    aOut += GetValue(values, "bowAimValidCount");
    aOut += "/";
    aOut += GetValue(values, "bowRotationValidCount");
    aOut += "\noffsets weapon=";
    aOut += GetValue(values, "leftWeaponOffsetValidCount");
    aOut += "/";
    aOut += GetValue(values, "rightWeaponOffsetValidCount");
    aOut += " magic=";
    aOut += GetValue(values, "primaryMagicOffsetValidCount");
    aOut += "/";
    aOut += GetValue(values, "secondaryMagicOffsetValidCount");
    aOut += " aim=";
    aOut += GetValue(values, "primaryMagicAimValidCount");
    aOut += "/";
    aOut += GetValue(values, "secondaryMagicAimValidCount");
    aOut += "\nequipment matches=";
    aOut += GetValue(values, "remoteEquipmentMatchCount");
    aOut += " components=";
    aOut += GetValue(values, "equipmentComponentUpsertCount");
    aOut += " drawQueued=";
    aOut += GetValue(values, "equipmentWeaponDrawQueuedCount");
    aOut += " missingForm=";
    aOut += GetValue(values, "equipmentMissingFormIdCount");
    aOut += " missingActor=";
    aOut += GetValue(values, "equipmentMissingActorCount");
    aOut += " staleRemoved=";
    aOut += GetValue(values, "staleEquipmentRemovedCount");
    aOut += "\nlast player=";
    aOut += GetValue(values, "last.playerId");
    aOut += " form=";
    aOut += GetValue(values, "last.formId");
    aOut += " seq=";
    aOut += GetValue(values, "last.poseSequence");
    aOut += "\nlast lanes spell=";
    aOut += GetValue(values, "last.spellOriginValid");
    aOut += "/";
    aOut += GetValue(values, "last.spellDestinationValid");
    aOut += " arrow=";
    aOut += GetValue(values, "last.arrowOriginValid");
    aOut += "/";
    aOut += GetValue(values, "last.arrowDestinationValid");
    aOut += " bow=";
    aOut += GetValue(values, "last.bowAimValid");
    aOut += "/";
    aOut += GetValue(values, "last.bowRotationValid");
    aOut += "\nlast offsets weapon=";
    aOut += GetValue(values, "last.leftWeaponOffsetValid");
    aOut += "/";
    aOut += GetValue(values, "last.rightWeaponOffsetValid");
    aOut += " magic=";
    aOut += GetValue(values, "last.primaryMagicOffsetValid");
    aOut += "/";
    aOut += GetValue(values, "last.secondaryMagicOffsetValid");
    aOut += " aim=";
    aOut += GetValue(values, "last.primaryMagicAimValid");
    aOut += "/";
    aOut += GetValue(values, "last.secondaryMagicAimValid");
    aOut += "\nlast equipment player=";
    aOut += GetValue(values, "lastEquipment.playerId");
    aOut += " form=";
    aOut += GetValue(values, "lastEquipment.formId");
    aOut += " seq=";
    aOut += GetValue(values, "lastEquipment.sequence");
    aOut += " drawn=";
    aOut += GetValue(values, "lastEquipment.weaponDrawn");
    aOut += " desiredDrawn=";
    aOut += GetValue(values, "lastEquipment.desiredWeaponDrawn");
    aOut += " drawQueued=";
    aOut += GetValue(values, "lastEquipment.weaponDrawQueued");
    aOut += " fullDrawn=";
    aOut += GetValue(values, "lastEquipment.weaponFullyDrawn");
    aOut += "\nlast space localMove=";
    aOut += GetValue(values, "last.localMovementAvailable");
    aOut += " remoteMove=";
    aOut += GetValue(values, "last.remoteMovementAvailable");
    aOut += " sameCell=";
    aOut += GetValue(values, "last.sameCell");
    aOut += " sameWorld=";
    aOut += GetValue(values, "last.sameWorldSpace");
    aOut += " canApply=";
    aOut += GetValue(values, "last.sameSpaceForApply");
    aOut += "\nlast actor=";
    aOut += GetValue(values, "last.actorAvailable");
    aOut += " root=";
    aOut += GetValue(values, "last.rootAvailable");
    aOut += " head=";
    aOut += GetValue(values, "last.headNodeFound");
    aOut += " lh=";
    aOut += GetValue(values, "last.leftHandNodeFound");
    aOut += " rh=";
    aOut += GetValue(values, "last.rightHandNodeFound");
    aOut += " hmd=";
    aOut += GetValue(values, "last.hmdCopied");
    aOut += " lhCopy=";
    aOut += GetValue(values, "last.leftHandCopied");
    aOut += " rhCopy=";
    aOut += GetValue(values, "last.rightHandCopied");
    aOut += " vrikApi=";
    aOut += GetValue(values, "last.vrikInterfaceAvailable");
    aOut += " move=";
    aOut += GetValue(values, "last.movementApplied");
    aOut += " invalidTransforms=";
    aOut += GetValue(values, "last.invalidTransformCount");
    aOut += " invalidVrik=";
    aOut += GetValue(values, "last.invalidVrikCount");
    aOut += " invalidMove=";
    aOut += GetValue(values, "last.invalidMovement");
}

void AppendRemotePlayersSummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.remoteplayers"));

    aOut += "\nRemote players: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "tracked=";
    aOut += GetValue(values, "trackedPlayerCount");
    aOut += " joined=";
    aOut += GetValue(values, "joinedPlayerCount");
    aOut += " pose=";
    aOut += GetValue(values, "poseMatchedCount");
    aOut += " movement=";
    aOut += GetValue(values, "movementMatchedCount");
    aOut += " equip=";
    aOut += GetValue(values, "equipmentMatchedCount");
    aOut += " action=";
    aOut += GetValue(values, "activationMatchedCount");
    aOut += "/";
    aOut += GetValue(values, "magicMatchedCount");
    aOut += "/";
    aOut += GetValue(values, "combatMatchedCount");
    aOut += "/";
    aOut += GetValue(values, "projectileMatchedCount");
    aOut += "/";
    aOut += GetValue(values, "grabMatchedCount");
    aOut += " sameCell=";
    aOut += GetValue(values, "sameCellCount");
    aOut += " sameWorld=";
    aOut += GetValue(values, "sameWorldSpaceCount");
    aOut += " avatarReady=";
    aOut += GetValue(values, "avatarValidationReadyCount");
    aOut += " higgsReady=";
    aOut += GetValue(values, "higgsAvatarValidationReadyCount");
}

void AppendRemotePlayersTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.remoteplayers"));

    aOut += "\n\nRemote player proxy:";
    if (values.empty())
    {
        aOut += "\nmissing";
        return;
    }

    aOut += "\nready=";
    aOut += GetValue(values, "ready");
    aOut += " online=";
    aOut += GetValue(values, "online");
    aOut += " tracked=";
    aOut += GetValue(values, "trackedPlayerCount");
    aOut += " joined=";
    aOut += GetValue(values, "joinedPlayerCount");
    aOut += "\nmatched pose=";
    aOut += GetValue(values, "poseMatchedCount");
    aOut += " movement=";
    aOut += GetValue(values, "movementMatchedCount");
    aOut += " equipment=";
    aOut += GetValue(values, "equipmentMatchedCount");
    aOut += " higgs=";
    aOut += GetValue(values, "higgsMatchedCount");
    aOut += "\naction activation=";
    aOut += GetValue(values, "activationMatchedCount");
    aOut += " magic=";
    aOut += GetValue(values, "magicMatchedCount");
    aOut += " combat=";
    aOut += GetValue(values, "combatMatchedCount");
    aOut += " projectile=";
    aOut += GetValue(values, "projectileMatchedCount");
    aOut += " grab=";
    aOut += GetValue(values, "grabMatchedCount");
    aOut += "\nsame cell=";
    aOut += GetValue(values, "sameCellCount");
    aOut += " same world=";
    aOut += GetValue(values, "sameWorldSpaceCount");
    aOut += " same space=";
    aOut += GetValue(values, "sameSpaceCount");
    aOut += "\nvrik matched=";
    aOut += GetValue(values, "vrikMatchedCount");
    aOut += " avatar ready=";
    aOut += GetValue(values, "avatarValidationReadyCount");
    aOut += " higgs avatar ready=";
    aOut += GetValue(values, "higgsAvatarValidationReadyCount");
}

void AppendDiscoverySummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.discovery"));

    aOut += "\nDiscovery: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "ready=";
    aOut += GetValue(values, "ready");
    aOut += " actors=";
    aOut += GetValue(values, "actorCount");
    aOut += "/";
    aOut += GetValue(values, "actorLimit");
    aOut += " cell=";
    aOut += GetValue(values, "playerCellFormId");
    aOut += " world=";
    aOut += GetValue(values, "playerWorldSpaceFormId");
    aOut += " grid=";
    aOut += GetValue(values, "currentGrid");
}

void AppendDiscoveryTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.discovery"));

    aOut += "\n\nDiscovery:";
    if (values.empty())
    {
        aOut += "\nmissing";
        return;
    }

    aOut += "\nready=";
    aOut += GetValue(values, "ready");
    aOut += " actors=";
    aOut += GetValue(values, "actorCount");
    aOut += "/";
    aOut += GetValue(values, "actorLimit");
    aOut += " currentGrid=";
    aOut += GetValue(values, "currentGrid");
    aOut += " centerGrid=";
    aOut += GetValue(values, "centerGrid");
    aOut += "\nplayerCell=";
    aOut += GetValue(values, "playerCellFormId");
    aOut += " playerWorld=";
    aOut += GetValue(values, "playerWorldSpaceFormId");
    aOut += " cachedWorld=";
    aOut += GetValue(values, "cachedWorldSpaceFormId");
    aOut += " cachedInterior=";
    aOut += GetValue(values, "cachedInteriorCellFormId");
    aOut += "\nlocation=";
    aOut += GetValue(values, "locationFormId");
    aOut += " firstActor=";
    aOut += GetValue(values, "actor.0.formId");
}

void AppendPlayerCellSummary(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.playercell"));

    aOut += "\nPlayer cell: ";
    if (values.empty())
    {
        aOut += "missing";
        return;
    }

    aOut += "ready=";
    aOut += GetValue(values, "ready");
    aOut += " online=";
    aOut += GetValue(values, "online");
    aOut += " generation=";
    aOut += GetValue(values, "connectionGeneration");
    aOut += " grid=";
    aOut += GetValue(values, "gridCellRequestCount");
    aOut += " exterior=";
    aOut += GetValue(values, "exteriorCellRequestCount");
    aOut += " interior=";
    aOut += GetValue(values, "interiorCellRequestCount");
    aOut += " level=";
    aOut += GetValue(values, "levelRequestCount");
}

void AppendPlayerCellTelemetry(std::string& aOut)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.playercell"));

    aOut += "\n\nPlayer cell:";
    if (values.empty())
    {
        aOut += "\nmissing";
        return;
    }

    aOut += "\nready=";
    aOut += GetValue(values, "ready");
    aOut += " online=";
    aOut += GetValue(values, "online");
    aOut += " playerId=";
    aOut += GetValue(values, "localPlayerId");
    aOut += " session=";
    aOut += GetValue(values, "sessionId");
    aOut += " generation=";
    aOut += GetValue(values, "connectionGeneration");
    aOut += " form=";
    aOut += GetValue(values, "playerFormId");
    aOut += "\nlevel current=";
    aOut += GetValue(values, "currentLevel");
    aOut += " cached=";
    aOut += GetValue(values, "cachedLevel");
    aOut += " lastSent=";
    aOut += GetValue(values, "lastLevelSent");
    aOut += " sent=";
    aOut += GetValue(values, "levelRequestCount");
    aOut += "\nrequests grid=";
    aOut += GetValue(values, "gridCellRequestCount");
    aOut += " exterior=";
    aOut += GetValue(values, "exteriorCellRequestCount");
    aOut += " interior=";
    aOut += GetValue(values, "interiorCellRequestCount");
    aOut += " skipped=";
    aOut += GetValue(values, "offlineSkippedRequestCount");
    aOut += " translateFail=";
    aOut += GetValue(values, "worldSpaceTranslationFailureCount");
    aOut += "\nlastGrid valid=";
    aOut += GetValue(values, "lastGrid.valid");
    aOut += " world=";
    aOut += GetValue(values, "lastGrid.worldSpace.serverModId");
    aOut += ":";
    aOut += GetValue(values, "lastGrid.worldSpace.serverBaseId");
    aOut += " cell=";
    aOut += GetValue(values, "lastGrid.playerCell.serverModId");
    aOut += ":";
    aOut += GetValue(values, "lastGrid.playerCell.serverBaseId");
    aOut += " center=";
    aOut += GetValue(values, "lastGrid.center");
    aOut += " count=";
    aOut += GetValue(values, "lastGrid.cellCount");
    aOut += " generation=";
    aOut += GetValue(values, "lastGrid.connectionGeneration");
    aOut += "\nlastCell valid=";
    aOut += GetValue(values, "lastCell.valid");
    aOut += " exterior=";
    aOut += GetValue(values, "lastCell.exterior");
    aOut += " generation=";
    aOut += GetValue(values, "lastCell.connectionGeneration");
    aOut += " cell=";
    aOut += GetValue(values, "lastCell.cell.serverModId");
    aOut += ":";
    aOut += GetValue(values, "lastCell.cell.serverBaseId");
    aOut += " world=";
    aOut += GetValue(values, "lastCell.worldSpace.serverModId");
    aOut += ":";
    aOut += GetValue(values, "lastCell.worldSpace.serverBaseId");
    aOut += " coords=";
    aOut += GetValue(values, "lastCell.currentCoords");
}

void AppendSaveLoadSummary(std::string& aOut, const VRSaveLoadService& acSaveLoad)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.saveload"));

    aOut += "\nLoad events: " + std::to_string(acSaveLoad.GetLoadGameCount());
    aOut += "\nPlayer ready: ";
    aOut += YesNo(acSaveLoad.IsPlayerReady());
    aOut += "\nReady after load: ";
    aOut += YesNo(acSaveLoad.IsReadyAfterLastLoad());

    if (values.empty())
    {
        aOut += "\nSave/load ids: missing";
        return;
    }

    aOut += "\nLoad cell: raw=";
    aOut += GetValue(values, "playerCellFormId");
    aOut += " server=";
    aOut += GetValue(values, "playerCell.serverModId");
    aOut += ":";
    aOut += GetValue(values, "playerCell.serverBaseId");
    aOut += "\nLoad world: raw=";
    aOut += GetValue(values, "playerWorldSpaceFormId");
    aOut += " server=";
    aOut += GetValue(values, "playerWorldSpace.serverModId");
    aOut += ":";
    aOut += GetValue(values, "playerWorldSpace.serverBaseId");
}

void AppendSaveLoadTelemetry(std::string& aOut, const VRSaveLoadService& acSaveLoad)
{
    const auto values = ReadKeyValueFile(GetHandoffPath("SkyrimTogetherVR.saveload"));

    aOut += "\n\nSave/load:";
    aOut += "\nloadEvents=" + std::to_string(acSaveLoad.GetLoadGameCount());
    aOut += " playerReady=";
    aOut += YesNo(acSaveLoad.IsPlayerReady());
    aOut += " readyAfterLoad=";
    aOut += YesNo(acSaveLoad.IsReadyAfterLastLoad());

    if (values.empty())
    {
        aOut += "\nhandoff=missing";
        return;
    }

    aOut += "\nready=";
    aOut += GetValue(values, "ready");
    aOut += " online=";
    aOut += GetValue(values, "online");
    aOut += " state=";
    aOut += GetValue(values, "connectionState");
    aOut += " observed=";
    aOut += GetValue(values, "loadGameObserved");
    aOut += " count=";
    aOut += GetValue(values, "loadGameCount");
    aOut += "\nwaitingAfterLoad=";
    aOut += GetValue(values, "waitingForReadyAfterLoad");
    aOut += " secondsSinceLoad=";
    aOut += GetValue(values, "secondsSinceLastLoad");
    aOut += "\nplayer raw=";
    aOut += GetValue(values, "playerFormId");
    aOut += " server=";
    aOut += GetValue(values, "player.serverModId");
    aOut += ":";
    aOut += GetValue(values, "player.serverBaseId");
    aOut += "\ncell raw=";
    aOut += GetValue(values, "playerCellFormId");
    aOut += " server=";
    aOut += GetValue(values, "playerCell.serverModId");
    aOut += ":";
    aOut += GetValue(values, "playerCell.serverBaseId");
    aOut += "\nworld raw=";
    aOut += GetValue(values, "playerWorldSpaceFormId");
    aOut += " server=";
    aOut += GetValue(values, "playerWorldSpace.serverModId");
    aOut += ":";
    aOut += GetValue(values, "playerWorldSpace.serverBaseId");
}

void AppendPoseNode(std::string& aOut, const char* apName, const VRPoseTransform& acNode)
{
    aOut += "\n";
    aOut += apName;
    aOut += ": ";
    if (!acNode.Valid)
    {
        aOut += "missing";
        return;
    }

    AppendPosition(aOut, acNode.Position);
}

void AppendRemotePoseNode(std::string& aOut, const char* apName, const VRPoseNodeData& acNode)
{
    aOut += " ";
    aOut += apName;
    aOut += "=";
    if (!acNode.Valid)
    {
        aOut += "missing";
        return;
    }

    AppendPosition(aOut, acNode.Position);
}

void AppendVrikSummary(std::string& aOut, const char* apName, const VRVrikData& acVrik)
{
    aOut += " ";
    aOut += apName;
    aOut += ".vrik=";
    if (!acVrik.Detected)
    {
        aOut += "missing";
        return;
    }

    aOut += acVrik.InterfaceAvailable ? "api" : "detected";
    if (acVrik.LeftFingers.Valid)
        aOut += ",lf";
    if (acVrik.RightFingers.Valid)
        aOut += ",rf";
    if (acVrik.CameraOffsetsValid)
        aOut += ",cam";
}

template <class TMap, class TAppend> void AppendFirstRemoteSamples(std::string& aOut, const TMap& acMap, size_t aLimit, TAppend&& aAppend)
{
    size_t count = 0;
    for (const auto& [playerId, value] : acMap)
    {
        if (count >= aLimit)
            break;

        aOut += "\nRemote ";
        aOut += std::to_string(playerId);
        aOut += ":";
        aOut += aAppend(value);
        ++count;
    }

    if (acMap.size() > aLimit)
    {
        aOut += "\n... ";
        aOut += std::to_string(acMap.size() - aLimit);
        aOut += " more";
    }
}
}

bool IsRemotePlayer(Actor* apActor)
{
    spdlog::info("Calling IsRemotePlayer");

    auto* pExtension = apActor->GetExtension();
    if (!pExtension)
        return false;

    return pExtension->IsRemotePlayer();
}

bool IsPlayer(Actor* apActor)
{
    spdlog::info("Calling IsPlayer");

    auto* pExtension = apActor->GetExtension();
    if (!pExtension)
        return false;

    return pExtension->IsPlayer();
}

bool ConnectToSkyrimTogether(const char* apEndpoint, const char* apPassword)
{
#if TP_SKYRIM_VR
    const auto endpoint = Trim(apEndpoint ? apEndpoint : "");
    const auto resolvedEndpoint = endpoint.empty() ? GetConfiguredEndpointString() : endpoint;
    const auto password = apPassword ? std::string(apPassword) : "";
    const auto resolvedPassword = !password.empty() || !endpoint.empty() ? password : GetConfiguredPasswordString();

    if (resolvedEndpoint.empty())
        return false;

    if (!endpoint.empty())
        WriteConnectionConfig(endpoint, password);

    return World::Get().ctx().at<VRConnectionService>().RequestConnect(resolvedEndpoint, resolvedPassword);
#else
    (void)apEndpoint;
    (void)apPassword;
    return false;
#endif
}

bool DisconnectFromSkyrimTogether()
{
#if TP_SKYRIM_VR
    return World::Get().ctx().at<VRConnectionService>().RequestDisconnect();
#else
    return false;
#endif
}

bool IsSkyrimTogetherConnected()
{
#if TP_SKYRIM_VR
    return World::Get().GetTransport().IsOnline();
#else
    return false;
#endif
}

bool SetSkyrimTogetherConnectionConfig(const char* apEndpoint, const char* apPassword)
{
#if TP_SKYRIM_VR
    const auto endpoint = Trim(apEndpoint ? apEndpoint : "");
    if (endpoint.empty())
        return false;

    const auto password = apPassword ? std::string(apPassword) : "";
    if (!WriteConnectionConfig(endpoint, password))
        return false;

    s_configuredEndpoint = endpoint;
    s_configuredPassword = password;
    return true;
#else
    (void)apEndpoint;
    (void)apPassword;
    return false;
#endif
}

const char* GetSkyrimTogetherConnectionState()
{
#if TP_SKYRIM_VR
    return World::Get().ctx().at<VRConnectionService>().GetState().c_str();
#else
    return "offline";
#endif
}

const char* GetSkyrimTogetherConfiguredEndpoint()
{
#if TP_SKYRIM_VR
    s_configuredEndpoint = GetConfiguredEndpointString();
    return s_configuredEndpoint.c_str();
#else
    return kDefaultEndpoint;
#endif
}

const char* GetSkyrimTogetherConfiguredPassword()
{
#if TP_SKYRIM_VR
    s_configuredPassword = GetConfiguredPasswordString();
    return s_configuredPassword.c_str();
#else
    return "";
#endif
}

const char* GetSkyrimTogetherStatusSummary()
{
#if TP_SKYRIM_VR
    auto& world = World::Get();
    auto& transport = world.GetTransport();
    auto& context = world.ctx();
    const auto& connection = context.at<VRConnectionService>();
    const auto& pose = context.at<VRPoseService>();

    s_statusSummary = "State: " + connection.GetState();
    s_statusSummary += "\nOnline: ";
    s_statusSummary += YesNo(transport.IsOnline());
    s_statusSummary += "\nConfigured endpoint: " + GetConfiguredEndpointString();
    s_statusSummary += "\nLocal player id: " + std::to_string(transport.GetLocalPlayerId());
    s_statusSummary += "\nLocal pose: ";
    s_statusSummary += YesNo(pose.HasSnapshot());
    s_statusSummary += "\nRemote poses: " + std::to_string(pose.GetRemotePoses().size());
    AppendRemotePlayersSummary(s_statusSummary);
    AppendDiscoverySummary(s_statusSummary);
    AppendPlayerCellSummary(s_statusSummary);
    AppendAvatarSummary(s_statusSummary);

#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    const auto& movement = context.at<VRMovementService>();
    s_statusSummary += "\nLocal movement: ";
    s_statusSummary += YesNo(movement.HasLocalMovement());
    s_statusSummary += "\nRemote movement: " + std::to_string(movement.GetRemoteMovementCount());
#endif

#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    const auto& inventory = context.at<VRInventoryService>();
    s_statusSummary += "\nLocal equipment: ";
    s_statusSummary += YesNo(inventory.HasLocalEquipment());
    s_statusSummary += "\nRemote equipment: " + std::to_string(inventory.GetRemoteEquipmentCount());
    s_statusSummary += "\nInventory sync: equipment snapshot only";
#endif

#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    const auto& activation = context.at<VRActivationService>();
    s_statusSummary += "\nLocal activation: ";
    s_statusSummary += YesNo(activation.HasLocalActivation());
    s_statusSummary += "\nRemote activation: " + std::to_string(activation.GetRemoteActivationCount());
#endif

#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    const auto& magic = context.at<VRMagicService>();
    s_statusSummary += "\nLocal magic effect: ";
    s_statusSummary += YesNo(magic.HasLocalMagicEffect());
    s_statusSummary += "\nRemote magic effects: " + std::to_string(magic.GetRemoteMagicEffectCount());
#endif

#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    const auto& combat = context.at<VRCombatService>();
    s_statusSummary += "\nLocal combat hit: ";
    s_statusSummary += YesNo(combat.HasLocalCombatHit());
    s_statusSummary += "\nRemote combat hits: " + std::to_string(combat.GetRemoteCombatHitCount());
#endif

#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    const auto& projectile = context.at<VRProjectileService>();
    s_statusSummary += "\nLocal projectile: ";
    s_statusSummary += YesNo(projectile.HasLocalProjectileEvent());
    s_statusSummary += "\nRemote projectiles: " + std::to_string(projectile.GetRemoteProjectileEventCount());
#endif

#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
    const auto& grab = context.at<VRGrabService>();
    s_statusSummary += "\nLocal grab: ";
    s_statusSummary += YesNo(grab.HasLocalGrab());
    s_statusSummary += "\nRemote grabs: " + std::to_string(grab.GetRemoteGrabCount());
#endif

#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    const auto& higgs = context.at<VRHiggsService>();
    s_statusSummary += "\nLocal HIGGS relay: ";
    s_statusSummary += YesNo(higgs.HasLocalHiggsState());
    s_statusSummary += "\nRemote HIGGS relays: " + std::to_string(higgs.GetRemoteHiggsStateCount());
#endif

#if TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE
    const auto& saveLoad = context.at<VRSaveLoadService>();
    AppendSaveLoadSummary(s_statusSummary, saveLoad);
#endif

    AppendCompatibilitySummary(s_statusSummary);
    AppendHiggsSummary(s_statusSummary);
    AppendPlanckSummary(s_statusSummary);
    return s_statusSummary.c_str();
#else
    return "State: offline\nOnline: No";
#endif
}

const char* GetSkyrimTogetherTelemetryReadout()
{
#if TP_SKYRIM_VR
    auto& world = World::Get();
    auto& transport = world.GetTransport();
    auto& context = world.ctx();
    const auto& pose = context.at<VRPoseService>();

    s_telemetryReadout = "Online: ";
    s_telemetryReadout += YesNo(transport.IsOnline());
    s_telemetryReadout += "\nLocal player id: " + std::to_string(transport.GetLocalPlayerId());
    AppendVrikSummary(s_telemetryReadout, "local", pose.GetLocalVrikData());

    s_telemetryReadout += "\n\nLocal pose:";
    if (pose.HasSnapshot())
    {
        const auto& snapshot = pose.GetLastSnapshot();
        AppendPoseNode(s_telemetryReadout, "HMD", snapshot.Hmd);
        AppendPoseNode(s_telemetryReadout, "Left hand", snapshot.LeftHand);
        AppendPoseNode(s_telemetryReadout, "Right hand", snapshot.RightHand);
    }
    else
    {
        s_telemetryReadout += "\nmissing";
    }

    s_telemetryReadout += "\n\nRemote poses: " + std::to_string(pose.GetRemotePoses().size());
    AppendFirstRemoteSamples(
        s_telemetryReadout, pose.GetRemotePoses(), 3,
        [](const VRPoseUpdate& acPose)
        {
            std::string line;
            AppendRemotePoseNode(line, "hmd", acPose.Hmd);
            AppendRemotePoseNode(line, "lh", acPose.LeftHand);
            AppendRemotePoseNode(line, "rh", acPose.RightHand);
            AppendVrikSummary(line, "ik", acPose.Vrik);
            return line;
        });

    AppendRemotePlayersTelemetry(s_telemetryReadout);
    AppendDiscoveryTelemetry(s_telemetryReadout);
    AppendPlayerCellTelemetry(s_telemetryReadout);
    AppendAvatarTelemetry(s_telemetryReadout);

#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    const auto& movement = context.at<VRMovementService>();
    s_telemetryReadout += "\n\nMovement:";
    if (movement.HasLocalMovement())
    {
        s_telemetryReadout += "\nLocal pos: ";
        AppendPosition(s_telemetryReadout, movement.GetLocalMovement().Position);
    }
    else
    {
        s_telemetryReadout += "\nLocal pos: missing";
    }

    s_telemetryReadout += "\nRemote movement: " + std::to_string(movement.GetRemoteMovementCount());
    AppendFirstRemoteSamples(
        s_telemetryReadout, movement.GetRemoteMovements(), 3,
        [](const VRMovementUpdate& acMovement)
        {
            std::string line = " pos=";
            AppendPosition(line, acMovement.Position);
            line += " dir=";
            line += std::to_string(static_cast<int32_t>(acMovement.Direction));
            return line;
        });
#endif

#if TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
    const auto& inventory = context.at<VRInventoryService>();
    s_telemetryReadout += "\n\nEquipment:";
    s_telemetryReadout += "\nPolicy: equipment snapshot only; full inventory traversal/mutation disabled";
    if (inventory.HasLocalEquipment())
    {
        const auto& localEquipment = inventory.GetLocalEquipment();
        s_telemetryReadout += "\nLocal drawn: ";
        s_telemetryReadout += YesNo(localEquipment.WeaponDrawn);
        s_telemetryReadout += " right=";
        AppendGameId(s_telemetryReadout, localEquipment.RightWeapon);
        s_telemetryReadout += " left=";
        AppendGameId(s_telemetryReadout, localEquipment.LeftWeapon);
    }
    else
    {
        s_telemetryReadout += "\nLocal equipment: missing";
    }

    s_telemetryReadout += "\nRemote equipment: " + std::to_string(inventory.GetRemoteEquipmentCount());
    AppendFirstRemoteSamples(
        s_telemetryReadout, inventory.GetRemoteEquipment(), 3,
        [](const VREquipmentUpdate& acEquipment)
        {
            std::string line = " drawn=";
            line += YesNo(acEquipment.WeaponDrawn);
            line += " right=";
            AppendGameId(line, acEquipment.RightWeapon);
            line += " left=";
            AppendGameId(line, acEquipment.LeftWeapon);
            return line;
        });
#endif

#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    const auto& activation = context.at<VRActivationService>();
    s_telemetryReadout += "\n\nActivation local=";
    s_telemetryReadout += YesNo(activation.HasLocalActivation());
    s_telemetryReadout += " remote=" + std::to_string(activation.GetRemoteActivationCount());
#endif

#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    const auto& magic = context.at<VRMagicService>();
    s_telemetryReadout += "\nMagic local=";
    s_telemetryReadout += YesNo(magic.HasLocalMagicEffect());
    s_telemetryReadout += " remote=" + std::to_string(magic.GetRemoteMagicEffectCount());
#endif

#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    const auto& combat = context.at<VRCombatService>();
    s_telemetryReadout += "\nCombat local=";
    s_telemetryReadout += YesNo(combat.HasLocalCombatHit());
    s_telemetryReadout += " remote=" + std::to_string(combat.GetRemoteCombatHitCount());
#endif

#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    const auto& projectile = context.at<VRProjectileService>();
    s_telemetryReadout += "\nProjectile local=";
    s_telemetryReadout += YesNo(projectile.HasLocalProjectileEvent());
    s_telemetryReadout += " remote=" + std::to_string(projectile.GetRemoteProjectileEventCount());
#endif

#if TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE
    const auto& grab = context.at<VRGrabService>();
    s_telemetryReadout += "\nGrab local=";
    s_telemetryReadout += YesNo(grab.HasLocalGrab());
    s_telemetryReadout += " remote=" + std::to_string(grab.GetRemoteGrabCount());
#endif

#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    const auto& higgs = context.at<VRHiggsService>();
    s_telemetryReadout += "\nHIGGS relay local=";
    s_telemetryReadout += YesNo(higgs.HasLocalHiggsState());
    s_telemetryReadout += " remote=" + std::to_string(higgs.GetRemoteHiggsStateCount());
    AppendFirstRemoteSamples(
        s_telemetryReadout, higgs.GetRemoteHiggsStates(), 3,
        [](const VRHiggsState& acState)
        {
            std::string line = " api=";
            line += YesNo(acState.InterfaceAvailable);
            line += " callbacks=";
            line += YesNo(acState.CallbacksRegistered);
            line += " snapshot=";
            line += YesNo(acState.SnapshotAvailable);
            line += " twoHanding=";
            line += YesNo(acState.TwoHanding);
            if (acState.LastEventValid)
            {
                line += " eventSeq=";
                line += std::to_string(acState.LastEvent.Sequence);
            }
            return line;
        });
#endif

#if TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE
    const auto& saveLoad = context.at<VRSaveLoadService>();
    AppendSaveLoadTelemetry(s_telemetryReadout, saveLoad);
#endif

    AppendCompatibilityTelemetry(s_telemetryReadout);
    AppendHiggsTelemetry(s_telemetryReadout);
    AppendPlanckTelemetry(s_telemetryReadout);
    return s_telemetryReadout.c_str();
#else
    return "Online: No\nTelemetry unavailable";
#endif
}

} // namespace PapyrusFunctions
