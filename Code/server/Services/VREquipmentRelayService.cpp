#include <Services/VREquipmentRelayService.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Messages/NotifyVREquipmentUpdate.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/RequestVREquipmentUpdate.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VREquipmentUpdate.h>
#include <World.h>

namespace
{
constexpr uint64_t kMinEquipmentRelayIntervalMs = 900;
constexpr std::uint32_t kRightHandEquipSlotFormId = 0x00013F42;
constexpr std::uint32_t kLeftHandEquipSlotFormId = 0x00013F43;
constexpr auto kEquipmentCapability =
    SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VREquipmentRelay);

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

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VREquipmentRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    auto& relayState = m_playerEquipmentRelayState[playerId];
    const auto* previous = relayState.HasEquipment ? &relayState.LastEquipment : nullptr;
    if (!ShouldRelayEquipment(playerId, acMessage.Packet))
        return;

    NotifyVREquipmentUpdate notify{};
    notify.PlayerId = playerId;
    notify.Equipment = acMessage.Packet.Equipment;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            kEquipmentCapability,
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");

    if (character)
        RelayLegacyEquipment(World::ToInteger(*character), *character, previous, acMessage.Packet.Equipment,
                             acMessage.pPlayer);
    relayState.LastEquipment = acMessage.Packet.Equipment;
    relayState.HasEquipment = true;
}

void VREquipmentRelayService::RelayLegacyEquipment(
    const std::uint32_t aServerId, const entt::entity aOrigin, const VREquipmentUpdate* apPrevious,
    const VREquipmentUpdate& acCurrent, const Player* apSender) const noexcept
{
    const std::array<GameId, 6> current{acCurrent.LeftWeapon, acCurrent.RightWeapon, acCurrent.Ammo,
                                        acCurrent.LeftSpell, acCurrent.RightSpell, acCurrent.PowerOrShout};
    const std::array<GameId, 6> previous = apPrevious ?
        std::array<GameId, 6>{apPrevious->LeftWeapon, apPrevious->RightWeapon, apPrevious->Ammo,
                              apPrevious->LeftSpell, apPrevious->RightSpell, apPrevious->PowerOrShout} :
        std::array<GameId, 6>{};
    for (std::size_t index = 0; index < current.size(); ++index) {
        if (current[index] == previous[index])
            continue;
        const GameId slot = (index == 0 || index == 3) ? GameId{0, kLeftHandEquipSlotFormId} :
                            (index == 1 || index == 4) ? GameId{0, kRightHandEquipSlotFormId} : GameId{};
        const auto relay = [&](const GameId& acItem, const bool aUnequip) {
            if (!acItem)
                return;
            NotifyEquipmentChanges legacy{};
            legacy.ServerId = aServerId;
            legacy.ItemId = acItem;
            legacy.EquipSlotId = slot;
            legacy.Count = 1;
            legacy.Unequip = aUnequip;
            legacy.IsSpell = index == 3 || index == 4;
            legacy.IsShout = index == 5;
            TP_UNUSED(GameServer::Get()->SendToPlayersWithoutCapabilitiesInRange(
                legacy, aOrigin, kEquipmentCapability, apSender));
        };
        relay(previous[index], true);
        relay(current[index], false);
    }
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
