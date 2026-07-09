#include <TiltedOnlinePCH.h>

#include <Services/VRHiggsService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/RequestVRHiggsState.h>
#include <Services/TransportService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr double kHiggsSendInterval = 0.25;
constexpr double kHiggsBridgeReadInterval = 1.0 / 20.0;
constexpr double kHiggsStatusWriteInterval = 0.25;
constexpr double kRemoteHiggsStaleSeconds = 10.0;
constexpr double kHiggsBridgeStaleSeconds = 1.0;
constexpr char kHiggsBridgeStatusFileName[] = "SkyrimTogetherVR.higgs";
constexpr char kHiggsNetworkStatusFileName[] = "SkyrimTogetherVR.higgsnet";

using KeyValueMap = std::unordered_map<std::string, std::string>;

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
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

const std::string* FindValue(const KeyValueMap& acValues, const std::string& acKey)
{
    const auto it = acValues.find(acKey);
    return it != acValues.end() ? &it->second : nullptr;
}

bool HasKey(const KeyValueMap& acValues, const std::string& acKey) noexcept
{
    return acValues.find(acKey) != acValues.end();
}

bool ParseBool(const std::string* apValue, bool aDefault = false) noexcept
{
    if (!apValue)
        return aDefault;

    return *apValue == "1" || *apValue == "true" || *apValue == "True";
}

uint32_t ParseUInt32(const std::string* apValue, uint32_t aDefault = 0) noexcept
{
    if (!apValue || apValue->empty())
        return aDefault;

    char* pEnd = nullptr;
    const auto value = std::strtoul(apValue->c_str(), &pEnd, 0);
    if (pEnd == apValue->c_str())
        return aDefault;

    return static_cast<uint32_t>(value & 0xFFFFFFFFu);
}

float ParseFloat(const std::string* apValue, float aDefault = 0.0f) noexcept
{
    if (!apValue || apValue->empty())
        return aDefault;

    char* pEnd = nullptr;
    const auto value = std::strtof(apValue->c_str(), &pEnd);
    if (pEnd == apValue->c_str())
        return aDefault;

    return value;
}

glm::vec3 ParseVector3(const std::string* apValue, const glm::vec3& acDefault) noexcept
{
    if (!apValue || apValue->empty())
        return acDefault;

    const char* pCursor = apValue->c_str();
    char* pEnd = nullptr;

    const float x = std::strtof(pCursor, &pEnd);
    if (pEnd == pCursor || *pEnd != ',')
        return acDefault;

    pCursor = pEnd + 1;
    const float y = std::strtof(pCursor, &pEnd);
    if (pEnd == pCursor || *pEnd != ',')
        return acDefault;

    pCursor = pEnd + 1;
    const float z = std::strtof(pCursor, &pEnd);
    if (pEnd == pCursor)
        return acDefault;

    return {x, y, z};
}

bool GetBool(const KeyValueMap& acValues, const std::string& acKey, bool aDefault = false) noexcept
{
    return ParseBool(FindValue(acValues, acKey), aDefault);
}

uint32_t GetUInt32(const KeyValueMap& acValues, const std::string& acKey, uint32_t aDefault = 0) noexcept
{
    return ParseUInt32(FindValue(acValues, acKey), aDefault);
}

float GetFloat(const KeyValueMap& acValues, const std::string& acKey, float aDefault = 0.0f) noexcept
{
    return ParseFloat(FindValue(acValues, acKey), aDefault);
}

glm::vec3 GetVector3(const KeyValueMap& acValues, const std::string& acKey, const glm::vec3& acDefault) noexcept
{
    return ParseVector3(FindValue(acValues, acKey), acDefault);
}

GameId ToServerId(World& aWorld, uint32_t aFormId) noexcept
{
    GameId result{};
    if (aFormId)
        aWorld.GetModSystem().GetServerModId(aFormId, result);
    return result;
}

VRHiggsEventSnapshot::Kind ParseEventKind(const std::string* apValue) noexcept
{
    if (!apValue)
        return VRHiggsEventSnapshot::Kind::kUnknown;

    if (*apValue == "pulled")
        return VRHiggsEventSnapshot::Kind::kPulled;
    if (*apValue == "grabbed")
        return VRHiggsEventSnapshot::Kind::kGrabbed;
    if (*apValue == "dropped")
        return VRHiggsEventSnapshot::Kind::kDropped;
    if (*apValue == "stashed")
        return VRHiggsEventSnapshot::Kind::kStashed;
    if (*apValue == "consumed")
        return VRHiggsEventSnapshot::Kind::kConsumed;
    if (*apValue == "collision")
        return VRHiggsEventSnapshot::Kind::kCollision;
    if (*apValue == "startTwoHanding")
        return VRHiggsEventSnapshot::Kind::kStartTwoHanding;
    if (*apValue == "stopTwoHanding")
        return VRHiggsEventSnapshot::Kind::kStopTwoHanding;

    return VRHiggsEventSnapshot::Kind::kUnknown;
}

const char* ToString(VRHiggsEventSnapshot::Kind aKind) noexcept
{
    switch (aKind)
    {
    case VRHiggsEventSnapshot::Kind::kPulled:
        return "pulled";
    case VRHiggsEventSnapshot::Kind::kGrabbed:
        return "grabbed";
    case VRHiggsEventSnapshot::Kind::kDropped:
        return "dropped";
    case VRHiggsEventSnapshot::Kind::kStashed:
        return "stashed";
    case VRHiggsEventSnapshot::Kind::kConsumed:
        return "consumed";
    case VRHiggsEventSnapshot::Kind::kCollision:
        return "collision";
    case VRHiggsEventSnapshot::Kind::kStartTwoHanding:
        return "startTwoHanding";
    case VRHiggsEventSnapshot::Kind::kStopTwoHanding:
        return "stopTwoHanding";
    case VRHiggsEventSnapshot::Kind::kUnknown:
    default:
        return "unknown";
    }
}

void ParseFingers(const KeyValueMap& acValues, const std::string& acPrefix, VRHiggsFingerState& aFingers) noexcept
{
    aFingers.Valid = GetBool(acValues, acPrefix + ".fingers.valid");
    if (!aFingers.Valid)
        return;

    aFingers.Thumb = GetFloat(acValues, acPrefix + ".fingers.thumb");
    aFingers.Index = GetFloat(acValues, acPrefix + ".fingers.index");
    aFingers.Middle = GetFloat(acValues, acPrefix + ".fingers.middle");
    aFingers.Ring = GetFloat(acValues, acPrefix + ".fingers.ring");
    aFingers.Pinky = GetFloat(acValues, acPrefix + ".fingers.pinky");
}

void ParseTransform(const KeyValueMap& acValues, const std::string& acPrefix, VRHiggsGrabTransform& aTransform) noexcept
{
    const auto transformPrefix = acPrefix + ".grabTransform";
    aTransform.Valid = GetBool(acValues, transformPrefix + ".valid");
    if (!aTransform.Valid)
        return;

    aTransform.Translate = GetVector3(acValues, transformPrefix + ".translate", {});
    aTransform.AxisX = GetVector3(acValues, transformPrefix + ".axisX", {1.0f, 0.0f, 0.0f});
    aTransform.AxisY = GetVector3(acValues, transformPrefix + ".axisY", {0.0f, 1.0f, 0.0f});
    aTransform.AxisZ = GetVector3(acValues, transformPrefix + ".axisZ", {0.0f, 0.0f, 1.0f});
    aTransform.Scale = GetFloat(acValues, transformPrefix + ".scale", 1.0f);
}

void ParseHandState(World& aWorld, const KeyValueMap& acValues, const std::string& acPrefix, VRHiggsHandState& aHand) noexcept
{
    aHand.Valid = GetBool(acValues, acPrefix + ".valid");
    if (!aHand.Valid)
        return;

    aHand.HoldingObject = GetBool(acValues, acPrefix + ".holdingObject");
    aHand.CanGrabObject = GetBool(acValues, acPrefix + ".canGrabObject");
    aHand.HandInGrabbableState = GetBool(acValues, acPrefix + ".handInGrabbableState");
    aHand.Disabled = GetBool(acValues, acPrefix + ".disabled");
    aHand.WeaponCollisionDisabled = GetBool(acValues, acPrefix + ".weaponCollisionDisabled");
    aHand.GrabbedObject = ToServerId(aWorld, GetUInt32(acValues, acPrefix + ".grabbedObjectFormId"));
    ParseFingers(acValues, acPrefix, aHand.Fingers);
    ParseTransform(acValues, acPrefix, aHand.GrabTransform);
}

void ParseLastEvent(World& aWorld, const KeyValueMap& acValues, VRHiggsState& aState) noexcept
{
    const auto eventCount = GetUInt32(acValues, "recentEventCount");
    if (eventCount == 0)
        return;

    const auto prefix = std::string("recentEvent.") + std::to_string(eventCount - 1);
    aState.LastEventValid = true;
    aState.LastEvent.Sequence = GetUInt32(acValues, prefix + ".sequence");
    aState.LastEvent.EventKind = ParseEventKind(FindValue(acValues, prefix + ".type"));
    aState.LastEvent.HasHand = GetBool(acValues, prefix + ".hasHand");
    const auto* pHand = FindValue(acValues, prefix + ".hand");
    aState.LastEvent.IsLeft = pHand && *pHand == "left";
    aState.LastEvent.ObjectId = ToServerId(aWorld, GetUInt32(acValues, prefix + ".formId"));
    aState.LastEvent.Mass = GetFloat(acValues, prefix + ".mass");
    aState.LastEvent.SeparatingVelocity = GetFloat(acValues, prefix + ".separatingVelocity");
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasCoherentHiggsFingerState(const KeyValueMap& acValues, const std::string& acPrefix) noexcept
{
    if (!HasKey(acValues, acPrefix + ".fingers.valid"))
        return false;

    if (!GetBool(acValues, acPrefix + ".fingers.valid"))
        return true;

    return HasKey(acValues, acPrefix + ".fingers.thumb") && HasKey(acValues, acPrefix + ".fingers.index") &&
           HasKey(acValues, acPrefix + ".fingers.middle") && HasKey(acValues, acPrefix + ".fingers.ring") &&
           HasKey(acValues, acPrefix + ".fingers.pinky");
}

bool HasCoherentHiggsGrabTransform(const KeyValueMap& acValues, const std::string& acPrefix) noexcept
{
    const auto transformPrefix = acPrefix + ".grabTransform";
    if (!HasKey(acValues, transformPrefix + ".valid"))
        return false;

    if (!GetBool(acValues, transformPrefix + ".valid"))
        return true;

    return HasKey(acValues, transformPrefix + ".translate") && HasKey(acValues, transformPrefix + ".axisX") &&
           HasKey(acValues, transformPrefix + ".axisY") && HasKey(acValues, transformPrefix + ".axisZ") &&
           HasKey(acValues, transformPrefix + ".scale");
}

bool HasCoherentHiggsHandState(const KeyValueMap& acValues, const std::string& acPrefix) noexcept
{
    if (!HasKey(acValues, acPrefix + ".valid"))
        return false;

    if (!GetBool(acValues, acPrefix + ".valid"))
        return true;

    return HasKey(acValues, acPrefix + ".holdingObject") && HasKey(acValues, acPrefix + ".canGrabObject") &&
           HasKey(acValues, acPrefix + ".handInGrabbableState") && HasKey(acValues, acPrefix + ".disabled") &&
           HasKey(acValues, acPrefix + ".weaponCollisionDisabled") &&
           HasKey(acValues, acPrefix + ".grabbedObjectFormId") &&
           HasCoherentHiggsFingerState(acValues, acPrefix) &&
           HasCoherentHiggsGrabTransform(acValues, acPrefix);
}

bool HasCoherentHiggsEventState(const KeyValueMap& acValues) noexcept
{
    if (!HasKey(acValues, "recentEventCount"))
        return false;

    const auto eventCount = GetUInt32(acValues, "recentEventCount");
    if (eventCount == 0)
        return true;

    const auto prefix = std::string("recentEvent.") + std::to_string(eventCount - 1);
    return HasKey(acValues, prefix + ".sequence") && HasKey(acValues, prefix + ".type") &&
           HasKey(acValues, prefix + ".hasHand") && HasKey(acValues, prefix + ".hand") &&
           HasKey(acValues, prefix + ".formId") && HasKey(acValues, prefix + ".mass") &&
           HasKey(acValues, prefix + ".separatingVelocity");
}

bool HasCoherentHiggsBridgeData(const KeyValueMap& acValues) noexcept
{
    if (!GetBool(acValues, "bridge.loaded"))
        return false;

    return HasKey(acValues, "bridge.sequence") && HasKey(acValues, "higgs.detected") &&
           HasKey(acValues, "higgs.interfaceAvailable") && HasKey(acValues, "higgs.callbacksRegistered") &&
           HasKey(acValues, "higgs.snapshotAvailable") && HasKey(acValues, "higgs.snapshotSequence") &&
           HasKey(acValues, "higgs.twoHanding") && HasCoherentHiggsHandState(acValues, "left") &&
           HasCoherentHiggsHandState(acValues, "right") && HasCoherentHiggsEventState(acValues);
}

void WriteGameId(std::ofstream& aFile, const std::string& acPrefix, const GameId& acId)
{
    aFile << acPrefix << ".serverModId=" << acId.ModId << "\n";
    aFile << acPrefix << ".serverBaseId=" << acId.BaseId << "\n";
}

void WriteFingers(std::ofstream& aFile, const std::string& acPrefix, const VRHiggsFingerState& acFingers)
{
    aFile << acPrefix << ".valid=" << (acFingers.Valid ? "1" : "0") << "\n";
    if (!acFingers.Valid)
        return;

    aFile << acPrefix << ".thumb=" << acFingers.Thumb << "\n";
    aFile << acPrefix << ".index=" << acFingers.Index << "\n";
    aFile << acPrefix << ".middle=" << acFingers.Middle << "\n";
    aFile << acPrefix << ".ring=" << acFingers.Ring << "\n";
    aFile << acPrefix << ".pinky=" << acFingers.Pinky << "\n";
}

void WriteTransform(std::ofstream& aFile, const std::string& acPrefix, const VRHiggsGrabTransform& acTransform)
{
    aFile << acPrefix << ".valid=" << (acTransform.Valid ? "1" : "0") << "\n";
    if (!acTransform.Valid)
        return;

    aFile << acPrefix << ".translate=" << acTransform.Translate.x << "," << acTransform.Translate.y << "," << acTransform.Translate.z << "\n";
    aFile << acPrefix << ".axisX=" << acTransform.AxisX.x << "," << acTransform.AxisX.y << "," << acTransform.AxisX.z << "\n";
    aFile << acPrefix << ".axisY=" << acTransform.AxisY.x << "," << acTransform.AxisY.y << "," << acTransform.AxisY.z << "\n";
    aFile << acPrefix << ".axisZ=" << acTransform.AxisZ.x << "," << acTransform.AxisZ.y << "," << acTransform.AxisZ.z << "\n";
    aFile << acPrefix << ".scale=" << acTransform.Scale << "\n";
}

void WriteHandState(std::ofstream& aFile, const std::string& acPrefix, const VRHiggsHandState& acHand)
{
    aFile << acPrefix << ".valid=" << (acHand.Valid ? "1" : "0") << "\n";
    if (!acHand.Valid)
        return;

    aFile << acPrefix << ".holdingObject=" << (acHand.HoldingObject ? "1" : "0") << "\n";
    aFile << acPrefix << ".canGrabObject=" << (acHand.CanGrabObject ? "1" : "0") << "\n";
    aFile << acPrefix << ".handInGrabbableState=" << (acHand.HandInGrabbableState ? "1" : "0") << "\n";
    aFile << acPrefix << ".disabled=" << (acHand.Disabled ? "1" : "0") << "\n";
    aFile << acPrefix << ".weaponCollisionDisabled=" << (acHand.WeaponCollisionDisabled ? "1" : "0") << "\n";
    WriteGameId(aFile, acPrefix + ".grabbedObject", acHand.GrabbedObject);
    WriteFingers(aFile, acPrefix + ".fingers", acHand.Fingers);
    WriteTransform(aFile, acPrefix + ".grabTransform", acHand.GrabTransform);
}

void WriteEventSnapshot(std::ofstream& aFile, const std::string& acPrefix, const VRHiggsEventSnapshot& acEvent)
{
    aFile << acPrefix << ".sequence=" << acEvent.Sequence << "\n";
    aFile << acPrefix << ".kind=" << ToString(acEvent.EventKind) << "\n";
    aFile << acPrefix << ".hasHand=" << (acEvent.HasHand ? "1" : "0") << "\n";
    aFile << acPrefix << ".hand=" << (acEvent.IsLeft ? "left" : "right") << "\n";
    WriteGameId(aFile, acPrefix + ".object", acEvent.ObjectId);
    aFile << acPrefix << ".mass=" << acEvent.Mass << "\n";
    aFile << acPrefix << ".separatingVelocity=" << acEvent.SeparatingVelocity << "\n";
}

void WriteHiggsState(std::ofstream& aFile, const std::string& acPrefix, const VRHiggsState& acState)
{
    aFile << acPrefix << ".sequence=" << acState.Sequence << "\n";
    aFile << acPrefix << ".bridgeLoaded=" << (acState.BridgeLoaded ? "1" : "0") << "\n";
    aFile << acPrefix << ".detected=" << (acState.Detected ? "1" : "0") << "\n";
    aFile << acPrefix << ".interfaceAvailable=" << (acState.InterfaceAvailable ? "1" : "0") << "\n";
    aFile << acPrefix << ".callbacksRegistered=" << (acState.CallbacksRegistered ? "1" : "0") << "\n";
    aFile << acPrefix << ".snapshotAvailable=" << (acState.SnapshotAvailable ? "1" : "0") << "\n";
    aFile << acPrefix << ".snapshotSequence=" << acState.SnapshotSequence << "\n";
    aFile << acPrefix << ".twoHanding=" << (acState.TwoHanding ? "1" : "0") << "\n";
    WriteHandState(aFile, acPrefix + ".left", acState.Left);
    WriteHandState(aFile, acPrefix + ".right", acState.Right);
    aFile << acPrefix << ".lastEvent.valid=" << (acState.LastEventValid ? "1" : "0") << "\n";
    if (acState.LastEventValid)
        WriteEventSnapshot(aFile, acPrefix + ".lastEvent", acState.LastEvent);
}
} // namespace

VRHiggsService::VRHiggsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_bridgeStatusPath(m_handoffDir / kHiggsBridgeStatusFileName)
    , m_networkStatusPath(m_handoffDir / kHiggsNetworkStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR HIGGS network handoff status file: {}", m_networkStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRHiggsService::OnUpdate>(this);
    m_vrHiggsStateConnection = aDispatcher.sink<NotifyVRHiggsState>().connect<&VRHiggsService::OnVRHiggsState>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRHiggsService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRHiggsService::OnDisconnected>(this);
}

void VRHiggsService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    m_bridgeReadTimer += acEvent.Delta;
    if (!m_bridgeReadInitialized || m_bridgeReadTimer >= kHiggsBridgeReadInterval)
    {
        m_bridgeReadTimer = 0.0;
        m_bridgeReadInitialized = true;

        VRHiggsState state{};
        if (CaptureLocalHiggsState(state))
        {
            if (!m_hasLocalState || state != m_lastLocalState)
            {
                m_lastLocalState = state;
                m_hasLocalState = true;
                m_statusDirty = true;
            }
        }
        else if (m_hasLocalState)
        {
            m_lastLocalState = {};
            m_hasLocalState = false;
            m_statusDirty = true;
        }
    }

    PruneRemoteStates(acEvent.Delta);

    m_sendTimer += acEvent.Delta;
    if (m_sendTimer >= kHiggsSendInterval)
    {
        m_sendTimer = 0.0;
        SendHiggsState();
    }

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kHiggsStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteHiggsNetworkStatusFile();
}

void VRHiggsService::OnVRHiggsState(const NotifyVRHiggsState& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    const auto existingIt = m_remoteStates.find(acMessage.PlayerId);
    if (existingIt != m_remoteStates.end() && !IsNewerSequence(acMessage.State.Sequence, existingIt->second.Sequence))
        return;

    m_remoteStates[acMessage.PlayerId] = acMessage.State;
    m_remoteStateAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRHiggsService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto stateCount = m_remoteStates.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteStateAges.erase(acMessage.PlayerId);
    if (stateCount || ageCount)
        m_statusDirty = true;
}

void VRHiggsService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remoteStates.empty() || !m_remoteStateAges.empty())
    {
        m_remoteStates.clear();
        m_remoteStateAges.clear();
        m_statusDirty = true;
    }
}

void VRHiggsService::PruneRemoteStates(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remoteStates.empty() || !m_remoteStateAges.empty())
        {
            m_remoteStates.clear();
            m_remoteStateAges.clear();
            m_statusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remoteStateAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemoteHiggsStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remoteStateAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remoteStateAges.erase(playerId);
        m_remoteStates.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRHiggsService::CaptureLocalHiggsState(VRHiggsState& aState) noexcept
{
    using Clock = std::chrono::steady_clock;

    static VRHiggsState s_cachedBridgeState{};
    static uint32_t s_cachedBridgeSequence = 0;
    static std::string s_cachedBridgeEpoch;
    static Clock::time_point s_cachedBridgeTime{};
    static bool s_hasCachedBridgeState = false;

    const auto values = ReadKeyValueFile(m_bridgeStatusPath);
    const auto now = Clock::now();
    const auto cachedFresh =
        s_hasCachedBridgeState &&
        std::chrono::duration<double>(now - s_cachedBridgeTime).count() <= kHiggsBridgeStaleSeconds;

    if (values.empty() || !HasCoherentHiggsBridgeData(values))
    {
        if (cachedFresh)
        {
            aState = s_cachedBridgeState;
            return true;
        }
        return false;
    }

    const auto bridgeSequence = GetUInt32(values, "bridge.sequence");
    const auto* pBridgeEpoch = FindValue(values, "bridge.epoch");
    const std::string bridgeEpoch = pBridgeEpoch ? *pBridgeEpoch : "legacy";
    if (bridgeSequence == 0)
    {
        if (cachedFresh)
        {
            aState = s_cachedBridgeState;
            return true;
        }
        return false;
    }

    const bool sameBridgeEpoch = s_hasCachedBridgeState && bridgeEpoch == s_cachedBridgeEpoch;
    if (sameBridgeEpoch && !IsNewerSequence(bridgeSequence, s_cachedBridgeSequence))
    {
        if (cachedFresh)
        {
            aState = s_cachedBridgeState;
            return true;
        }
        return false;
    }

    aState.Sequence = bridgeSequence;
    aState.BridgeLoaded = GetBool(values, "bridge.loaded");
    aState.Detected = GetBool(values, "higgs.detected");
    aState.InterfaceAvailable = GetBool(values, "higgs.interfaceAvailable");
    aState.CallbacksRegistered = GetBool(values, "higgs.callbacksRegistered");
    aState.SnapshotAvailable = GetBool(values, "higgs.snapshotAvailable");
    aState.SnapshotSequence = GetUInt32(values, "higgs.snapshotSequence");
    aState.TwoHanding = GetBool(values, "higgs.twoHanding");
    ParseHandState(m_world, values, "left", aState.Left);
    ParseHandState(m_world, values, "right", aState.Right);
    ParseLastEvent(m_world, values, aState);

    const bool hasState = aState.BridgeLoaded || aState.Detected || aState.InterfaceAvailable ||
                          aState.CallbacksRegistered || aState.SnapshotAvailable || aState.Left.Valid ||
                          aState.Right.Valid;
    if (hasState)
    {
        s_cachedBridgeState = aState;
        s_cachedBridgeSequence = bridgeSequence;
        s_cachedBridgeEpoch = bridgeEpoch;
        s_cachedBridgeTime = now;
        s_hasCachedBridgeState = true;
    }

    return hasState;
}

void VRHiggsService::SendHiggsState() noexcept
{
    if (!m_transport.IsOnline() || !m_hasLocalState)
        return;

    RequestVRHiggsState request{};
    request.State = m_lastLocalState;
    m_transport.Send(request);
}

void VRHiggsService::WriteHiggsNetworkStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_networkStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "ready=" << (m_hasLocalState ? "1" : "0") << "\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localHiggsAvailable=" << (m_hasLocalState ? "1" : "0") << "\n";
    file << "remoteHiggsCount=" << m_remoteStates.size() << "\n";

    if (m_hasLocalState)
        WriteHiggsState(file, "localHiggs", m_lastLocalState);

    for (const auto& [playerId, state] : m_remoteStates)
    {
        const auto ageIt = m_remoteStateAges.find(playerId);
        const auto age = ageIt != m_remoteStateAges.end() ? ageIt->second : 0.0;
        const auto prefix = std::string("remoteHiggs.") + std::to_string(playerId);

        file << prefix << ".ageSeconds=" << age << "\n";
        WriteHiggsState(file, prefix, state);
    }

    m_statusDirty = false;
}
