#include <TiltedOnlinePCH.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyVRPoseUpdate.h>
#include <Messages/RequestVRPoseUpdate.h>
#include <Services/TransportService.h>
#include <Services/VRPoseService.h>
#include <VR/VRBodyPoseCapture.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cwctype>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr double kPoseSendInterval = 1.0 / 20.0;
constexpr double kVrikBridgeReadInterval = 1.0 / 20.0;
constexpr double kPoseStatusWriteInterval = 0.25;
constexpr double kRemotePoseStaleSeconds = 3.0;
constexpr double kVrikBridgeStaleSeconds = 1.0;
constexpr char kPoseStatusFileName[] = "SkyrimTogetherVR.pose";
constexpr char kVrikBridgeFileName[] = "SkyrimTogetherVR.vrik";

std::filesystem::path GetHandoffDirectory()
{
    return SkyrimTogetherVR::Handoff::GetDirectory();
}

const VRPoseTransform* FirstValidHand(const VRPlayerPoseSnapshot& aSnapshot) noexcept
{
    if (aSnapshot.RightHand.Valid)
        return &aSnapshot.RightHand;
    if (aSnapshot.LeftHand.Valid)
        return &aSnapshot.LeftHand;
    return nullptr;
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

bool HasPluginFile(const std::filesystem::path& acPluginPath, std::initializer_list<const wchar_t*> aPluginNames) noexcept
{
    std::error_code ec;
    if (!std::filesystem::is_directory(acPluginPath, ec))
        return false;

    std::filesystem::directory_iterator it(acPluginPath, ec);
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

bool IsVrikInstalled()
{
    static const bool installed = []()
    {
        const auto dataPath = GetHandoffDirectory().parent_path();
        const auto pluginPath = dataPath / "SKSE" / "Plugins";
        return HasPluginFile(pluginPath, {L"vrik.dll", L"VRIK.dll"});
    }();

    return installed;
}

std::filesystem::path GetVrikBridgePath()
{
    return GetHandoffDirectory() / kVrikBridgeFileName;
}

bool ParseBool(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apKey, bool aDefault = false)
{
    const auto it = acValues.find(apKey);
    if (it == acValues.end())
        return aDefault;

    return it->second == "1" || it->second == "true" || it->second == "yes";
}

bool HasKey(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apKey) noexcept
{
    return acValues.find(apKey) != acValues.end();
}

void InvalidateVrikPayload(VRVrikData& aVrik) noexcept
{
    aVrik.LeftFingers = {};
    aVrik.RightFingers = {};
    aVrik.CameraOffsetsValid = false;
    aVrik.CameraOffset = {};
    aVrik.FinalCameraOffset = {};
    aVrik.FinalSmoothingOffset = {};
}

uint32_t ParseUInt32(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apKey, uint32_t aDefault = 0) noexcept
{
    const auto it = acValues.find(apKey);
    if (it == acValues.end())
        return aDefault;

    char* pEnd = nullptr;
    const auto value = std::strtoul(it->second.c_str(), &pEnd, 0);
    return pEnd != it->second.c_str() ? static_cast<uint32_t>(value & 0xFFFFFFFFu) : aDefault;
}

std::string ParseString(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apKey, const char* apDefault = "") noexcept
{
    const auto it = acValues.find(apKey);
    return it != acValues.end() ? it->second : apDefault;
}

float ParseFloat(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apKey, float aDefault = 0.0f)
{
    const auto it = acValues.find(apKey);
    if (it == acValues.end())
        return aDefault;

    char* pEnd = nullptr;
    const auto value = std::strtof(it->second.c_str(), &pEnd);
    return pEnd != it->second.c_str() ? value : aDefault;
}

glm::vec3 ParseVec3(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apKey)
{
    const auto it = acValues.find(apKey);
    if (it == acValues.end())
        return {};

    std::istringstream stream(it->second);
    std::string token;
    glm::vec3 result{};
    if (std::getline(stream, token, ','))
        result.x = std::strtof(token.c_str(), nullptr);
    if (std::getline(stream, token, ','))
        result.y = std::strtof(token.c_str(), nullptr);
    if (std::getline(stream, token, ','))
        result.z = std::strtof(token.c_str(), nullptr);
    return result;
}

TiltedPhoques::Map<std::string, std::string> ReadKeyValueFile(const std::filesystem::path& acPath)
{
    TiltedPhoques::Map<std::string, std::string> result;
    std::ifstream file(acPath);
    if (!file)
        return result;

    std::string line;
    while (std::getline(file, line))
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;

        result[line.substr(0, separator)] = line.substr(separator + 1);
    }

    return result;
}

VRFingerCurlData BuildFingerData(const TiltedPhoques::Map<std::string, std::string>& acValues, const char* apPrefix)
{
    VRFingerCurlData result{};
    const std::string prefix(apPrefix);
    result.Valid = ParseBool(acValues, (prefix + ".valid").c_str());
    if (!result.Valid)
        return result;

    result.Thumb = ParseFloat(acValues, (prefix + ".thumb").c_str());
    result.Index = ParseFloat(acValues, (prefix + ".index").c_str());
    result.Middle = ParseFloat(acValues, (prefix + ".middle").c_str());
    result.Ring = ParseFloat(acValues, (prefix + ".ring").c_str());
    result.Pinky = ParseFloat(acValues, (prefix + ".pinky").c_str());
    return result;
}

VRPoseNodeData ToPoseNodeData(const VRPoseTransform& acTransform) noexcept
{
    VRPoseNodeData result{};
    result.Valid = acTransform.Valid;
    if (!result.Valid)
        return result;

    result.Position = acTransform.Position;
    result.AxisX = acTransform.AxisX;
    result.AxisY = acTransform.AxisY;
    result.AxisZ = acTransform.AxisZ;
    result.Scale = acTransform.Scale;
    return result;
}

VRPoseUpdate ToPoseUpdate(
    const VRPlayerPoseSnapshot& acSnapshot,
    const VRBodyPoseData& acBody,
    const VRVrikData& acVrik,
    uint32_t aSequence) noexcept
{
    VRPoseUpdate result{};
    result.Sequence = aSequence;
    result.Hmd = ToPoseNodeData(acSnapshot.Hmd);
    result.LeftHand = ToPoseNodeData(acSnapshot.LeftHand);
    result.RightHand = ToPoseNodeData(acSnapshot.RightHand);
    result.SpellOrigin = ToPoseNodeData(acSnapshot.SpellOrigin);
    result.SpellDestination = ToPoseNodeData(acSnapshot.SpellDestination);
    result.ArrowOrigin = ToPoseNodeData(acSnapshot.ArrowOrigin);
    result.ArrowDestination = ToPoseNodeData(acSnapshot.ArrowDestination);
    result.BowAim = ToPoseNodeData(acSnapshot.BowAim);
    result.BowRotation = ToPoseNodeData(acSnapshot.BowRotation);
    result.LeftWeaponOffset = ToPoseNodeData(acSnapshot.LeftWeaponOffset);
    result.RightWeaponOffset = ToPoseNodeData(acSnapshot.RightWeaponOffset);
    result.PrimaryMagicOffset = ToPoseNodeData(acSnapshot.PrimaryMagicOffset);
    result.PrimaryMagicAim = ToPoseNodeData(acSnapshot.PrimaryMagicAim);
    result.SecondaryMagicOffset = ToPoseNodeData(acSnapshot.SecondaryMagicOffset);
    result.SecondaryMagicAim = ToPoseNodeData(acSnapshot.SecondaryMagicAim);
    result.Body = acBody;
    result.Vrik = acVrik;
    return result;
}

void WriteNode(std::ofstream& aFile, const char* apPrefix, const VRPoseNodeData& acNode)
{
    aFile << apPrefix << ".valid=" << (acNode.Valid ? "1" : "0") << "\n";
    if (!acNode.Valid)
        return;

    aFile << apPrefix << ".position=" << acNode.Position.x << "," << acNode.Position.y << "," << acNode.Position.z << "\n";
    aFile << apPrefix << ".axisX=" << acNode.AxisX.x << "," << acNode.AxisX.y << "," << acNode.AxisX.z << "\n";
    aFile << apPrefix << ".axisY=" << acNode.AxisY.x << "," << acNode.AxisY.y << "," << acNode.AxisY.z << "\n";
    aFile << apPrefix << ".axisZ=" << acNode.AxisZ.x << "," << acNode.AxisZ.y << "," << acNode.AxisZ.z << "\n";
    aFile << apPrefix << ".scale=" << acNode.Scale << "\n";
}

void WriteSnapshotNode(std::ofstream& aFile, const char* apPrefix, const VRPoseTransform& acNode)
{
    aFile << apPrefix << ".valid=" << (acNode.Valid ? "1" : "0") << "\n";
    if (!acNode.Valid)
        return;

    aFile << apPrefix << ".position=" << acNode.Position.x << "," << acNode.Position.y << "," << acNode.Position.z << "\n";
    aFile << apPrefix << ".axisX=" << acNode.AxisX.x << "," << acNode.AxisX.y << "," << acNode.AxisX.z << "\n";
    aFile << apPrefix << ".axisY=" << acNode.AxisY.x << "," << acNode.AxisY.y << "," << acNode.AxisY.z << "\n";
    aFile << apPrefix << ".axisZ=" << acNode.AxisZ.x << "," << acNode.AxisZ.y << "," << acNode.AxisZ.z << "\n";
    aFile << apPrefix << ".scale=" << acNode.Scale << "\n";
}

void WriteFingerCurl(std::ofstream& aFile, const char* apPrefix, const VRFingerCurlData& acFingers)
{
    aFile << apPrefix << ".valid=" << (acFingers.Valid ? "1" : "0") << "\n";
    if (!acFingers.Valid)
        return;

    aFile << apPrefix << ".thumb=" << acFingers.Thumb << "\n";
    aFile << apPrefix << ".index=" << acFingers.Index << "\n";
    aFile << apPrefix << ".middle=" << acFingers.Middle << "\n";
    aFile << apPrefix << ".ring=" << acFingers.Ring << "\n";
    aFile << apPrefix << ".pinky=" << acFingers.Pinky << "\n";
}

void WriteVrikData(std::ofstream& aFile, const char* apPrefix, const VRVrikData& acVrik)
{
    aFile << apPrefix << ".detected=" << (acVrik.Detected ? "1" : "0") << "\n";
    aFile << apPrefix << ".interfaceAvailable=" << (acVrik.InterfaceAvailable ? "1" : "0") << "\n";
    WriteFingerCurl(aFile, (std::string(apPrefix) + ".leftFingers").c_str(), acVrik.LeftFingers);
    WriteFingerCurl(aFile, (std::string(apPrefix) + ".rightFingers").c_str(), acVrik.RightFingers);
    aFile << apPrefix << ".cameraOffsetsValid=" << (acVrik.CameraOffsetsValid ? "1" : "0") << "\n";
    if (!acVrik.CameraOffsetsValid)
        return;

    aFile << apPrefix << ".cameraOffset=" << acVrik.CameraOffset.x << "," << acVrik.CameraOffset.y << "," << acVrik.CameraOffset.z << "\n";
    aFile << apPrefix << ".finalCameraOffset=" << acVrik.FinalCameraOffset.x << "," << acVrik.FinalCameraOffset.y << "," << acVrik.FinalCameraOffset.z << "\n";
    aFile << apPrefix << ".finalSmoothingOffset=" << acVrik.FinalSmoothingOffset.x << "," << acVrik.FinalSmoothingOffset.y << "," << acVrik.FinalSmoothingOffset.z << "\n";
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasCoherentVrikBridgeData(const TiltedPhoques::Map<std::string, std::string>& acValues) noexcept
{
    if (!ParseBool(acValues, "bridge.loaded"))
        return false;

    if (!HasKey(acValues, "bridge.sequence") || !HasKey(acValues, "vrik.detected") ||
        !HasKey(acValues, "vrik.interfaceAvailable") || !HasKey(acValues, "bridge.epoch") ||
        !HasKey(acValues, "vrik.snapshotAvailable") || !HasKey(acValues, "vrik.snapshotSequence") ||
        !HasKey(acValues, "leftFingers.valid") ||
        !HasKey(acValues, "rightFingers.valid") || !HasKey(acValues, "cameraOffsetsValid"))
        return false;

    if (!ParseBool(acValues, "cameraOffsetsValid"))
        return true;

    return HasKey(acValues, "cameraOffset") && HasKey(acValues, "finalCameraOffset") &&
           HasKey(acValues, "finalSmoothingOffset");
}

VRVrikData BuildLocalVrikData() noexcept
{
    using Clock = std::chrono::steady_clock;

    static VRVrikData s_cachedBridgeData{};
    static uint32_t s_cachedVrikSnapshotSequence = 0;
    static std::string s_cachedBridgeEpoch;
    static Clock::time_point s_cachedBridgeTime{};
    static bool s_hasCachedBridgeData = false;

    const auto buildBaseResult = []() noexcept
    {
        VRVrikData result{};
        result.Detected = IsVrikInstalled();
        return result;
    };

    const auto values = ReadKeyValueFile(GetVrikBridgePath());

    const auto applyCapabilityState = [&values, &buildBaseResult]() noexcept
    {
        auto result = buildBaseResult();
        result.Detected = result.Detected || ParseBool(values, "vrik.detected");
        result.InterfaceAvailable = ParseBool(values, "vrik.interfaceAvailable");
        return result;
    };

    const auto now = Clock::now();
    const auto cachedFresh =
        s_hasCachedBridgeData &&
        std::chrono::duration<double>(now - s_cachedBridgeTime).count() <= kVrikBridgeStaleSeconds;

    if (values.empty() || !HasCoherentVrikBridgeData(values))
    {
        auto result = cachedFresh ? s_cachedBridgeData : buildBaseResult();

        if (cachedFresh)
        {
            const auto capabilityState = applyCapabilityState();
            result.Detected = capabilityState.Detected;
            result.InterfaceAvailable = capabilityState.InterfaceAvailable;
        }
        else
        {
            result = applyCapabilityState();
        }

        InvalidateVrikPayload(result);
        return result;
    }

    const auto bridgeSequence = ParseUInt32(values, "bridge.sequence");
    const auto bridgeEpoch = ParseString(values, "bridge.epoch", "legacy");
    if (bridgeSequence == 0)
    {
        auto result = applyCapabilityState();
        InvalidateVrikPayload(result);
        return result;
    }

    const auto vrikSnapshotAvailable = ParseBool(values, "vrik.snapshotAvailable");
    const auto vrikSnapshotSequence = ParseUInt32(values, "vrik.snapshotSequence");

    const bool sameBridgeEpoch = s_hasCachedBridgeData && bridgeEpoch == s_cachedBridgeEpoch;
    const bool hasNewSnapshot = vrikSnapshotAvailable &&
        (!sameBridgeEpoch ? vrikSnapshotSequence > 0 : IsNewerSequence(vrikSnapshotSequence, s_cachedVrikSnapshotSequence));

    auto result = applyCapabilityState();

    if (!hasNewSnapshot)
    {
        InvalidateVrikPayload(result);
    }
    else
    {
        result.LeftFingers = BuildFingerData(values, "leftFingers");
        result.RightFingers = BuildFingerData(values, "rightFingers");
        result.CameraOffsetsValid = ParseBool(values, "cameraOffsetsValid");
        if (result.CameraOffsetsValid)
        {
            result.CameraOffset = ParseVec3(values, "cameraOffset");
            result.FinalCameraOffset = ParseVec3(values, "finalCameraOffset");
            result.FinalSmoothingOffset = ParseVec3(values, "finalSmoothingOffset");
        }
    }

    s_cachedBridgeData = result;
    s_cachedVrikSnapshotSequence = hasNewSnapshot ? vrikSnapshotSequence : (sameBridgeEpoch ? s_cachedVrikSnapshotSequence : 0u);
    s_cachedBridgeEpoch = bridgeEpoch;
    s_cachedBridgeTime = now;
    s_hasCachedBridgeData = true;

    return result;
}
}

VRPoseService::VRPoseService(entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_transport(aTransport)
    , m_handoffDir(GetHandoffDirectory())
    , m_poseStatusPath(m_handoffDir / kPoseStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    spdlog::info("SkyrimTogetherVR pose handoff status file: {}", m_poseStatusPath.string());

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRPoseService::OnUpdate>(this);
    m_vrPoseUpdateConnection = aDispatcher.sink<NotifyVRPoseUpdate>().connect<&VRPoseService::OnVRPoseUpdate>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&VRPoseService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRPoseService::OnDisconnected>(this);
}

void VRPoseService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    m_localBodyPose = SkyrimTogetherVR::BodyPoseCapture::CopyLatestFresh();

    m_vrikBridgeReadTimer += acEvent.Delta;
    if (!m_vrikBridgeInitialized || m_vrikBridgeReadTimer >= kVrikBridgeReadInterval)
    {
        m_vrikBridgeReadTimer = 0.0;
        m_vrikBridgeInitialized = true;
        m_localVrik = BuildLocalVrikData();
    }

    VRPlayerPoseSnapshot snapshot{};
    if (SkyrimVR::CaptureLocalPlayerPoseSnapshot(snapshot))
    {
        m_lastSnapshot = snapshot;
        m_hasSnapshot = true;
    }

    PruneRemotePoses(acEvent.Delta);

    m_sendTimer += acEvent.Delta;
    if (m_sendTimer >= kPoseSendInterval)
    {
        m_sendTimer = 0.0;
        SendPoseUpdate();
    }

    m_poseStatusTimer += acEvent.Delta;
    if (m_poseStatusTimer >= kPoseStatusWriteInterval)
    {
        m_poseStatusTimer = 0.0;
        WritePoseStatusFile();
    }

    m_logTimer += acEvent.Delta;
    if (m_logTimer < 1.0)
        return;

    m_logTimer = 0.0;
    LogSnapshot();
}

void VRPoseService::OnVRPoseUpdate(const NotifyVRPoseUpdate& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;
    if (acMessage.Pose.Sequence == 0 || !HasAnyVRPosePayload(acMessage.Pose) || !IsVRPoseUpdateSafe(acMessage.Pose))
        return;

    const auto existingIt = m_remotePoses.find(acMessage.PlayerId);
    if (existingIt != m_remotePoses.end() && !IsNewerSequence(acMessage.Pose.Sequence, existingIt->second.Sequence))
        return;

    m_remotePoses[acMessage.PlayerId] = acMessage.Pose;
    m_remotePoseAges[acMessage.PlayerId] = 0.0;
    m_poseStatusDirty = true;
}

void VRPoseService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    const auto poseCount = m_remotePoses.erase(acMessage.PlayerId);
    const auto ageCount = m_remotePoseAges.erase(acMessage.PlayerId);
    if (poseCount || ageCount)
        m_poseStatusDirty = true;
}

void VRPoseService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    TP_UNUSED(acEvent);

    if (!m_remotePoses.empty() || !m_remotePoseAges.empty())
    {
        m_remotePoses.clear();
        m_remotePoseAges.clear();
        m_poseStatusDirty = true;
    }
}

void VRPoseService::SendPoseUpdate() noexcept
{
    if (!m_transport.IsOnline() || !m_hasSnapshot)
        return;

    RequestVRPoseUpdate request{};
    request.Pose = ToPoseUpdate(m_lastSnapshot, m_localBodyPose, m_localVrik, ++m_sequence);
    m_transport.Send(request);
}

void VRPoseService::PruneRemotePoses(double aDelta) noexcept
{
    if (!m_transport.IsOnline())
    {
        if (!m_remotePoses.empty() || !m_remotePoseAges.empty())
        {
            m_remotePoses.clear();
            m_remotePoseAges.clear();
            m_poseStatusDirty = true;
        }
        return;
    }

    std::vector<uint32_t> trackedPlayerIds;
    std::vector<uint32_t> expiredPlayerIds;
    for (const auto& [playerId, age] : m_remotePoseAges)
    {
        trackedPlayerIds.push_back(playerId);
        if (age + aDelta >= kRemotePoseStaleSeconds)
            expiredPlayerIds.push_back(playerId);
    }
    for (auto playerId : trackedPlayerIds)
        m_remotePoseAges[playerId] += aDelta;

    for (auto playerId : expiredPlayerIds)
    {
        m_remotePoseAges.erase(playerId);
        m_remotePoses.erase(playerId);
        m_poseStatusDirty = true;
    }
}

void VRPoseService::WritePoseStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_handoffDir, ec);

    const auto tempPath = m_poseStatusPath.string() + ".tmp";
    std::ofstream file(tempPath, std::ios::trunc);
    if (!file)
        return;

    file << "online=" << (m_transport.IsOnline() ? "1" : "0") << "\n";
    file << "localPlayerId=" << m_transport.GetLocalPlayerId() << "\n";
    file << "localPoseAvailable=" << (m_hasSnapshot ? "1" : "0") << "\n";
    file << "localSequence=" << m_sequence << "\n";
    file << "remotePoseCount=" << m_remotePoses.size() << "\n";
    file << "local.body.formatVersion=" << m_localBodyPose.FormatVersion << "\n";
    file << "local.body.valid=" << (m_localBodyPose.Valid ? "1" : "0") << "\n";
    file << "local.body.captureSequence=" << m_localBodyPose.CaptureSequence << "\n";
    file << "local.body.rootGeneration=" << m_localBodyPose.RootGeneration << "\n";
    WriteNode(file, "local.body.pelvis", m_localBodyPose.Pelvis);
    WriteNode(file, "local.body.leftThigh", m_localBodyPose.LeftThigh);
    WriteNode(file, "local.body.leftCalf", m_localBodyPose.LeftCalf);
    WriteNode(file, "local.body.leftFoot", m_localBodyPose.LeftFoot);
    WriteNode(file, "local.body.rightThigh", m_localBodyPose.RightThigh);
    WriteNode(file, "local.body.rightCalf", m_localBodyPose.RightCalf);
    WriteNode(file, "local.body.rightFoot", m_localBodyPose.RightFoot);
    WriteVrikData(file, "local.vrik", m_localVrik);

    if (m_hasSnapshot)
    {
        WriteSnapshotNode(file, "local.hmd", m_lastSnapshot.Hmd);
        WriteSnapshotNode(file, "local.leftHand", m_lastSnapshot.LeftHand);
        WriteSnapshotNode(file, "local.rightHand", m_lastSnapshot.RightHand);
        WriteSnapshotNode(file, "local.spellOrigin", m_lastSnapshot.SpellOrigin);
        WriteSnapshotNode(file, "local.spellDestination", m_lastSnapshot.SpellDestination);
        WriteSnapshotNode(file, "local.arrowOrigin", m_lastSnapshot.ArrowOrigin);
        WriteSnapshotNode(file, "local.arrowDestination", m_lastSnapshot.ArrowDestination);
        WriteSnapshotNode(file, "local.bowAim", m_lastSnapshot.BowAim);
        WriteSnapshotNode(file, "local.bowRotation", m_lastSnapshot.BowRotation);
        WriteSnapshotNode(file, "local.leftWeaponOffset", m_lastSnapshot.LeftWeaponOffset);
        WriteSnapshotNode(file, "local.rightWeaponOffset", m_lastSnapshot.RightWeaponOffset);
        WriteSnapshotNode(file, "local.primaryMagicOffset", m_lastSnapshot.PrimaryMagicOffset);
        WriteSnapshotNode(file, "local.primaryMagicAim", m_lastSnapshot.PrimaryMagicAim);
        WriteSnapshotNode(file, "local.secondaryMagicOffset", m_lastSnapshot.SecondaryMagicOffset);
        WriteSnapshotNode(file, "local.secondaryMagicAim", m_lastSnapshot.SecondaryMagicAim);
    }

    for (const auto& [playerId, pose] : m_remotePoses)
    {
        const auto ageIt = m_remotePoseAges.find(playerId);
        const auto age = ageIt != m_remotePoseAges.end() ? ageIt->second : 0.0;

        file << "remote." << playerId << ".sequence=" << pose.Sequence << "\n";
        file << "remote." << playerId << ".ageSeconds=" << age << "\n";

        const auto prefix = std::string("remote.") + std::to_string(playerId);
        WriteNode(file, (prefix + ".hmd").c_str(), pose.Hmd);
        WriteNode(file, (prefix + ".leftHand").c_str(), pose.LeftHand);
        WriteNode(file, (prefix + ".rightHand").c_str(), pose.RightHand);
        WriteNode(file, (prefix + ".spellOrigin").c_str(), pose.SpellOrigin);
        WriteNode(file, (prefix + ".spellDestination").c_str(), pose.SpellDestination);
        WriteNode(file, (prefix + ".arrowOrigin").c_str(), pose.ArrowOrigin);
        WriteNode(file, (prefix + ".arrowDestination").c_str(), pose.ArrowDestination);
        WriteNode(file, (prefix + ".bowAim").c_str(), pose.BowAim);
        WriteNode(file, (prefix + ".bowRotation").c_str(), pose.BowRotation);
        WriteNode(file, (prefix + ".leftWeaponOffset").c_str(), pose.LeftWeaponOffset);
        WriteNode(file, (prefix + ".rightWeaponOffset").c_str(), pose.RightWeaponOffset);
        WriteNode(file, (prefix + ".primaryMagicOffset").c_str(), pose.PrimaryMagicOffset);
        WriteNode(file, (prefix + ".primaryMagicAim").c_str(), pose.PrimaryMagicAim);
        WriteNode(file, (prefix + ".secondaryMagicOffset").c_str(), pose.SecondaryMagicOffset);
        WriteNode(file, (prefix + ".secondaryMagicAim").c_str(), pose.SecondaryMagicAim);
        file << prefix << ".body.formatVersion=" << pose.Body.FormatVersion << "\n";
        file << prefix << ".body.valid=" << (pose.Body.Valid ? "1" : "0") << "\n";
        file << prefix << ".body.captureSequence=" << pose.Body.CaptureSequence << "\n";
        file << prefix << ".body.rootGeneration=" << pose.Body.RootGeneration << "\n";
        WriteNode(file, (prefix + ".body.pelvis").c_str(), pose.Body.Pelvis);
        WriteNode(file, (prefix + ".body.leftThigh").c_str(), pose.Body.LeftThigh);
        WriteNode(file, (prefix + ".body.leftCalf").c_str(), pose.Body.LeftCalf);
        WriteNode(file, (prefix + ".body.leftFoot").c_str(), pose.Body.LeftFoot);
        WriteNode(file, (prefix + ".body.rightThigh").c_str(), pose.Body.RightThigh);
        WriteNode(file, (prefix + ".body.rightCalf").c_str(), pose.Body.RightCalf);
        WriteNode(file, (prefix + ".body.rightFoot").c_str(), pose.Body.RightFoot);
        WriteVrikData(file, (prefix + ".vrik").c_str(), pose.Vrik);
    }

    file.close();
    std::filesystem::rename(tempPath, m_poseStatusPath, ec);
    if (ec)
    {
        std::filesystem::remove(m_poseStatusPath, ec);
        std::filesystem::rename(tempPath, m_poseStatusPath, ec);
    }

    if (!ec)
        m_poseStatusDirty = false;
}

void VRPoseService::LogSnapshot() const noexcept
{
    if (!m_hasSnapshot)
    {
        spdlog::info("SkyrimTogetherVR pose snapshot: unavailable");
        return;
    }

    const auto* pHand = FirstValidHand(m_lastSnapshot);
    if (!pHand)
    {
        spdlog::info(
            "SkyrimTogetherVR pose snapshot: hmd=({}, {}, {}), hand=unavailable",
            m_lastSnapshot.Hmd.Position.x,
            m_lastSnapshot.Hmd.Position.y,
            m_lastSnapshot.Hmd.Position.z);
        return;
    }

    spdlog::info(
        "SkyrimTogetherVR pose snapshot: hmd=({}, {}, {}), hand=({}, {}, {}), bodyValid={}, spellOriginValid={}, arrowOriginValid={}, weaponOffsetValid={}, remotePoseCount={}",
        m_lastSnapshot.Hmd.Position.x,
        m_lastSnapshot.Hmd.Position.y,
        m_lastSnapshot.Hmd.Position.z,
        pHand->Position.x,
        pHand->Position.y,
        pHand->Position.z,
        m_localBodyPose.Valid,
        m_lastSnapshot.SpellOrigin.Valid,
        m_lastSnapshot.ArrowOrigin.Valid,
        m_lastSnapshot.LeftWeaponOffset.Valid || m_lastSnapshot.RightWeaponOffset.Valid,
        m_remotePoses.size());
}
