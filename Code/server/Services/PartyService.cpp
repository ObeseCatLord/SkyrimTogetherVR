#include <Services/PartyService.h>
#include <Components.h>
#include <GameServer.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/NotifyPlayerList.h>
#include <Messages/NotifyPartyInfo.h>
#include <Messages/NotifyPartyInvite.h>
#include <Messages/PartyInviteRequest.h>
#include <Messages/PartyAcceptInviteRequest.h>
#include <Messages/PartyLeaveRequest.h>
#include <Messages/NotifyPartyJoined.h>
#include <Messages/NotifyPartyLeft.h>
#include <Messages/PartyCreateRequest.h>
#include <Messages/PartyChangeLeaderRequest.h>
#include <Messages/PartyKickRequest.h>
#include <Messages/NotifyPlayerJoined.h>

#include <Setting.h>
namespace
{
Console::Setting bAutoPartyJoin{"Gameplay:bAutoPartyJoin", "Join parties automatically, as long as there is only one party in the server", true};

struct PartyJoinedNotification
{
    Player* pPlayer{};
    NotifyPartyJoined Message{};
};

bool IsManagedPlayer(World& aWorld, const Player* apPlayer) noexcept
{
    if (!apPlayer)
        return false;

    for (Player* pPlayer : aWorld.GetPlayerManager())
    {
        if (pPlayer == apPlayer)
            return true;
    }

    return false;
}

bool IsPartyMember(const PartyService::Party& acParty, const Player* apPlayer) noexcept
{
    return apPlayer && std::find(acParty.Members.begin(), acParty.Members.end(), apPlayer) != acParty.Members.end();
}

bool IsPartyStateValid(World& aWorld, const PartyService::Party& acParty, const uint32_t aPartyId) noexcept
{
    if (acParty.Members.empty())
        return false;

    bool hasLeader = false;
    for (auto itor = acParty.Members.begin(); itor != acParty.Members.end(); ++itor)
    {
        const Player* const pPlayer = *itor;
        if (!IsManagedPlayer(aWorld, pPlayer) || pPlayer->GetParty().JoinedPartyId != aPartyId ||
            std::find(acParty.Members.begin(), itor, pPlayer) != itor)
            return false;

        hasLeader = hasLeader || pPlayer->GetId() == acParty.LeaderPlayerId;
    }

    return hasLeader;
}

bool BuildPartyJoinedMessage(const PartyService::Party& acParty, const Player* apRecipient,
                             const Player* apAdditionalMember, NotifyPartyJoined& aMessage) noexcept
try
{
    if (!apRecipient || (apAdditionalMember && IsPartyMember(acParty, apAdditionalMember)))
        return false;

    aMessage.LeaderPlayerId = acParty.LeaderPlayerId;
    aMessage.IsLeader = acParty.LeaderPlayerId == apRecipient->GetId();
    aMessage.PlayerIds.reserve(acParty.Members.size() + (apAdditionalMember ? 1 : 0));

    for (const Player* pPlayer : acParty.Members)
    {
        if (!pPlayer)
            return false;

        aMessage.PlayerIds.push_back(pPlayer->GetId());
    }

    if (apAdditionalMember)
        aMessage.PlayerIds.push_back(apAdditionalMember->GetId());

    return true;
}
catch (...)
{
    spdlog::error("[PartyService]: Unable to stage party joined notification");
    return false;
}

bool BuildPartyInfoMessage(const PartyService::Party& acParty, const Player* apRemovedMember,
                           const Player* apAdditionalMember, const uint32_t aLeaderPlayerId,
                           NotifyPartyInfo& aMessage) noexcept
try
{
    if ((apRemovedMember && !IsPartyMember(acParty, apRemovedMember)) ||
        (apAdditionalMember && IsPartyMember(acParty, apAdditionalMember)))
        return false;

    size_t memberCount = apAdditionalMember ? 1 : 0;
    bool hasLeader = false;
    for (const Player* pPlayer : acParty.Members)
    {
        if (!pPlayer)
            return false;

        if (pPlayer == apRemovedMember)
            continue;

        ++memberCount;
        hasLeader = hasLeader || pPlayer->GetId() == aLeaderPlayerId;
    }

    if (apAdditionalMember)
        hasLeader = hasLeader || apAdditionalMember->GetId() == aLeaderPlayerId;

    if (memberCount == 0 || !hasLeader)
        return false;

    aMessage.LeaderPlayerId = aLeaderPlayerId;
    aMessage.PlayerIds.reserve(memberCount);
    for (const Player* pPlayer : acParty.Members)
    {
        if (pPlayer != apRemovedMember)
            aMessage.PlayerIds.push_back(pPlayer->GetId());
    }

    if (apAdditionalMember)
        aMessage.PlayerIds.push_back(apAdditionalMember->GetId());

    return true;
}
catch (...)
{
    spdlog::error("[PartyService]: Unable to stage party information notification");
    return false;
}
}

PartyService::PartyService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_updateEvent(aDispatcher.sink<UpdateEvent>().connect<&PartyService::OnUpdate>(this))
    , m_playerJoinConnection(aDispatcher.sink<PlayerJoinEvent>().connect<&PartyService::OnPlayerJoin>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&PartyService::OnPlayerLeave>(this))
    , m_partyInviteConnection(aDispatcher.sink<PacketEvent<PartyInviteRequest>>().connect<&PartyService::OnPartyInvite>(this))
    , m_partyAcceptInviteConnection(aDispatcher.sink<PacketEvent<PartyAcceptInviteRequest>>().connect<&PartyService::OnPartyAcceptInvite>(this))
    , m_partyLeaveConnection(aDispatcher.sink<PacketEvent<PartyLeaveRequest>>().connect<&PartyService::OnPartyLeave>(this))
    , m_partyCreateConnection(aDispatcher.sink<PacketEvent<PartyCreateRequest>>().connect<&PartyService::OnPartyCreate>(this))
    , m_partyChangeLeaderConnection(aDispatcher.sink<PacketEvent<PartyChangeLeaderRequest>>().connect<&PartyService::OnPartyChangeLeader>(this))
    , m_partyKickConnection(aDispatcher.sink<PacketEvent<PartyKickRequest>>().connect<&PartyService::OnPartyKick>(this))
{
}

const PartyService::Party* PartyService::GetById(uint32_t aId) const noexcept
{
    auto itor = m_parties.find(aId);
    if (itor != std::end(m_parties))
        return &itor->second;

    return nullptr;
}

bool PartyService::IsPlayerInParty(Player* const apPlayer) const noexcept
{
    if (!IsManagedPlayer(m_world, apPlayer))
        return false;

    const auto& partyComponent = apPlayer->GetParty();
    if (!partyComponent.JoinedPartyId)
        return false;

    const auto* const pParty = GetById(*partyComponent.JoinedPartyId);
    return pParty && IsPartyStateValid(m_world, *pParty, *partyComponent.JoinedPartyId) &&
        IsPartyMember(*pParty, apPlayer);
}

bool PartyService::IsPlayerLeader(const Player* const apPlayer) const noexcept
{
    if (!IsManagedPlayer(m_world, apPlayer))
        return false;

    const auto& partyComponent = apPlayer->GetParty();
    if (!partyComponent.JoinedPartyId)
        return false;

    const auto* const pParty = GetById(*partyComponent.JoinedPartyId);
    return pParty && IsPartyStateValid(m_world, *pParty, *partyComponent.JoinedPartyId) &&
        IsPartyMember(*pParty, apPlayer) && pParty->LeaderPlayerId == apPlayer->GetId();
}

PartyService::Party* PartyService::GetPlayerParty(Player* const apPlayer) noexcept
{
    if (!IsManagedPlayer(m_world, apPlayer))
        return nullptr;

    const auto& partyComponent = apPlayer->GetParty();
    if (!partyComponent.JoinedPartyId)
        return nullptr;

    auto itor = m_parties.find(*partyComponent.JoinedPartyId);
    if (itor == std::end(m_parties) || !IsPartyStateValid(m_world, itor.value(), *partyComponent.JoinedPartyId) ||
        !IsPartyMember(itor.value(), apPlayer))
        return nullptr;

    return &itor.value();
}

void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept try
{
    const auto cCurrentTick = GameServer::Get()->GetTick();
    if (m_nextInvitationExpire > cCurrentTick)
        return;

    // Only expire once every 10 seconds
    m_nextInvitationExpire = cCurrentTick + 10000;

    auto view = m_world.view<PartyComponent>();
    for (auto entity : view)
    {
        auto& partyComponent = view.get<PartyComponent>(entity);
        auto itor = std::begin(partyComponent.Invitations);
        while (itor != std::end(partyComponent.Invitations))
        {
            if (itor->second < cCurrentTick)
            {
                itor = partyComponent.Invitations.erase(itor);
            }
            else
            {
                ++itor;
            }
        }
    }
}
catch (...)
{
    spdlog::error("[PartyService]: Invitation expiration failed");
}

void PartyService::OnPartyCreate(const PacketEvent<PartyCreateRequest>& acPacket) noexcept try
{
    Player* const player = acPacket.pPlayer;

    spdlog::debug("[PartyService]: Received request to create party");

    if (!IsManagedPlayer(m_world, player))
    {
        spdlog::warn("[PartyService]: Ignoring party create request from an invalid player");
        return;
    }

    auto& playerPartyComponent = player->GetParty();
    if (playerPartyComponent.JoinedPartyId) // Ensure not in party
        return;

    const uint32_t partyId = m_nextId;
    if (m_parties.find(partyId) != std::end(m_parties))
    {
        spdlog::error("[PartyService]: Party id {} is already in use", partyId);
        return;
    }

    const bool autoJoin = m_parties.empty() && bAutoPartyJoin;
    const size_t playerCount = autoJoin ? m_world.GetPlayerManager().Count() : 1;

    Party newParty{};
    newParty.LeaderPlayerId = player->GetId();
    newParty.Members.reserve(playerCount);
    newParty.Members.push_back(player);

    Vector<PartyJoinedNotification> joinedNotifications;
    joinedNotifications.reserve(playerCount);

    PartyJoinedNotification playerNotification{};
    playerNotification.pPlayer = player;
    if (!BuildPartyJoinedMessage(newParty, player, nullptr, playerNotification.Message))
        return;
    joinedNotifications.push_back(std::move(playerNotification));

    if (autoJoin)
    {
        for (Player* otherPlayer : m_world.GetPlayerManager())
        {
            if (!otherPlayer || otherPlayer->GetId() == player->GetId() || otherPlayer->GetParty().JoinedPartyId)
                continue;

            newParty.Members.push_back(otherPlayer);

            PartyJoinedNotification notification{};
            notification.pPlayer = otherPlayer;
            if (!BuildPartyJoinedMessage(newParty, otherPlayer, nullptr, notification.Message))
                return;
            joinedNotifications.push_back(std::move(notification));
        }
    }

    NotifyPartyInfo partyInfo{};
    if (autoJoin && !BuildPartyInfoMessage(newParty, nullptr, nullptr, newParty.LeaderPlayerId, partyInfo))
        return;

    const auto [partyItor, inserted] = m_parties.emplace(partyId, std::move(newParty));
    if (!inserted)
    {
        spdlog::error("[PartyService]: Could not create party {}", partyId);
        return;
    }

    Party& party = partyItor.value();
    for (Player* pMember : party.Members)
        pMember->GetParty().JoinedPartyId = partyId;

    ++m_nextId;

    spdlog::debug("[PartyService]: Created party for {}", player->GetId());
    for (const auto& notification : joinedNotifications)
        SendPartyJoinedEvent(notification.pPlayer, notification.Message);

    if (autoJoin)
        BroadcastPartyInfo(party, partyInfo);
}
catch (...)
{
    spdlog::error("[PartyService]: Party creation rejected after an allocation or send failure");
}

void PartyService::OnPartyChangeLeader(const PacketEvent<PartyChangeLeaderRequest>& acPacket) noexcept try
{
    auto& message = acPacket.Packet;
    Player* const player = acPacket.pPlayer;

    spdlog::debug("[PartyService]: Received request to change party leader to {}", message.PartyMemberPlayerId);

    if (!IsManagedPlayer(m_world, player))
    {
        spdlog::warn("[PartyService]: Ignoring party leader change request from an invalid player");
        return;
    }

    Player* const pNewLeader = m_world.GetPlayerManager().GetById(message.PartyMemberPlayerId);
    if (!pNewLeader)
    {
        spdlog::error("[PartyService]: Player {} does not exist. Cannot change party leader", message.PartyMemberPlayerId);
        return;
    }

    Party* const pParty = GetPlayerParty(player);
    if (!pParty || pParty->LeaderPlayerId != player->GetId() || !IsPartyMember(*pParty, pNewLeader))
        return;

    NotifyPartyInfo partyInfo{};
    if (!BuildPartyInfoMessage(*pParty, nullptr, nullptr, pNewLeader->GetId(), partyInfo))
        return;

    pParty->LeaderPlayerId = pNewLeader->GetId();
    spdlog::debug("[PartyService]: Changed party leader to {}, updating party members.", pParty->LeaderPlayerId);
    BroadcastPartyInfo(*pParty, partyInfo);
}
catch (...)
{
    spdlog::error("[PartyService]: Party leader change rejected after an allocation or send failure");
}

void PartyService::OnPartyKick(const PacketEvent<PartyKickRequest>& acPacket) noexcept try
{
    auto& message = acPacket.Packet;
    Player* const player = acPacket.pPlayer;

    spdlog::debug("[PartyService]: Received request to kick player {}", message.PartyMemberPlayerId);

    if (!IsManagedPlayer(m_world, player))
    {
        spdlog::warn("[PartyService]: Ignoring party kick request from an invalid player");
        return;
    }

    Player* const pKick = m_world.GetPlayerManager().GetById(message.PartyMemberPlayerId);
    if (!pKick)
    {
        spdlog::error("[PartyService]: Player {} does not exist. Cannot kick", message.PartyMemberPlayerId);
        return;
    }

    Party* const pParty = GetPlayerParty(player);
    if (!pParty || pParty->LeaderPlayerId != player->GetId() || !IsPartyMember(*pParty, pKick))
        return;

    spdlog::debug("[PartyService]: Kicking player {} from party", pKick->GetId());
    if (RemovePlayerFromParty(pKick))
        BroadcastPlayerList(pKick);
}
catch (...)
{
    spdlog::error("[PartyService]: Party kick rejected after an allocation or send failure");
}

void PartyService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept try
{
    Player* const pJoiningPlayer = acEvent.pPlayer;
    if (!IsManagedPlayer(m_world, pJoiningPlayer))
    {
        spdlog::warn("[PartyService]: Ignoring player join for an invalid player");
        return;
    }

    BroadcastPlayerList();

    NotifyPlayerJoined notify{};
    notify.PlayerId = pJoiningPlayer->GetId();
    notify.Username = pJoiningPlayer->GetUsername();

    notify.WorldSpaceId = acEvent.WorldSpaceId;
    notify.CellId = acEvent.CellId;

    notify.Level = pJoiningPlayer->GetLevel();

    spdlog::debug("[Party] New notify player {:x} {}", notify.PlayerId, notify.Username.c_str());

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (!pPlayer || pPlayer == pJoiningPlayer)
            continue;

        try
        {
            pPlayer->Send(notify);
        }
        catch (...)
        {
            spdlog::error("[PartyService]: Unable to send player joined notification");
        }
    }

    if (m_parties.size() != 1 || !bAutoPartyJoin || pJoiningPlayer->GetParty().JoinedPartyId)
        return;

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        Party* const pParty = GetPlayerParty(pPlayer);
        if (!pParty)
            continue;

        const auto partyId = pPlayer->GetParty().JoinedPartyId;
        if (partyId && AddPlayerToParty(*pParty, *partyId, pJoiningPlayer))
            break;
    }
}
catch (...)
{
    spdlog::error("[PartyService]: Player join processing rejected after an allocation or send failure");
}

void PartyService::OnPartyInvite(const PacketEvent<PartyInviteRequest>& acPacket) noexcept try
{
    auto& message = acPacket.Packet;

    Player* const pInviter = acPacket.pPlayer;
    if (!IsManagedPlayer(m_world, pInviter))
    {
        spdlog::warn("[PartyService]: Ignoring party invite from an invalid player");
        return;
    }

    // Make sure the player we invite exists.
    Player* const pInvitee = m_world.GetPlayerManager().GetById(message.PlayerId);
    if (!pInvitee || pInvitee == pInviter)
        return;

    auto& inviteePartyComponent = pInvitee->GetParty();
    if (inviteePartyComponent.JoinedPartyId)
    {
        spdlog::debug("[PartyService]: Invitee in party already, cancelling invite.");
        return;
    }

    Party* const pParty = GetPlayerParty(pInviter);
    if (!pParty)
    {
        spdlog::debug("[PartyService]: Inviter not in party, cancelling invite.");
        return;
    }

    if (pParty->LeaderPlayerId != pInviter->GetId())
    {
        spdlog::debug("[PartyService]: Inviter not party leader, cancelling invite.");
        return;
    }

    // Expire in 60 seconds.
    const auto cExpiryTick = GameServer::Get()->GetTick() + 60000;

    NotifyPartyInvite notification{};
    notification.InviterId = pInviter->GetId();
    notification.ExpiryTick = cExpiryTick;

    auto invitationItor = inviteePartyComponent.Invitations.find(pInviter);
    const bool hadInvitation = invitationItor != std::end(inviteePartyComponent.Invitations);
    const uint64_t previousExpiry = hadInvitation ? invitationItor->second : 0;
    if (hadInvitation)
    {
        invitationItor->second = cExpiryTick;
    }
    else
    {
        const bool inserted = inviteePartyComponent.Invitations.emplace(pInviter, cExpiryTick).second;
        if (!inserted)
        {
            spdlog::error("[PartyService]: Unable to record party invite");
            return;
        }
    }

    try
    {
        spdlog::debug("[PartyService]: Sending party invite to {}", pInvitee->GetId());
        pInvitee->Send(notification);
    }
    catch (...)
    {
        if (hadInvitation)
            invitationItor->second = previousExpiry;
        else
            inviteePartyComponent.Invitations.erase(pInviter);

        spdlog::error("[PartyService]: Unable to send party invite");
    }
}
catch (...)
{
    spdlog::error("[PartyService]: Party invite rejected after an allocation or send failure");
}

void PartyService::OnPartyAcceptInvite(const PacketEvent<PartyAcceptInviteRequest>& acPacket) noexcept try
{
    auto& message = acPacket.Packet;
    Player* const pSelf = acPacket.pPlayer;
    if (!IsManagedPlayer(m_world, pSelf))
    {
        spdlog::warn("[PartyService]: Ignoring party acceptance from an invalid player");
        return;
    }

    spdlog::debug("[PartyService]: Got party accept request from {}", pSelf->GetId());

    Player* const pInviter = m_world.GetPlayerManager().GetById(message.InviterId);
    if (!pInviter || pInviter == pSelf)
        return;

    auto& selfPartyComponent = pSelf->GetParty();
    if (selfPartyComponent.Invitations.count(pInviter) == 0)
        return;

    if (selfPartyComponent.JoinedPartyId)
    {
        spdlog::debug("[PartyService]: Invitee already in party, cancelling.");
        return;
    }

    Party* const pParty = GetPlayerParty(pInviter);
    if (!pParty)
    {
        spdlog::debug("[PartyService]: Inviter not in party. Cancelling.");
        return;
    }

    if (pParty->LeaderPlayerId != pInviter->GetId())
    {
        spdlog::debug("[PartyService]: Inviter is not party leader. Cancelling.");
        return;
    }

    const auto partyId = pInviter->GetParty().JoinedPartyId;
    if (partyId && AddPlayerToParty(*pParty, *partyId, pSelf))
        spdlog::debug("[PartyService]: Added invitee to party, sending events");
}
catch (...)
{
    spdlog::error("[PartyService]: Party acceptance rejected after an allocation or send failure");
}

void PartyService::OnPartyLeave(const PacketEvent<PartyLeaveRequest>& acPacket) noexcept try
{
    RemovePlayerFromParty(acPacket.pPlayer);
}
catch (...)
{
    spdlog::error("[PartyService]: Party leave rejected after an allocation or send failure");
}

void PartyService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept try
{
    if (!IsManagedPlayer(m_world, acEvent.pPlayer))
    {
        spdlog::warn("[PartyService]: Ignoring player leave for an invalid player");
        return;
    }

    RemovePlayerFromParty(acEvent.pPlayer);
    BroadcastPlayerList(acEvent.pPlayer);
}
catch (...)
{
    spdlog::error("[PartyService]: Player leave processing rejected after an allocation or send failure");
}

bool PartyService::AddPlayerToParty(Party& aParty, const uint32_t aPartyId, Player* apPlayer) noexcept try
{
    auto partyItor = m_parties.find(aPartyId);
    if (!IsManagedPlayer(m_world, apPlayer) || partyItor == std::end(m_parties) || &partyItor.value() != &aParty ||
        apPlayer->GetParty().JoinedPartyId || !IsPartyStateValid(m_world, aParty, aPartyId) ||
        IsPartyMember(aParty, apPlayer))
        return false;

    NotifyPartyJoined joinedMessage{};
    if (!BuildPartyJoinedMessage(aParty, apPlayer, apPlayer, joinedMessage))
        return false;

    NotifyPartyInfo partyInfo{};
    if (!BuildPartyInfoMessage(aParty, nullptr, apPlayer, aParty.LeaderPlayerId, partyInfo))
        return false;

    aParty.Members.reserve(aParty.Members.size() + 1);
    aParty.Members.push_back(apPlayer);
    apPlayer->GetParty().JoinedPartyId = aPartyId;

    SendPartyJoinedEvent(apPlayer, joinedMessage);
    BroadcastPartyInfo(aParty, partyInfo);
    return true;
}
catch (...)
{
    spdlog::error("[PartyService]: Unable to add player to party");
    return false;
}

bool PartyService::RemovePlayerFromParty(Player* apPlayer) noexcept try
{
    if (!IsManagedPlayer(m_world, apPlayer))
        return false;

    auto& partyComponent = apPlayer->GetParty();
    if (!partyComponent.JoinedPartyId)
        return false;

    const uint32_t partyId = *partyComponent.JoinedPartyId;
    auto partyItor = m_parties.find(partyId);
    if (partyItor == std::end(m_parties) || !IsPartyStateValid(m_world, partyItor.value(), partyId))
    {
        spdlog::warn("[PartyService]: Cannot remove player from an invalid party");
        return false;
    }

    Party& party = partyItor.value();
    auto memberItor = std::find(party.Members.begin(), party.Members.end(), apPlayer);
    if (memberItor == party.Members.end())
    {
        spdlog::warn("[PartyService]: Player is not a member of the requested party");
        return false;
    }

    NotifyPartyLeft leftMessage{};
    NotifyPartyInfo partyInfo{};
    uint32_t newLeaderPlayerId = party.LeaderPlayerId;
    const bool leaderChanged = newLeaderPlayerId == apPlayer->GetId();
    if (party.Members.size() > 1)
    {
        if (leaderChanged)
        {
            for (Player* pMember : party.Members)
            {
                if (pMember != apPlayer)
                {
                    newLeaderPlayerId = pMember->GetId();
                    break;
                }
            }
        }

        if (!BuildPartyInfoMessage(party, apPlayer, nullptr, newLeaderPlayerId, partyInfo))
            return false;
    }

    party.Members.erase(memberItor);
    partyComponent.JoinedPartyId.reset();

    if (party.Members.empty())
    {
        m_parties.erase(partyId);
    }
    else
    {
        party.LeaderPlayerId = newLeaderPlayerId;
        if (leaderChanged)
            spdlog::debug("[PartyService]: Leader left, reassigned party leader to {}", newLeaderPlayerId);
        BroadcastPartyInfo(party, partyInfo);
    }

    try
    {
        spdlog::debug("[PartyService]: Sending party left event to player.");
        apPlayer->Send(leftMessage);
    }
    catch (...)
    {
        spdlog::error("[PartyService]: Unable to send party left notification");
    }

    return true;
}
catch (...)
{
    spdlog::error("[PartyService]: Unable to remove player from party");
    return false;
}

void PartyService::BroadcastPlayerList(Player* apPlayer) const noexcept try
{
    for (Player* pSelf : m_world.GetPlayerManager())
    {
        if (!pSelf || apPlayer == pSelf)
            continue;

        try
        {
            NotifyPlayerList playerList{};
            for (Player* pPlayer : m_world.GetPlayerManager())
            {
                if (!pPlayer || pSelf == pPlayer || apPlayer == pPlayer)
                    continue;

                playerList.Players[pPlayer->GetId()] = pPlayer->GetUsername();
            }

            pSelf->Send(playerList);
        }
        catch (...)
        {
            spdlog::error("[PartyService]: Unable to send player list notification");
        }
    }
}
catch (...)
{
    spdlog::error("[PartyService]: Unable to broadcast player list");
}

void PartyService::BroadcastPartyInfo(uint32_t aPartyId) const noexcept try
{
    auto itor = m_parties.find(aPartyId);
    if (itor == std::end(m_parties) || !IsPartyStateValid(m_world, itor.value(), aPartyId))
        return;

    NotifyPartyInfo message{};
    if (!BuildPartyInfoMessage(itor.value(), nullptr, nullptr, itor.value().LeaderPlayerId, message))
        return;

    BroadcastPartyInfo(itor.value(), message);
}
catch (...)
{
    spdlog::error("[PartyService]: Unable to broadcast party information");
}

void PartyService::BroadcastPartyInfo(const Party& acParty, NotifyPartyInfo& aMessage) const noexcept
{
    for (Player* pPlayer : acParty.Members)
    {
        if (!pPlayer)
            continue;

        try
        {
            aMessage.IsLeader = pPlayer->GetId() == acParty.LeaderPlayerId;
            pPlayer->Send(aMessage);
        }
        catch (...)
        {
            spdlog::error("[PartyService]: Unable to send party information notification");
        }
    }
}

void PartyService::SendPartyJoinedEvent(Player* apPlayer, const NotifyPartyJoined& acMessage) const noexcept
{
    if (!apPlayer)
        return;

    try
    {
        spdlog::debug("[PartyService]: Sending party join event to player");
        apPlayer->Send(acMessage);
    }
    catch (...)
    {
        spdlog::error("[PartyService]: Unable to send party joined notification");
    }
}
