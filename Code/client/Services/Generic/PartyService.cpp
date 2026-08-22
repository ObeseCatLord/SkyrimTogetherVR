#include <Services/PartyService.h>

#include <Services/OverlayService.h>
#include <Services/TransportService.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

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

#include <OverlayApp.hpp>

#include <Forms/TESGlobal.h>

#include <algorithm>

PartyService::PartyService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransportService) noexcept
    : m_world(aWorld)
    , m_transport(aTransportService)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PartyService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PartyService::OnDisconnected>(this);

    m_playerListConnection = aDispatcher.sink<NotifyPlayerList>().connect<&PartyService::OnPlayerList>(this);
    m_partyInfoConnection = aDispatcher.sink<NotifyPartyInfo>().connect<&PartyService::OnPartyInfo>(this);
    m_partyInviteConnection = aDispatcher.sink<NotifyPartyInvite>().connect<&PartyService::OnPartyInvite>(this);
    m_partyJoinedConnection = aDispatcher.sink<NotifyPartyJoined>().connect<&PartyService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<NotifyPartyLeft>().connect<&PartyService::OnPartyLeft>(this);
}

bool PartyService::CreateParty() const noexcept
{
    if (m_inParty)
        return false;

    PartyCreateRequest request;
    return m_transport.Send(request);
}

bool PartyService::LeaveParty() const noexcept
{
    if (!m_inParty)
        return false;

    PartyLeaveRequest request;
    return m_transport.Send(request);
}

bool PartyService::CreateInvite(const uint32_t aPlayerId) const noexcept
{
    if (!m_inParty || !m_isLeader || aPlayerId == 0 || aPlayerId == m_transport.GetLocalPlayerId() ||
        !m_players.contains(aPlayerId) || std::find(m_partyMembers.begin(), m_partyMembers.end(), aPlayerId) != m_partyMembers.end())
        return false;

    PartyInviteRequest request;
    request.PlayerId = aPlayerId;
    return m_transport.Send(request);
}

bool PartyService::AcceptInvite(const uint32_t aInviterId) noexcept
{
    const auto invitation = m_invitations.find(aInviterId);
    if (m_inParty || invitation == m_invitations.end() || invitation->second < m_transport.GetClock().GetCurrentTick())
        return false;

    PartyAcceptInviteRequest request;
    request.InviterId = aInviterId;
    // Sending is asynchronous.  Keep the invitation until the server confirms
    // the party transition (or its normal expiry) so a rejected/lost request
    // does not destroy the only retryable client state.
    return m_transport.Send(request);
}

bool PartyService::DeclineInvite(const uint32_t aInviterId) noexcept
{
    const auto invitation = m_invitations.find(aInviterId);
    if (invitation == m_invitations.end())
        return false;

    m_invitations.erase(invitation);
    return true;
}

bool PartyService::KickPartyMember(const uint32_t aPlayerId) const noexcept
{
    if (!m_inParty || !m_isLeader || aPlayerId == 0 || aPlayerId == m_transport.GetLocalPlayerId() ||
        std::find(m_partyMembers.begin(), m_partyMembers.end(), aPlayerId) == m_partyMembers.end())
        return false;

    PartyKickRequest kickMessage;
    kickMessage.PartyMemberPlayerId = aPlayerId;
    return m_transport.Send(kickMessage);
}

bool PartyService::ChangePartyLeader(const uint32_t aPlayerId) const noexcept
{
    if (!m_inParty || !m_isLeader || aPlayerId == 0 || aPlayerId == m_transport.GetLocalPlayerId() ||
        std::find(m_partyMembers.begin(), m_partyMembers.end(), aPlayerId) == m_partyMembers.end())
        return false;

    PartyChangeLeaderRequest changeMessage;
    changeMessage.PartyMemberPlayerId = aPlayerId;
    return m_transport.Send(changeMessage);
}

void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto cCurrentTick = m_transport.GetClock().GetCurrentTick();
    if (m_nextUpdate > cCurrentTick)
        return;

    // Update once every second
    m_nextUpdate = cCurrentTick + 1000;

    auto itor = std::begin(m_invitations);
    while (itor != std::end(m_invitations))
    {
        if (itor->second < cCurrentTick)
            itor = m_invitations.erase(itor);
        else
            ++itor;
    }
}

void PartyService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    DestroyParty();
    m_invitations.clear();
    m_players.clear();
}

void PartyService::OnPlayerList(const NotifyPlayerList& acPlayerList) noexcept
{
    m_players = acPlayerList.Players;
}

void PartyService::OnPartyInfo(const NotifyPartyInfo& acPartyInfo) noexcept
{
    if (m_inParty)
    {
        spdlog::debug("[PartyService]: Got party info update");
        m_isLeader = acPartyInfo.IsLeader;
        m_leaderPlayerId = acPartyInfo.LeaderPlayerId;
        m_partyMembers = acPartyInfo.PlayerIds;

        // TODO: this can be done a bit prettier
        if (m_isLeader)
        {
#if !TP_SKYRIM_VR
            TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
            pWorldEncountersEnabled->SetValueData(1.f);
#endif
        }

        if (auto* pOverlayService = m_world.ctx().find<OverlayService>())
        {
            if (auto* pOverlay = pOverlayService->GetOverlayApp())
            {
                auto pArguments = CefListValue::Create();
                auto pPlayerIds = CefListValue::Create();
                for (int i = 0; i < m_partyMembers.size(); i++)
                    pPlayerIds->SetInt(i, m_partyMembers[i]);

                pArguments->SetList(0, pPlayerIds);
                pArguments->SetInt(1, acPartyInfo.LeaderPlayerId);
                pOverlay->ExecuteAsync("partyInfo", pArguments);
            }
        }
    }
}

void PartyService::OnPartyInvite(const NotifyPartyInvite& acPartyInvite) noexcept
{
    spdlog::debug("[PartyService]: Got party invite from {}", acPartyInvite.InviterId);

    m_invitations[acPartyInvite.InviterId] = acPartyInvite.ExpiryTick;

    if (auto* pOverlayService = m_world.ctx().find<OverlayService>())
    {
        if (auto* pOverlay = pOverlayService->GetOverlayApp())
        {
            auto pArguments = CefListValue::Create();
            pArguments->SetInt(0, acPartyInvite.InviterId);
            pOverlay->ExecuteAsync("partyInviteReceived", pArguments);
        }
    }
}

void PartyService::OnPartyJoined(const NotifyPartyJoined& acPartyJoined) noexcept
{
    spdlog::debug("[PartyService]: Joined party. LeaderId: {}, IsLeader: {}", acPartyJoined.LeaderPlayerId, acPartyJoined.IsLeader);

    m_inParty = true;
    m_isLeader = acPartyJoined.IsLeader;
    m_leaderPlayerId = acPartyJoined.LeaderPlayerId;
    m_partyMembers = acPartyJoined.PlayerIds;
    m_invitations.clear();

    m_world.GetDispatcher().trigger(PartyJoinedEvent(m_isLeader));
}

void PartyService::OnPartyLeft(const NotifyPartyLeft& acPartyLeft) noexcept
{
    spdlog::debug("[PartyService]: Left party");

    DestroyParty();

    m_world.GetDispatcher().trigger(PartyLeftEvent());
}

void PartyService::DestroyParty() noexcept
{
    m_inParty = false;
    m_isLeader = false;
    m_leaderPlayerId = 0;
    m_partyMembers.clear();
}
