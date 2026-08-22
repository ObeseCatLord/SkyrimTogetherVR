#include <GameServer.h>
#include <Components.h>

#include <World.h>
#include <Services/QuestService.h>
#include <Services/RevisionedRecoveryPolicy.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/NotifyQuestResync.h>
#include <Messages/RequestQuestResync.h>

#include <Events/PlayerLeaveEvent.h>

#include <Setting.h>

#include <limits>
namespace
{
Console::Setting bEnableMiscQuestSync{"Gameplay:bEnableMiscQuestSync", "(Experimental) Syncs miscellaneous quests when possible", false};
constexpr std::size_t kMaximumQuestLogEntries = 4096;
constexpr std::size_t kMaximumQuestSnapshotRevisions = 4096;
constexpr std::uint8_t kMaximumQuestType = 11;

[[nodiscard]] bool IsValidQuestSnapshot(const QuestLog& acQuests) noexcept
{
    if (!acQuests.IsDecodedValid || acQuests.Entries.size() > kMaximumQuestLogEntries)
        return false;
    for (std::size_t index = 0; index < acQuests.Entries.size(); ++index)
    {
        if (!acQuests.Entries[index].Id)
            return false;
        for (std::size_t prior = 0; prior < index; ++prior)
            if (acQuests.Entries[prior].Id == acQuests.Entries[index].Id)
                return false;
    }
    return true;
}
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
    m_questResyncConnection = aDispatcher.sink<PacketEvent<RequestQuestResync>>().connect<&QuestService::OnQuestResyncRequest>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&QuestService::OnPlayerLeave>(this);
}

std::uint64_t QuestService::NextQuestRevision(
    const std::uint32_t aPlayerId, const std::uint64_t aKnownRevision) noexcept
{
    if (aPlayerId == 0)
        return 0;
    try
    {
        auto it = m_questRevisions.find(aPlayerId);
        if (it == m_questRevisions.end())
        {
            if (m_questRevisions.size() >= kMaximumQuestSnapshotRevisions)
                return 0;
            it = m_questRevisions.emplace(aPlayerId, 0).first;
        }
        if (!SkyrimTogether::RevisionedRecovery::CanIssueSnapshot(it->second, aKnownRevision))
            return 0;
        return ++it->second;
    }
    catch (...)
    {
        return 0;
    }
}

void QuestService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_questRevisions.erase(acEvent.pPlayer->GetId());
}

void QuestService::OnQuestResyncRequest(const PacketEvent<RequestQuestResync>& acMessage) noexcept
{
    const auto& request = acMessage.Packet;
    if (!acMessage.pPlayer || !request.IsDecodedValid || !request.IsValid() ||
        !SkyrimTogether::Protocol::IsVrGameplayClient(acMessage.pPlayer->GetGameplayCapabilities()) ||
        !SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::RevisionedCanonicalRecovery))
        return;

    const auto requesterParty = acMessage.pPlayer->GetParty().JoinedPartyId;
    auto* const owner = m_world.GetPlayerManager().GetById(request.OwnerPlayerId);
    if (!owner || !SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy::CanAuthorizeQuestOwner(
                      acMessage.pPlayer->GetId(), owner->GetId(), requesterParty.value_or(0),
                      owner->GetParty().JoinedPartyId.value_or(0)))
        return;

    const auto& snapshot = owner->GetQuestLogComponent().QuestContent;
    if (!IsValidQuestSnapshot(snapshot))
        return;
    const auto revision = NextQuestRevision(owner->GetId(), request.KnownRevision);
    if (revision == 0)
        return;

    NotifyQuestResync response{};
    response.OwnerPlayerId = owner->GetId();
    response.RequestId = request.RequestId;
    response.CanonicalRevision = revision;
    response.HasParty = true;
    response.PartyId = *requesterParty;
    response.Snapshot = snapshot;
    acMessage.pPlayer->Send(response);
}

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;

    auto* pPlayer = acMessage.pPlayer;
    if (!pPlayer || (message.OwnerPlayerId != 0 && message.OwnerPlayerId != pPlayer->GetId()) ||
        !message.Id || message.ClientQuestType > kMaximumQuestType ||
        (message.Status != RequestQuestUpdate::Started &&
         message.Status != RequestQuestUpdate::StageUpdate &&
         message.Status != RequestQuestUpdate::Stopped))
        return;

    const auto& partyComponent = pPlayer->GetParty();
    if (!partyComponent.JoinedPartyId.has_value())
        return;

    if (message.ClientQuestType == 0 || message.ClientQuestType == 6) // Types None or Miscellaneous. Hard-coded to avoid client header file.
    {
        if (!bEnableMiscQuestSync)
            return;
    }

    // The sender is the only authority for a normal quest mutation; a legacy
    // zero owner claim is normalized here, never forwarded. Reserve the
    // canonical revision before modifying the log so an unorderable update
    // fails closed.
    const auto revision = NextQuestRevision(pPlayer->GetId(), 0);
    if (revision == 0)
        return;

    auto& questComponent = pPlayer->GetQuestLogComponent();
    auto& entries = questComponent.QuestContent.Entries;

    auto questIt = std::find_if(entries.begin(), entries.end(), [&message](const auto& e) { return e.Id == message.Id; });

    NotifyQuestUpdate notify{};
    notify.OwnerPlayerId = pPlayer->GetId();
    notify.CanonicalRevision = revision;
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    notify.Status = message.Status;
    notify.ClientQuestType = message.ClientQuestType;

    if (message.Status == RequestQuestUpdate::Started || message.Status == RequestQuestUpdate::StageUpdate)
    {
        // in order to prevent bugs when a quest is in progress
        // and being updated we add it as a new quest record to
        // maintain a proper remote questlog state.
        if (questIt == entries.end())
        {
            if (entries.size() >= kMaximumQuestLogEntries)
                return;
            auto& newQuest = entries.emplace_back();
            newQuest.Id = message.Id;
            newQuest.Stage = message.Stage;

            if (message.Status == RequestQuestUpdate::Started)
            {
                notify.Status = NotifyQuestUpdate::Started;
            }
            else
            {
                notify.Status = NotifyQuestUpdate::StageUpdate;
            }
        }
        else
        {
            auto& record = *questIt;
            record.Id = message.Id;
            record.Stage = message.Stage;

            notify.Status = NotifyQuestUpdate::StageUpdate;
        }
    }
    else if (message.Status == RequestQuestUpdate::Stopped)
    {
        if (questIt != entries.end())
            entries.erase(questIt);

        notify.Status = NotifyQuestUpdate::Stopped;
    }

    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}
