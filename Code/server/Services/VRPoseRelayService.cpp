#include <cmath>
#include <Services/VRPoseRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVRPoseUpdate.h>
#include <Messages/RequestVRPoseUpdate.h>
#include <Structs/VRPoseUpdate.h>

namespace
{
constexpr uint64_t kMinPoseRelayIntervalMs = 45;
// Broad but bounded sanity checks for Skyrim VR transforms to reject corrupt or hostile packets.
constexpr float kMaxPosePositionComponent = 1000000.0f;
constexpr float kMaxPoseBasisLength = 4.0f;
constexpr float kMinPoseBasisLength = 0.2f;
constexpr float kMaxPoseBasisDot = 0.35f;
constexpr float kMinPoseScale = 0.01f;
constexpr float kMaxPoseScale = 100.0f;
constexpr float kMaxVrikOffsetComponent = 1000.0f;
constexpr float kMaxFingerCurl = 1.0f;

bool IsFiniteFloat(float aValue) noexcept
{
    return std::isfinite(aValue);
}

bool IsFiniteVector(const glm::vec3& aValue) noexcept
{
    return IsFiniteFloat(aValue.x) && IsFiniteFloat(aValue.y) && IsFiniteFloat(aValue.z);
}

bool IsBoundedVector(const glm::vec3& aValue, float aMaxAbsComponent) noexcept
{
    return std::abs(aValue.x) <= aMaxAbsComponent && std::abs(aValue.y) <= aMaxAbsComponent && std::abs(aValue.z) <= aMaxAbsComponent;
}

bool IsPoseNodeSafe(const VRPoseNodeData& acPose) noexcept
{
    if (!acPose.Valid)
        return true;

    if (!IsFiniteVector(acPose.Position) || !IsFiniteVector(acPose.AxisX) || !IsFiniteVector(acPose.AxisY) || !IsFiniteVector(acPose.AxisZ))
        return false;

    if (!IsBoundedVector(acPose.Position, kMaxPosePositionComponent))
        return false;

    if (!IsBoundedVector(acPose.AxisX, kMaxPoseBasisLength) || !IsBoundedVector(acPose.AxisY, kMaxPoseBasisLength) ||
        !IsBoundedVector(acPose.AxisZ, kMaxPoseBasisLength))
        return false;

    const auto axisXLengthSq = acPose.AxisX.x * acPose.AxisX.x + acPose.AxisX.y * acPose.AxisX.y + acPose.AxisX.z * acPose.AxisX.z;
    const auto axisYLengthSq = acPose.AxisY.x * acPose.AxisY.x + acPose.AxisY.y * acPose.AxisY.y + acPose.AxisY.z * acPose.AxisY.z;
    const auto axisZLengthSq = acPose.AxisZ.x * acPose.AxisZ.x + acPose.AxisZ.y * acPose.AxisZ.y + acPose.AxisZ.z * acPose.AxisZ.z;
    const auto minAxisLengthSq = kMinPoseBasisLength * kMinPoseBasisLength;
    const auto maxAxisLengthSq = kMaxPoseBasisLength * kMaxPoseBasisLength;

    if (axisXLengthSq < minAxisLengthSq || axisXLengthSq > maxAxisLengthSq)
        return false;
    if (axisYLengthSq < minAxisLengthSq || axisYLengthSq > maxAxisLengthSq)
        return false;
    if (axisZLengthSq < minAxisLengthSq || axisZLengthSq > maxAxisLengthSq)
        return false;

    const auto axisXYDot = acPose.AxisX.x * acPose.AxisY.x + acPose.AxisX.y * acPose.AxisY.y + acPose.AxisX.z * acPose.AxisY.z;
    const auto axisXZDot = acPose.AxisX.x * acPose.AxisZ.x + acPose.AxisX.y * acPose.AxisZ.y + acPose.AxisX.z * acPose.AxisZ.z;
    const auto axisYZDot = acPose.AxisY.x * acPose.AxisZ.x + acPose.AxisY.y * acPose.AxisZ.y + acPose.AxisY.z * acPose.AxisZ.z;
    if (std::abs(axisXYDot) > kMaxPoseBasisDot || std::abs(axisXZDot) > kMaxPoseBasisDot ||
        std::abs(axisYZDot) > kMaxPoseBasisDot)
        return false;

    return IsFiniteFloat(acPose.Scale) && acPose.Scale >= kMinPoseScale && acPose.Scale <= kMaxPoseScale;
}

bool IsSafeFingerCurlData(const VRFingerCurlData& acFingers) noexcept
{
    if (!acFingers.Valid)
        return true;

    return IsFiniteFloat(acFingers.Thumb) && IsFiniteFloat(acFingers.Index) && IsFiniteFloat(acFingers.Middle) &&
           IsFiniteFloat(acFingers.Ring) && IsFiniteFloat(acFingers.Pinky) &&
           acFingers.Thumb >= 0.0f && acFingers.Thumb <= kMaxFingerCurl &&
           acFingers.Index >= 0.0f && acFingers.Index <= kMaxFingerCurl &&
           acFingers.Middle >= 0.0f && acFingers.Middle <= kMaxFingerCurl &&
           acFingers.Ring >= 0.0f && acFingers.Ring <= kMaxFingerCurl &&
           acFingers.Pinky >= 0.0f && acFingers.Pinky <= kMaxFingerCurl;
}

bool IsSafeVrikCameraOffsets(const VRVrikData& acVrik) noexcept
{
    if (!acVrik.CameraOffsetsValid)
        return true;

    return IsFiniteVector(acVrik.CameraOffset) && IsFiniteVector(acVrik.FinalCameraOffset) && IsFiniteVector(acVrik.FinalSmoothingOffset) &&
           IsBoundedVector(acVrik.CameraOffset, kMaxVrikOffsetComponent) &&
           IsBoundedVector(acVrik.FinalCameraOffset, kMaxVrikOffsetComponent) &&
           IsBoundedVector(acVrik.FinalSmoothingOffset, kMaxVrikOffsetComponent);
}

bool IsPoseUpdateSafe(const VRPoseUpdate& acPose) noexcept
{
    if (!IsPoseNodeSafe(acPose.Hmd) || !IsPoseNodeSafe(acPose.LeftHand) || !IsPoseNodeSafe(acPose.RightHand) ||
        !IsPoseNodeSafe(acPose.SpellOrigin) || !IsPoseNodeSafe(acPose.SpellDestination) || !IsPoseNodeSafe(acPose.ArrowOrigin) ||
        !IsPoseNodeSafe(acPose.ArrowDestination) || !IsPoseNodeSafe(acPose.BowAim) || !IsPoseNodeSafe(acPose.BowRotation) ||
        !IsPoseNodeSafe(acPose.LeftWeaponOffset) || !IsPoseNodeSafe(acPose.RightWeaponOffset) || !IsPoseNodeSafe(acPose.PrimaryMagicOffset) ||
        !IsPoseNodeSafe(acPose.PrimaryMagicAim) || !IsPoseNodeSafe(acPose.SecondaryMagicOffset) || !IsPoseNodeSafe(acPose.SecondaryMagicAim))
        return false;

    if (!IsSafeFingerCurlData(acPose.Vrik.LeftFingers) || !IsSafeFingerCurlData(acPose.Vrik.RightFingers))
        return false;

    if (!IsSafeVrikCameraOffsets(acPose.Vrik))
        return false;

    return true;
}

bool HasAnyPoseNode(const VRPoseUpdate& acPose) noexcept
{
    return acPose.Hmd.Valid || acPose.LeftHand.Valid || acPose.RightHand.Valid || acPose.SpellOrigin.Valid ||
           acPose.SpellDestination.Valid || acPose.ArrowOrigin.Valid || acPose.ArrowDestination.Valid ||
           acPose.BowAim.Valid || acPose.BowRotation.Valid || acPose.LeftWeaponOffset.Valid ||
           acPose.RightWeaponOffset.Valid || acPose.PrimaryMagicOffset.Valid || acPose.PrimaryMagicAim.Valid ||
           acPose.SecondaryMagicOffset.Valid || acPose.SecondaryMagicAim.Valid;
}

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}
}

VRPoseRelayService::VRPoseRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrPoseUpdateConnection(aDispatcher.sink<PacketEvent<RequestVRPoseUpdate>>().connect<&VRPoseRelayService::OnVRPoseUpdate>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRPoseRelayService::OnPlayerLeave>(this))
{
}

void VRPoseRelayService::OnVRPoseUpdate(const PacketEvent<RequestVRPoseUpdate>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR pose relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayPose(playerId, acMessage.Packet))
        return;

    NotifyVRPoseUpdate notify{};
    notify.PlayerId = playerId;
    notify.Pose = acMessage.Packet.Pose;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VRPoseRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerPoseRelayState.erase(acEvent.pPlayer->GetId());
}

bool VRPoseRelayService::ShouldRelayPose(uint32_t aPlayerId, const RequestVRPoseUpdate& acRequest) noexcept
{
    const auto& pose = acRequest.Pose;
    if (pose.Sequence == 0 || !HasAnyPoseNode(pose))
        return false;

    if (!IsPoseUpdateSafe(pose))
        return false;

    const auto stateIt = m_playerPoseRelayState.find(aPlayerId);
    const auto& state = stateIt != m_playerPoseRelayState.end() ? stateIt->second : PlayerPoseRelayState{};
    if (state.HasSequence && !IsNewerSequence(pose.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinPoseRelayIntervalMs)
        return false;

    auto& mutableState = m_playerPoseRelayState[aPlayerId];
    mutableState.LastSequence = pose.Sequence;
    mutableState.LastRelayTick = now;
    mutableState.HasSequence = true;
    return true;
}
