#include <Services/VREquipmentRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVREquipmentUpdate.h>
#include <Messages/RequestVREquipmentUpdate.h>
#include <Structs/VREquipmentUpdate.h>

namespace
{
constexpr uint64_t kMinEquipmentRelayIntervalMs = 900;

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}
}

VREquipmentRelayService::VREquipmentRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrEquipmentUpdateConnection(aDispatcher.sink<PacketEvent<RequestVREquipmentUpdate>>().connect<&VREquipmentRelayService::OnVREquipmentUpdate>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VREquipmentRelayService::OnPlayerLeave>(this))
{
}

void VREquipmentRelayService::OnVREquipmentUpdate(const PacketEvent<RequestVREquipmentUpdate>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR equipment relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayEquipment(playerId, acMessage.Packet))
        return;

    NotifyVREquipmentUpdate notify{};
    notify.PlayerId = playerId;
    notify.Equipment = acMessage.Packet.Equipment;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void VREquipmentRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_playerEquipmentRelayState.erase(acEvent.pPlayer->GetId());
}

bool VREquipmentRelayService::ShouldRelayEquipment(
    uint32_t aPlayerId, const RequestVREquipmentUpdate& acRequest) noexcept
{
    const auto& equipment = acRequest.Equipment;
    if (equipment.Sequence == 0)
        return false;

    auto& state = m_playerEquipmentRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(equipment.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick &&
        now - state.LastRelayTick < kMinEquipmentRelayIntervalMs)
        return false;

    state.LastSequence = equipment.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
