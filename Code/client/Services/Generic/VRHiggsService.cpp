#include <TiltedOnlinePCH.h>

#include <Services/VRHiggsService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRHiggsState.h>
#include <Messages/RequestVRHiggsState.h>
#include <Structs/GameplayCapabilities.h>
#include <Services/TransportService.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace
{
// Held-object replay needs the same 20 Hz sampling cadence as the bridge
// readout. Mutation edges remain independently ordered and deduplicated.
constexpr double kHiggsSendInterval = 1.0 / 20.0;
constexpr double kHiggsBridgeReadInterval = 1.0 / 20.0;
constexpr double kHiggsStatusWriteInterval = 0.25;
constexpr double kRemoteHiggsStaleSeconds = 10.0;
constexpr std::uint64_t kHiggsBridgeStaleMilliseconds = 1000;
constexpr std::uint64_t kHiggsBridgeMaximumFutureSkewMilliseconds = 5000;
constexpr auto kHiggsBridgeReadoutLogInterval = std::chrono::seconds(5);
constexpr char kHiggsBridgeStatusFileName[] = "SkyrimTogetherVR.higgs";
constexpr char kHiggsNetworkStatusFileName[] = "SkyrimTogetherVR.higgsnet";

using KeyValueMap = std::unordered_map<std::string, std::string>;

enum class HiggsBridgeReadoutRejection : std::uint8_t
{
    None,
    Unavailable,
    MissingIdentity,
    MalformedIdentity,
    CurrentIdentityUnavailable,
    LaunchNonceMismatch,
    PriorProcess,
    GameRootMismatch,
    MissingFreshness,
    MalformedFreshness,
    Stale,
    FutureSkewed,
    MalformedState,
};

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

bool ReadKeyValueFile(const std::filesystem::path& acPath, KeyValueMap& arValues)
{
    arValues.clear();
    std::ifstream file(acPath);
    if (!file)
        return false;

    std::string line;
    while (std::getline(file, line))
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0)
            return false;

        if (!arValues.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
            return false;
    }

    return !arValues.empty() && !file.bad();
}

const std::string* FindValue(const KeyValueMap& acValues, const std::string& acKey)
{
    const auto it = acValues.find(acKey);
    return it != acValues.end() ? &it->second : nullptr;
}

bool TryParseBool(const std::string* apValue, bool& arValue) noexcept
{
    if (!apValue)
        return false;

    if (*apValue == "1" || *apValue == "true" || *apValue == "True")
    {
        arValue = true;
        return true;
    }
    if (*apValue == "0" || *apValue == "false" || *apValue == "False")
    {
        arValue = false;
        return true;
    }

    return false;
}

bool TryParseUInt32(const std::string* apValue, uint32_t& arValue) noexcept
{
    if (!apValue || apValue->empty())
        return false;

    const auto [pEnd, error] = std::from_chars(apValue->data(), apValue->data() + apValue->size(), arValue);
    return error == std::errc{} && pEnd == apValue->data() + apValue->size();
}

bool TryParseUInt64(const std::string* apValue, uint64_t& arValue) noexcept
{
    if (!apValue || apValue->empty())
        return false;

    const auto [pEnd, error] = std::from_chars(apValue->data(), apValue->data() + apValue->size(), arValue);
    return error == std::errc{} && pEnd == apValue->data() + apValue->size();
}

bool TryParseFloat(const std::string* apValue, float& arValue) noexcept
{
    if (!apValue || apValue->empty())
        return false;

    char* pEnd = nullptr;
    const auto value = std::strtof(apValue->c_str(), &pEnd);
    if (pEnd == apValue->c_str() || *pEnd != '\0' || !std::isfinite(value))
        return false;

    arValue = value;
    return true;
}

bool TryParseVector3(const std::string* apValue, glm::vec3& arValue) noexcept
{
    if (!apValue || apValue->empty())
        return false;

    const char* pCursor = apValue->c_str();
    char* pEnd = nullptr;

    const float x = std::strtof(pCursor, &pEnd);
    if (pEnd == pCursor || *pEnd != ',')
        return false;

    pCursor = pEnd + 1;
    const float y = std::strtof(pCursor, &pEnd);
    if (pEnd == pCursor || *pEnd != ',')
        return false;

    pCursor = pEnd + 1;
    const float z = std::strtof(pCursor, &pEnd);
    if (pEnd == pCursor || *pEnd != '\0' || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;

    arValue = {x, y, z};
    return true;
}

bool ParseBool(const std::string* apValue, bool aDefault = false) noexcept
{
    bool value{};
    return TryParseBool(apValue, value) ? value : aDefault;
}

uint32_t ParseUInt32(const std::string* apValue, uint32_t aDefault = 0) noexcept
{
    uint32_t value{};
    return TryParseUInt32(apValue, value) ? value : aDefault;
}

uint64_t ParseUInt64(const std::string* apValue, uint64_t aDefault = 0) noexcept
{
    uint64_t value{};
    return TryParseUInt64(apValue, value) ? value : aDefault;
}

float ParseFloat(const std::string* apValue, float aDefault = 0.0f) noexcept
{
    float value{};
    return TryParseFloat(apValue, value) ? value : aDefault;
}

glm::vec3 ParseVector3(const std::string* apValue, const glm::vec3& acDefault) noexcept
{
    glm::vec3 value{};
    return TryParseVector3(apValue, value) ? value : acDefault;
}

bool HasStrictBool(const KeyValueMap& acValues, const std::string& acKey) noexcept
{
    bool value{};
    return TryParseBool(FindValue(acValues, acKey), value);
}

bool HasStrictUInt32(const KeyValueMap& acValues, const std::string& acKey) noexcept
{
    uint32_t value{};
    return TryParseUInt32(FindValue(acValues, acKey), value);
}

bool HasStrictFloat(const KeyValueMap& acValues, const std::string& acKey) noexcept
{
    float value{};
    return TryParseFloat(FindValue(acValues, acKey), value);
}

bool HasStrictVector3(const KeyValueMap& acValues, const std::string& acKey) noexcept
{
    glm::vec3 value{};
    return TryParseVector3(FindValue(acValues, acKey), value);
}

std::filesystem::path CanonicalizeHandoffGamePath(const std::filesystem::path& acPath)
{
    std::error_code ec;
    auto canonicalPath = std::filesystem::weakly_canonical(acPath, ec);
    if (!ec)
        return canonicalPath.lexically_normal();

    ec.clear();
    canonicalPath = std::filesystem::absolute(acPath, ec);
    return ec ? std::filesystem::path{} : canonicalPath.lexically_normal();
}

bool IsSameHandoffGamePath(const std::filesystem::path& acExpected,
                           const std::filesystem::path& acCandidate) noexcept
{
#if defined(_WIN32)
    return _wcsicmp(acExpected.c_str(), acCandidate.c_str()) == 0;
#else
    return acExpected == acCandidate;
#endif
}

uint64_t CurrentUnixMilliseconds() noexcept
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return now > 0 ? static_cast<uint64_t>(now) : 0;
}

HiggsBridgeReadoutRejection ValidateHiggsBridgeIdentity(
    const KeyValueMap& acValues, const uint64_t aNowUnixMilliseconds) noexcept
{
    const auto* pReadoutNonce = FindValue(acValues, "launchNonce");
    const auto* pReadoutProcessId = FindValue(acValues, "processId");
    const auto* pReadoutGamePath = FindValue(acValues, "gamePath");
    if (!pReadoutNonce || !pReadoutProcessId || !pReadoutGamePath || pReadoutGamePath->empty())
        return HiggsBridgeReadoutRejection::MissingIdentity;

    const auto currentLaunchNonce = SkyrimTogetherVR::Handoff::GetLaunchNonce();
    if (currentLaunchNonce.empty())
        return HiggsBridgeReadoutRejection::CurrentIdentityUnavailable;

    std::string readoutLaunchNonce;
    if (!SkyrimTogetherVR::Handoff::NormalizeLaunchNonce(*pReadoutNonce, readoutLaunchNonce))
        return HiggsBridgeReadoutRejection::MalformedIdentity;
    if (readoutLaunchNonce != currentLaunchNonce)
        return HiggsBridgeReadoutRejection::LaunchNonceMismatch;

    uint32_t readoutProcessId{};
    if (!TryParseUInt32(pReadoutProcessId, readoutProcessId) || readoutProcessId == 0)
        return HiggsBridgeReadoutRejection::MalformedIdentity;
    if (readoutProcessId != SkyrimTogetherVR::Handoff::GetProcessId())
        return HiggsBridgeReadoutRejection::PriorProcess;

    const auto currentGamePath = CanonicalizeHandoffGamePath(SkyrimTogetherVR::Handoff::GetGameDirectory());
    const auto readoutGamePath = CanonicalizeHandoffGamePath(std::filesystem::path(*pReadoutGamePath));
    if (currentGamePath.empty() || readoutGamePath.empty())
        return HiggsBridgeReadoutRejection::MalformedIdentity;
    if (!IsSameHandoffGamePath(currentGamePath, readoutGamePath))
        return HiggsBridgeReadoutRejection::GameRootMismatch;

    const auto* pWriteTime = FindValue(acValues, "bridge.writeUnixMilliseconds");
    if (!pWriteTime)
        return HiggsBridgeReadoutRejection::MissingFreshness;

    uint64_t writeUnixMilliseconds{};
    if (!TryParseUInt64(pWriteTime, writeUnixMilliseconds) || writeUnixMilliseconds == 0 ||
        aNowUnixMilliseconds == 0)
        return HiggsBridgeReadoutRejection::MalformedFreshness;
    if (writeUnixMilliseconds > aNowUnixMilliseconds &&
        writeUnixMilliseconds - aNowUnixMilliseconds > kHiggsBridgeMaximumFutureSkewMilliseconds)
        return HiggsBridgeReadoutRejection::FutureSkewed;
    if (aNowUnixMilliseconds >= writeUnixMilliseconds &&
        aNowUnixMilliseconds - writeUnixMilliseconds > kHiggsBridgeStaleMilliseconds)
        return HiggsBridgeReadoutRejection::Stale;

    return HiggsBridgeReadoutRejection::None;
}

const char* DescribeHiggsBridgeReadoutRejection(const HiggsBridgeReadoutRejection aRejection) noexcept
{
    switch (aRejection)
    {
    case HiggsBridgeReadoutRejection::Unavailable:
        return "readout is unavailable or has duplicate/malformed key-value data";
    case HiggsBridgeReadoutRejection::MissingIdentity:
        return "readout is missing launch identity";
    case HiggsBridgeReadoutRejection::MalformedIdentity:
        return "readout launch identity is malformed";
    case HiggsBridgeReadoutRejection::CurrentIdentityUnavailable:
        return "current launch identity is unavailable";
    case HiggsBridgeReadoutRejection::LaunchNonceMismatch:
        return "readout launch nonce does not match the current launch";
    case HiggsBridgeReadoutRejection::PriorProcess:
        return "readout process ID belongs to an earlier or different process";
    case HiggsBridgeReadoutRejection::GameRootMismatch:
        return "readout game path does not match the current game root";
    case HiggsBridgeReadoutRejection::MissingFreshness:
        return "readout is missing its write freshness field";
    case HiggsBridgeReadoutRejection::MalformedFreshness:
        return "readout write freshness is malformed";
    case HiggsBridgeReadoutRejection::Stale:
        return "readout write freshness is stale";
    case HiggsBridgeReadoutRejection::FutureSkewed:
        return "readout write freshness exceeds the allowed future clock skew";
    case HiggsBridgeReadoutRejection::MalformedState:
        return "readout HIGGS state is malformed";
    case HiggsBridgeReadoutRejection::None:
    default:
        return "readout is valid";
    }
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
    const auto* nodeName = FindValue(acValues, acPrefix + ".grabbedNodeName");
    if (!nodeName || nodeName->size() >= aHand.GrabbedNodeName.size() ||
        std::any_of(nodeName->begin(), nodeName->end(), [](const char character) noexcept {
            return character == '\0' || character == '\r' || character == '\n';
        })) {
        aHand.Valid = false;
        return;
    }
    std::copy(nodeName->begin(), nodeName->end(), aHand.GrabbedNodeName.begin());
    aHand.GrabbedNodeNameLength = static_cast<uint8_t>(nodeName->size());
    ParseFingers(acValues, acPrefix, aHand.Fingers);
    ParseTransform(acValues, acPrefix, aHand.GrabTransform);
}

bool IsMutationEvent(const VRHiggsEventSnapshot::Kind aKind) noexcept
{
    // Terminal stash/consume edges release remote visual/lease state only.
    // Canonical inventory remains the sole durable mutation authority.
    return aKind == VRHiggsEventSnapshot::Kind::kPulled ||
           aKind == VRHiggsEventSnapshot::Kind::kGrabbed ||
           aKind == VRHiggsEventSnapshot::Kind::kDropped ||
           aKind == VRHiggsEventSnapshot::Kind::kStashed ||
           aKind == VRHiggsEventSnapshot::Kind::kConsumed;
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

std::uint32_t NextNonZeroSequence(const std::uint32_t aCurrent) noexcept
{
    const auto next = aCurrent + 1;
    return next != 0 ? next : 1;
}

bool ParseMutationEvents(World& aWorld, const KeyValueMap& acValues, VRHiggsState& arState) noexcept
{
    const auto eventCount = GetUInt32(acValues, "recentEventCount");
    if (eventCount > kMaximumHiggsMutationEvents)
        return false;

    std::uint32_t previousMutationSequence{};
    bool hasPreviousMutationSequence{false};
    for (std::uint32_t index = 0; index < eventCount; ++index)
    {
        const auto prefix = std::string("recentEvent.") + std::to_string(index);
        VRHiggsEventSnapshot event{};
        event.Sequence = GetUInt32(acValues, prefix + ".sequence");
        event.EventKind = ParseEventKind(FindValue(acValues, prefix + ".type"));
        event.HasHand = GetBool(acValues, prefix + ".hasHand");
        const auto* pHand = FindValue(acValues, prefix + ".hand");
        event.IsLeft = pHand && *pHand == "left";
        event.ObjectId = ToServerId(aWorld, GetUInt32(acValues, prefix + ".formId"));
        event.InventoryBaseForm = ToServerId(aWorld, GetUInt32(acValues, prefix + ".inventoryBaseFormId"));
        event.Mass = GetFloat(acValues, prefix + ".mass");
        event.SeparatingVelocity = GetFloat(acValues, prefix + ".separatingVelocity");
        ParseTransform(acValues, prefix, event.GrabTransform);
        const auto* nodeName = FindValue(acValues, prefix + ".grabbedNodeName");
        if (!nodeName || nodeName->size() >= event.GrabbedNodeName.size() ||
            std::any_of(nodeName->begin(), nodeName->end(), [](const char character) noexcept {
                return character == '\0' || character == '\r' || character == '\n';
            }))
            return false;
        std::copy(nodeName->begin(), nodeName->end(), event.GrabbedNodeName.begin());
        event.GrabbedNodeNameLength = static_cast<uint8_t>(nodeName->size());

        // Collisions and two-handing callbacks remain snapshot telemetry.
        if (!IsMutationEvent(event.EventKind))
            continue;
        if (event.Sequence == 0 || (hasPreviousMutationSequence &&
                                    !IsNewerSequence(event.Sequence, previousMutationSequence)))
            return false;
        if (arState.MutationEventCount >= arState.MutationEvents.size())
            return false;

        arState.MutationEvents[arState.MutationEventCount++] = event;
        previousMutationSequence = event.Sequence;
        hasPreviousMutationSequence = true;
    }

    arState.MutationSequence = hasPreviousMutationSequence ? previousMutationSequence : 0;
    return true;
}

bool HasCoherentHiggsFingerState(const KeyValueMap& acValues, const std::string& acPrefix) noexcept
{
    if (!HasStrictBool(acValues, acPrefix + ".fingers.valid"))
        return false;

    if (!GetBool(acValues, acPrefix + ".fingers.valid"))
        return true;

    return HasStrictFloat(acValues, acPrefix + ".fingers.thumb") &&
           HasStrictFloat(acValues, acPrefix + ".fingers.index") &&
           HasStrictFloat(acValues, acPrefix + ".fingers.middle") &&
           HasStrictFloat(acValues, acPrefix + ".fingers.ring") &&
           HasStrictFloat(acValues, acPrefix + ".fingers.pinky");
}

bool HasCoherentHiggsGrabTransform(const KeyValueMap& acValues, const std::string& acPrefix) noexcept
{
    const auto transformPrefix = acPrefix + ".grabTransform";
    if (!HasStrictBool(acValues, transformPrefix + ".valid"))
        return false;

    if (!GetBool(acValues, transformPrefix + ".valid"))
        return true;

    glm::vec3 translate{};
    return HasStrictVector3(acValues, transformPrefix + ".translate") &&
           TryParseVector3(FindValue(acValues, transformPrefix + ".translate"), translate) &&
           Vector3_NetQuantize::IsInRange(translate) &&
           HasStrictVector3(acValues, transformPrefix + ".axisX") &&
           HasStrictVector3(acValues, transformPrefix + ".axisY") &&
           HasStrictVector3(acValues, transformPrefix + ".axisZ") &&
           HasStrictFloat(acValues, transformPrefix + ".scale");
}

bool HasCoherentHiggsHandState(const KeyValueMap& acValues, const std::string& acPrefix) noexcept
{
    if (!HasStrictBool(acValues, acPrefix + ".valid"))
        return false;

    if (!GetBool(acValues, acPrefix + ".valid"))
        return true;

    return HasStrictBool(acValues, acPrefix + ".holdingObject") &&
           HasStrictBool(acValues, acPrefix + ".canGrabObject") &&
           HasStrictBool(acValues, acPrefix + ".handInGrabbableState") &&
           HasStrictBool(acValues, acPrefix + ".disabled") &&
           HasStrictBool(acValues, acPrefix + ".weaponCollisionDisabled") &&
           HasStrictUInt32(acValues, acPrefix + ".grabbedObjectFormId") &&
           FindValue(acValues, acPrefix + ".grabbedNodeName") != nullptr &&
           HasCoherentHiggsFingerState(acValues, acPrefix) &&
           HasCoherentHiggsGrabTransform(acValues, acPrefix);
}

bool HasCoherentHiggsEventState(const KeyValueMap& acValues) noexcept
{
    if (!HasStrictUInt32(acValues, "recentEventCount"))
        return false;

    const auto eventCount = GetUInt32(acValues, "recentEventCount");
    if (eventCount > kMaximumHiggsMutationEvents)
        return false;
    for (std::uint32_t index = 0; index < eventCount; ++index)
    {
        const auto prefix = std::string("recentEvent.") + std::to_string(index);
        const auto* pType = FindValue(acValues, prefix + ".type");
        const auto* pHand = FindValue(acValues, prefix + ".hand");
        if (!HasStrictUInt32(acValues, prefix + ".sequence") || !pType ||
            ParseEventKind(pType) == VRHiggsEventSnapshot::Kind::kUnknown ||
            !HasStrictBool(acValues, prefix + ".hasHand") || !pHand ||
            (*pHand != "left" && *pHand != "right") ||
            !HasStrictUInt32(acValues, prefix + ".formId") ||
            !HasStrictUInt32(acValues, prefix + ".inventoryBaseFormId") ||
            !HasStrictFloat(acValues, prefix + ".mass") ||
            !HasStrictFloat(acValues, prefix + ".separatingVelocity") ||
            !HasCoherentHiggsGrabTransform(acValues, prefix) ||
            !FindValue(acValues, prefix + ".grabbedNodeName"))
            return false;
    }
    return true;
}

bool HasCoherentHiggsBridgeData(const KeyValueMap& acValues) noexcept
{
    if (!HasStrictBool(acValues, "bridge.loaded") || !GetBool(acValues, "bridge.loaded"))
        return false;

    uint64_t bridgeEpoch{};
    uint32_t bridgeSequence{};
    return TryParseUInt64(FindValue(acValues, "bridge.epoch"), bridgeEpoch) && bridgeEpoch != 0 &&
           TryParseUInt32(FindValue(acValues, "bridge.sequence"), bridgeSequence) && bridgeSequence != 0 &&
           HasStrictBool(acValues, "higgs.detected") &&
           HasStrictBool(acValues, "higgs.interfaceAvailable") &&
           HasStrictBool(acValues, "higgs.callbacksRegistered") &&
           HasStrictBool(acValues, "higgs.snapshotAvailable") &&
           HasStrictUInt32(acValues, "higgs.snapshotSequence") &&
           HasStrictBool(acValues, "higgs.twoHanding") && HasCoherentHiggsHandState(acValues, "left") &&
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
    aFile << acPrefix << ".mutationSequence=" << acState.MutationSequence << "\n";
    aFile << acPrefix << ".bridgeLoaded=" << (acState.BridgeLoaded ? "1" : "0") << "\n";
    aFile << acPrefix << ".detected=" << (acState.Detected ? "1" : "0") << "\n";
    aFile << acPrefix << ".interfaceAvailable=" << (acState.InterfaceAvailable ? "1" : "0") << "\n";
    aFile << acPrefix << ".callbacksRegistered=" << (acState.CallbacksRegistered ? "1" : "0") << "\n";
    aFile << acPrefix << ".snapshotAvailable=" << (acState.SnapshotAvailable ? "1" : "0") << "\n";
    aFile << acPrefix << ".snapshotSequence=" << acState.SnapshotSequence << "\n";
    aFile << acPrefix << ".twoHanding=" << (acState.TwoHanding ? "1" : "0") << "\n";
    WriteHandState(aFile, acPrefix + ".left", acState.Left);
    WriteHandState(aFile, acPrefix + ".right", acState.Right);
    const auto eventCount = std::min<std::size_t>(acState.MutationEventCount, acState.MutationEvents.size());
    aFile << acPrefix << ".mutationEventCount=" << eventCount << "\n";
    for (std::size_t index = 0; index < eventCount; ++index)
        WriteEventSnapshot(aFile, acPrefix + ".mutationEvent." + std::to_string(index), acState.MutationEvents[index]);
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

bool VRHiggsService::RefreshLocalHiggsStateForAuthentication() noexcept
{
    VRHiggsState state{};
    if (!CaptureLocalHiggsState(state))
    {
        if (m_hasLocalState)
        {
            m_lastLocalState = {};
            m_hasLocalState = false;
            m_statusDirty = true;
        }
        return false;
    }

    if (!m_hasLocalState || state != m_lastLocalState)
    {
        m_lastLocalState = state;
        m_hasLocalState = true;
        m_statusDirty = true;
    }

    return IsLocalHiggsRelayOperational();
}

void VRHiggsService::ReportHiggsBridgeReadoutRejection(
    const std::uint8_t aRejection, const char* apReason) noexcept
{
    if (m_lastBridgeReadoutRejection == aRejection)
        return;

    m_lastBridgeReadoutRejection = aRejection;
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastBridgeReadoutLogTime < kHiggsBridgeReadoutLogInterval)
        return;

    spdlog::warn("SkyrimTogetherVR rejected HIGGS bridge readout: {}", apReason);
    m_lastBridgeReadoutLogTime = now;
}

void VRHiggsService::ReportHiggsBridgeReadoutAccepted() noexcept
{
    if (m_lastBridgeReadoutRejection == 0)
        return;

    m_lastBridgeReadoutRejection = 0;
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastBridgeReadoutLogTime < kHiggsBridgeReadoutLogInterval)
        return;

    spdlog::info("SkyrimTogetherVR accepted a fresh HIGGS bridge readout for the current launch");
    m_lastBridgeReadoutLogTime = now;
}

void VRHiggsService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const bool online = m_transport.IsOnline();
    if (!online && m_wasOnline)
        ClearLocalStateAtConnectionBoundary();

    m_bridgeReadTimer += acEvent.Delta;
    if (!m_bridgeReadInitialized || m_bridgeReadTimer >= kHiggsBridgeReadInterval)
    {
        m_bridgeReadTimer = 0.0;
        m_bridgeReadInitialized = true;

        TP_UNUSED(RefreshLocalHiggsStateForAuthentication());
    }

    PruneRemoteStates(acEvent.Delta);

    m_sendTimer += acEvent.Delta;
    if (online && m_sendTimer >= kHiggsSendInterval)
    {
        m_sendTimer = 0.0;
        SendHiggsState();
    }

    m_wasOnline = online;

    m_statusTimer += acEvent.Delta;
    if (!m_statusDirty && m_statusTimer < kHiggsStatusWriteInterval)
        return;

    m_statusTimer = 0.0;
    WriteHiggsNetworkStatusFile();
}

void VRHiggsService::OnVRHiggsState(const NotifyVRHiggsState& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId() ||
        !acMessage.State.IsDecodedValid || !acMessage.State.IsMutationReplayValid())
        return;

    const auto existingIt = m_remoteStates.find(acMessage.PlayerId);
    const auto epochIt = m_remoteProducerEpochs.find(acMessage.PlayerId);
    if (existingIt != m_remoteStates.end() && epochIt != m_remoteProducerEpochs.end() &&
        epochIt->second == acMessage.State.ProducerEpoch &&
        !IsNewerSequence(acMessage.State.Sequence, existingIt->second.Sequence))
        return;

    m_remoteStates[acMessage.PlayerId] = acMessage.State;
    m_remoteProducerEpochs[acMessage.PlayerId] = acMessage.State.ProducerEpoch;
    m_remoteStateAges[acMessage.PlayerId] = 0.0;
    m_statusDirty = true;
}

void VRHiggsService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto stateCount = m_remoteStates.erase(acMessage.PlayerId);
    const auto ageCount = m_remoteStateAges.erase(acMessage.PlayerId);
    m_remoteProducerEpochs.erase(acMessage.PlayerId);
    if (stateCount || ageCount)
        m_statusDirty = true;
}

void VRHiggsService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    ClearLocalStateAtConnectionBoundary();

    if (!m_remoteStates.empty() || !m_remoteStateAges.empty())
    {
        m_remoteStates.clear();
        m_remoteStateAges.clear();
        m_remoteProducerEpochs.clear();
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
            m_remoteProducerEpochs.clear();
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
        m_remoteProducerEpochs.erase(playerId);
        m_statusDirty = true;
    }
}

bool VRHiggsService::CaptureLocalHiggsState(VRHiggsState& aState) noexcept
{
    KeyValueMap values;
    if (!ReadKeyValueFile(m_bridgeStatusPath, values))
    {
        ReportHiggsBridgeReadoutRejection(
            static_cast<std::uint8_t>(HiggsBridgeReadoutRejection::Unavailable),
            DescribeHiggsBridgeReadoutRejection(HiggsBridgeReadoutRejection::Unavailable));
        return false;
    }

    const auto identityRejection = ValidateHiggsBridgeIdentity(values, CurrentUnixMilliseconds());
    if (identityRejection != HiggsBridgeReadoutRejection::None)
    {
        ReportHiggsBridgeReadoutRejection(
            static_cast<std::uint8_t>(identityRejection),
            DescribeHiggsBridgeReadoutRejection(identityRejection));
        return false;
    }

    if (!HasCoherentHiggsBridgeData(values))
    {
        ReportHiggsBridgeReadoutRejection(
            static_cast<std::uint8_t>(HiggsBridgeReadoutRejection::MalformedState),
            DescribeHiggsBridgeReadoutRejection(HiggsBridgeReadoutRejection::MalformedState));
        return false;
    }

    const auto bridgeSequence = GetUInt32(values, "bridge.sequence");
    const auto* pBridgeEpoch = FindValue(values, "bridge.epoch");
    const std::string bridgeEpoch = *pBridgeEpoch;

    aState.Sequence = bridgeSequence;
    aState.ProducerEpoch = ParseUInt64(FindValue(values, "bridge.epoch"));
    aState.BridgeLoaded = GetBool(values, "bridge.loaded");
    aState.Detected = GetBool(values, "higgs.detected");
    aState.InterfaceAvailable = GetBool(values, "higgs.interfaceAvailable");
    aState.CallbacksRegistered = GetBool(values, "higgs.callbacksRegistered");
    aState.SnapshotAvailable = GetBool(values, "higgs.snapshotAvailable");
    aState.SnapshotSequence = GetUInt32(values, "higgs.snapshotSequence");
    aState.TwoHanding = GetBool(values, "higgs.twoHanding");
    ParseHandState(m_world, values, "left", aState.Left);
    ParseHandState(m_world, values, "right", aState.Right);
    if (!ParseMutationEvents(m_world, values, aState))
    {
        ReportHiggsBridgeReadoutRejection(
            static_cast<std::uint8_t>(HiggsBridgeReadoutRejection::MalformedState),
            DescribeHiggsBridgeReadoutRejection(HiggsBridgeReadoutRejection::MalformedState));
        return false;
    }

    const bool hasState = aState.BridgeLoaded || aState.Detected || aState.InterfaceAvailable ||
                          aState.CallbacksRegistered || aState.SnapshotAvailable || aState.Left.Valid ||
                          aState.Right.Valid;

    MergeMutationReplayWindow(aState, bridgeEpoch, m_transport.IsOnline());
    ReportHiggsBridgeReadoutAccepted();

    return hasState;
}

void VRHiggsService::MergeMutationReplayWindow(
    VRHiggsState& arState, const std::string& acBridgeEpoch, const bool aOnline) noexcept
{
    const bool epochChanged = !m_bridgeEpoch.empty() && m_bridgeEpoch != acBridgeEpoch;
    bool rebase = epochChanged;
    if (epochChanged)
    {
        m_mutationReplayWindow = {};
        m_mutationReplayEventCount = 0;
        m_lastCapturedMutationSequence = 0;
        m_hasCapturedMutationSequence = false;
        // A bridge epoch is a producer reset, never a sequence continuation.
        m_mutationReplayFloorEpoch.clear();
        m_mutationReplayFloorSequence = 0;
        m_hasMutationReplayFloor = false;
        m_mutationReplayRebasePending = true;
    }
    m_bridgeEpoch = acBridgeEpoch;

    const auto eventCount = std::min<std::size_t>(arState.MutationEventCount, arState.MutationEvents.size());
    if (!aOnline)
    {
        const auto floorSequence = eventCount != 0 ?
            arState.MutationEvents[eventCount - 1].Sequence : 0;
        RebaseMutationReplayFloor(acBridgeEpoch, floorSequence);
        arState.MutationEvents = {};
        arState.MutationEventCount = 0;
        arState.MutationSequence = 0;
        return;
    }

    if (m_hasCapturedMutationSequence) {
        const auto firstNew = std::find_if(arState.MutationEvents.begin(),
                                           arState.MutationEvents.begin() + eventCount,
                                           [this](const VRHiggsEventSnapshot& event) noexcept {
                                               return IsNewerSequence(event.Sequence, m_lastCapturedMutationSequence);
                                           });
        if (firstNew != arState.MutationEvents.begin() + eventCount &&
            firstNew->Sequence != NextNonZeroSequence(m_lastCapturedMutationSequence))
            rebase = true;
    }
    std::uint32_t previousNewSequence = m_lastCapturedMutationSequence;
    bool havePreviousNewSequence = m_hasCapturedMutationSequence;
    for (std::size_t index = 0; index < eventCount; ++index) {
        const auto sequence = arState.MutationEvents[index].Sequence;
        if (!IsNewerSequence(sequence, m_lastCapturedMutationSequence))
            continue;
        if (havePreviousNewSequence && sequence != NextNonZeroSequence(previousNewSequence))
            rebase = true;
        previousNewSequence = sequence;
        havePreviousNewSequence = true;
    }

    if (rebase)
    {
        const auto floorSequence = eventCount != 0 ? arState.MutationEvents[eventCount - 1].Sequence : 0;
        RebaseMutationReplayFloor(acBridgeEpoch, floorSequence);
        arState.MutationEvents = {};
        arState.MutationEventCount = 0;
        arState.MutationSequence = 0;
        m_mutationReplayRebasePending = true;
        arState.MutationReplayRebased = true;
        static std::uint32_t s_rebaseLogCount{};
        if (s_rebaseLogCount != std::numeric_limits<std::uint32_t>::max())
            ++s_rebaseLogCount;
        if (s_rebaseLogCount == 1 || s_rebaseLogCount % 32 == 0)
            spdlog::warn("VR HIGGS replay rebased {} time(s) after producer epoch/window gap", s_rebaseLogCount);
        return;
    }

    if (m_hasMutationReplayFloor && m_mutationReplayFloorEpoch == acBridgeEpoch)
    {
        m_lastCapturedMutationSequence = m_mutationReplayFloorSequence;
        m_hasCapturedMutationSequence = m_mutationReplayFloorSequence != 0;
        m_mutationReplayFloorEpoch.clear();
        m_mutationReplayFloorSequence = 0;
        m_hasMutationReplayFloor = false;
    }
    else if (m_hasMutationReplayFloor)
    {
        m_mutationReplayFloorEpoch.clear();
        m_mutationReplayFloorSequence = 0;
        m_hasMutationReplayFloor = false;
    }

    for (std::size_t index = 0; index < eventCount; ++index)
    {
        const auto& event = arState.MutationEvents[index];
        if (event.Sequence == 0 || (m_hasCapturedMutationSequence &&
                                    !IsNewerSequence(event.Sequence, m_lastCapturedMutationSequence)))
            continue;

        if (m_mutationReplayEventCount == m_mutationReplayWindow.size())
        {
            for (std::size_t replayIndex = 1; replayIndex < m_mutationReplayEventCount; ++replayIndex)
                m_mutationReplayWindow[replayIndex - 1] = m_mutationReplayWindow[replayIndex];
            --m_mutationReplayEventCount;
        }
        m_mutationReplayWindow[m_mutationReplayEventCount++] = event;
        m_lastCapturedMutationSequence = event.Sequence;
        m_hasCapturedMutationSequence = true;
    }

    arState.MutationEvents = m_mutationReplayWindow;
    arState.MutationEventCount = static_cast<uint8_t>(m_mutationReplayEventCount);
    arState.MutationSequence = m_hasCapturedMutationSequence ? m_lastCapturedMutationSequence : 0;

    // A rebase is a baseline-only transaction. Keep emitting its marker until
    // the transport accepts it, never letting newer retained mutations turn
    // that baseline into a partial replay.
    if (m_mutationReplayRebasePending)
    {
        arState.MutationEvents = {};
        arState.MutationEventCount = 0;
        arState.MutationSequence = 0;
        // The relay identifies a rebase by a producer identity change. Keep the
        // authenticated bridge epoch for replay floors, but rotate the wire
        // producer on every history reset so even same-epoch gaps are admitted.
        if (m_networkProducerEpoch == 0 || m_networkProducerEpoch == arState.ProducerEpoch)
        {
            ++m_networkProducerEpoch;
            arState.ProducerEpoch = m_networkProducerEpoch;
        }
        arState.MutationReplayRebased = true;
    }
}

void VRHiggsService::RebaseMutationReplayFloor(
    const std::string& acBridgeEpoch, const uint32_t aMutationSequence) noexcept
{
    m_mutationReplayWindow = {};
    m_mutationReplayEventCount = 0;
    m_mutationReplayFloorEpoch = acBridgeEpoch;
    m_mutationReplayFloorSequence = aMutationSequence;
    m_hasMutationReplayFloor = aMutationSequence != 0;
    m_lastCapturedMutationSequence = aMutationSequence;
    m_hasCapturedMutationSequence = aMutationSequence != 0;
}

void VRHiggsService::ClearLocalStateAtConnectionBoundary() noexcept
{
    const auto floorSequence = m_hasCapturedMutationSequence ?
        m_lastCapturedMutationSequence : m_lastLocalState.MutationSequence;
    RebaseMutationReplayFloor(m_bridgeEpoch, floorSequence);
    m_lastLocalState = {};
    m_hasLocalState = false;
    m_sendTimer = 0.0;
    m_statusDirty = true;
    m_wasOnline = false;
}

void VRHiggsService::SendHiggsState() noexcept
{
    if (!m_transport.IsOnline() || !IsLocalHiggsRelayOperational() ||
        !SkyrimTogether::Protocol::HasCapability(
            m_transport.GetNegotiatedGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::VRHiggsRelay))
        return;

    RequestVRHiggsState request{};
    request.State = m_lastLocalState;
    if (m_transport.Send(request) && request.State.MutationReplayRebased)
    {
        m_mutationReplayRebasePending = false;
        m_lastLocalState.MutationReplayRebased = false;
    }
}

void VRHiggsService::WriteHiggsNetworkStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    std::ofstream file(m_networkStatusPath, std::ios::trunc);
    if (!file)
        return;

    file << "ready=" << (IsLocalHiggsRelayOperational() ? "1" : "0") << "\n";
    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localHiggsAvailable=" << (m_hasLocalState ? "1" : "0") << "\n";
    file << "localHiggsRelayOperational=" << (IsLocalHiggsRelayOperational() ? "1" : "0") << "\n";
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
