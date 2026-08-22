#include <TiltedOnlinePCH.h>

#include <Services/VRWorldReplicationService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/PlayerDialogueEvent.h>
#include <Events/RemoteGameplayBridgeResultEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyActivate.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/NotifyActorTeleport.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/NotifyObjectResync.h>
#include <Messages/NotifyPartyInfo.h>
#include <Messages/NotifyPlayerDialogue.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/NotifyQuestResync.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyTeleport.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/DialogueRequest.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/RequestCurrentWeather.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestObjectResync.h>
#include <Messages/RequestQuestResync.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestWeatherChange.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/TeleportCommandResponse.h>
#include <Services/TransportService.h>
#include <Services/VRAvatarService.h>
#include <Services/VRNpcOwnershipService.h>
#include <Structs/ServerSettings.h>
#include <Structs/GameplayCapabilities.h>
#include <VRGameplayBridge.h>
#include <World.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr std::size_t kMaximumRetainedLockStates = 128;
constexpr std::uint8_t kMaximumCanonicalResyncAttempts = 3;
constexpr double kCanonicalResyncRetrySeconds = 2.0;
constexpr std::uint8_t kObjectRecoveryInventory = 1u << 0;
constexpr std::uint8_t kObjectRecoveryLock = 1u << 1;
constexpr std::uint8_t kObjectRecoveryOpenState = 1u << 2;
constexpr std::uint8_t kMaximumReconcileAttempts = 3;
constexpr double kReconcileIntervalSeconds = 0.25;
constexpr std::size_t kMaximumPendingTextTransactions = 64;
constexpr std::size_t kMaximumPendingOutboundTransactions = 128;
constexpr std::uint8_t kMaximumRemoteCommandAttempts = 3;
constexpr double kRemoteCommandInitialRetryDelaySeconds = 0.125;
constexpr double kRemoteCommandMaximumRetryDelaySeconds = 0.5;
constexpr double kRemoteCommandLifetimeSeconds = 5.0;
constexpr double kRemoteCommandResultExpirySeconds = 2.0;
constexpr double kWaypointEchoSuppressionSeconds = 3.0;
constexpr double kSubtitleAssemblyExpirySeconds = 2.0;
constexpr float kWaypointPositionTolerance = 1.0F;
constexpr float kMaximumWorldPosition = 10'000'000.0F;
constexpr std::uint32_t kPlayerReferenceFormId = 0x00000014;
constexpr std::size_t kMaximumSubtitleTextBytes = 512;

[[nodiscard]] bool IsZeroBytes(const void* apData, const std::size_t aSize) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(apData);
    for (std::size_t index = 0; index < aSize; ++index)
    {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsSafeVoiceResourcePath(const std::string_view acPath) noexcept
{
    if (acPath.empty() || acPath.size() > 512 || acPath.front() == '/' || acPath.front() == '\\' ||
        acPath.find('\0') != std::string_view::npos || acPath.find(':') != std::string_view::npos)
        return false;

    std::size_t segmentStart = 0;
    while (segmentStart < acPath.size())
    {
        const auto segmentEnd = acPath.find_first_of("/\\", segmentStart);
        const auto segment = acPath.substr(
            segmentStart,
            segmentEnd == std::string_view::npos ? std::string_view::npos : segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (segmentEnd == std::string_view::npos)
            break;
        segmentStart = segmentEnd + 1;
    }

    const auto hasExtension = [&acPath](const std::string_view acExtension) {
        if (acPath.size() < acExtension.size())
            return false;
        const auto suffix = acPath.substr(acPath.size() - acExtension.size());
        return std::equal(suffix.begin(), suffix.end(), acExtension.begin(), [](const char aLeft, const char aRight) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(aLeft))) == aRight;
        });
    };
    return hasExtension(".fuz") || hasExtension(".xwm") || hasExtension(".wav");
}

[[nodiscard]] bool IsValidUtf8(const std::string_view acText) noexcept
{
    const auto isContinuation = [&acText](const std::size_t aIndex) noexcept {
        return aIndex < acText.size() &&
               (static_cast<std::uint8_t>(acText[aIndex]) & 0xC0u) == 0x80u;
    };
    for (std::size_t index = 0; index < acText.size();) {
        const auto byte = static_cast<std::uint8_t>(acText[index]);
        if (byte <= 0x7Fu) {
            ++index;
        } else if (byte >= 0xC2u && byte <= 0xDFu && isContinuation(index + 1)) {
            index += 2;
        } else if (byte == 0xE0u && index + 2 < acText.size() &&
                   static_cast<std::uint8_t>(acText[index + 1]) >= 0xA0u &&
                   static_cast<std::uint8_t>(acText[index + 1]) <= 0xBFu && isContinuation(index + 2)) {
            index += 3;
        } else if ((byte >= 0xE1u && byte <= 0xECu || byte >= 0xEEu && byte <= 0xEFu) &&
                   isContinuation(index + 1) && isContinuation(index + 2)) {
            index += 3;
        } else if (byte == 0xEDu && index + 2 < acText.size() &&
                   static_cast<std::uint8_t>(acText[index + 1]) >= 0x80u &&
                   static_cast<std::uint8_t>(acText[index + 1]) <= 0x9Fu && isContinuation(index + 2)) {
            index += 3;
        } else if (byte == 0xF0u && index + 3 < acText.size() &&
                   static_cast<std::uint8_t>(acText[index + 1]) >= 0x90u &&
                   static_cast<std::uint8_t>(acText[index + 1]) <= 0xBFu &&
                   isContinuation(index + 2) && isContinuation(index + 3)) {
            index += 4;
        } else if (byte >= 0xF1u && byte <= 0xF3u && isContinuation(index + 1) &&
                   isContinuation(index + 2) && isContinuation(index + 3)) {
            index += 4;
        } else if (byte == 0xF4u && index + 3 < acText.size() &&
                   static_cast<std::uint8_t>(acText[index + 1]) >= 0x80u &&
                   static_cast<std::uint8_t>(acText[index + 1]) <= 0x8Fu &&
                   isContinuation(index + 2) && isContinuation(index + 3)) {
            index += 4;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool BuildWorldCommand(
    const TransportService& aTransport,
    const GameplayBridge::GameplayDomain aDomain,
    const GameplayBridge::GameplayAction aAction,
    GameplayBridge::CommandRecord& arCommand) noexcept
{
    const auto nonce = aTransport.GetServerInstanceNonce();
    const auto generation = aTransport.GetConnectionGeneration();
    const auto epoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (!aTransport.IsOnline() || nonce == 0 || generation == 0 || epoch == 0 ||
        !GameplayBridge::IsActionInDomain(aDomain, aAction))
        return false;

    arCommand.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayAction);
    arCommand.Header.PayloadSize = GameplayBridge::kFixedPayloadBytes;
    arCommand.Header.Identity.ServerInstanceNonce = nonce;
    arCommand.Header.Identity.ConnectionGeneration = generation;
    arCommand.Header.Identity.LifecycleEpoch = epoch;
    arCommand.Payload.ApplyGameplayAction.Domain = static_cast<std::uint16_t>(aDomain);
    arCommand.Payload.ApplyGameplayAction.Action = static_cast<std::uint16_t>(aAction);
    return true;
}

[[nodiscard]] std::uint32_t ToLocalForm(World& aWorld, const GameId& acId) noexcept
{
    return acId ? aWorld.GetModSystem().GetGameId(acId) : 0;
}

[[nodiscard]] bool IsRetryableRemoteCommandStatus(const GameplayBridge::CommandStatus aStatus) noexcept
{
    return aStatus == GameplayBridge::CommandStatus::Inactive ||
           aStatus == GameplayBridge::CommandStatus::MissingForm ||
           aStatus == GameplayBridge::CommandStatus::MissingCell ||
           aStatus == GameplayBridge::CommandStatus::QueueOverflow;
}

[[nodiscard]] double RemoteCommandRetryDelay(const std::uint8_t aAttempts) noexcept
{
    const auto exponent = std::min<std::uint8_t>(aAttempts > 0 ? aAttempts - 1 : 0, 2);
    return std::min(kRemoteCommandMaximumRetryDelaySeconds,
                    kRemoteCommandInitialRetryDelaySeconds * static_cast<double>(1u << exponent));
}

[[nodiscard]] bool SubmitTextTransaction(
    GameplayBridge::CommandRecord aBase,
    const std::uint64_t aTextId,
    const std::string_view acText) noexcept try
{
    const auto maxBytes = static_cast<std::size_t>(GameplayBridge::kGameplayTextBytesPerChunk) *
                          GameplayBridge::kMaximumGameplayTextChunks;
    const auto byteCount = std::min(acText.size(), maxBytes);
    const auto chunkCount = static_cast<std::uint16_t>(std::max<std::size_t>(
        1, (byteCount + GameplayBridge::kGameplayTextBytesPerChunk - 1) /
               GameplayBridge::kGameplayTextBytesPerChunk));

    std::vector<GameplayBridge::CommandRecord> commands(chunkCount);
    for (std::uint16_t index = 0; index < chunkCount; ++index)
    {
        auto& command = commands[index];
        command.Header = aBase.Header;
        command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayTextChunk);
        const auto& base = aBase.Payload.ApplyGameplayAction;
        auto& payload = command.Payload.ApplyGameplayTextChunk;
        payload.TargetHandle = base.TargetHandle;
        payload.TargetLocalFormId = base.TargetLocalFormId;
        payload.AuxiliaryLocalFormId = base.LocalFormIdA;
        payload.Domain = base.Domain;
        payload.Action = base.Action;
        payload.TextId = aTextId;
        payload.ChunkIndex = index;
        payload.ChunkCount = chunkCount;
        const auto offset = static_cast<std::size_t>(index) * GameplayBridge::kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
            GameplayBridge::kGameplayTextBytesPerChunk, byteCount - std::min(offset, byteCount)));
        if (payload.ByteCount != 0)
            std::memcpy(payload.Utf8Bytes, acText.data() + offset, payload.ByteCount);
    }
    return SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size());
}
catch (...)
{
    return false;
}
} // namespace

template <class T>
bool VRWorldReplicationService::SendOutbound(T&& aRequest, const std::size_t aDomainIndex,
                                             const std::uint64_t aActionId) noexcept try
{
    using Request = std::decay_t<T>;
    Request request{std::forward<T>(aRequest)};
    if (m_pendingOutbound.empty() && m_transport.Send(request))
    {
        if (aActionId != 0 && aDomainIndex < m_lastLocalActionIdByDomain.size())
            m_lastLocalActionIdByDomain[aDomainIndex] = aActionId;
        return true;
    }

    if (m_pendingOutbound.size() >= kMaximumPendingOutboundTransactions)
    {
        TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
        return false;
    }
    if (m_pendingOutbound.empty())
    {
        m_pendingOutboundServerInstanceNonce = m_transport.GetServerInstanceNonce();
        m_pendingOutboundConnectionGeneration = m_transport.GetConnectionGeneration();
        m_pendingOutboundLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    }
    m_pendingOutbound.push_back({[this, request = std::move(request)]() mutable {
        return m_transport.Send(request);
    }});
    if (aActionId != 0 && aDomainIndex < m_lastLocalActionIdByDomain.size())
        m_lastLocalActionIdByDomain[aDomainIndex] = aActionId;
    return true;
}
catch (...)
{
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
    return false;
}

void VRWorldReplicationService::TrySendPendingOutbound() noexcept
{
    if (m_pendingOutbound.empty())
        return;
    if (!m_transport.IsOnline() ||
        m_pendingOutboundServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        m_pendingOutboundConnectionGeneration != m_transport.GetConnectionGeneration() ||
        m_pendingOutboundLifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch())
    {
        m_pendingOutbound.clear();
        m_pendingOutboundServerInstanceNonce = 0;
        m_pendingOutboundConnectionGeneration = 0;
        m_pendingOutboundLifecycleEpoch = 0;
        return;
    }

    while (!m_pendingOutbound.empty())
    {
        if (!m_pendingOutbound.front().TrySend())
            return;
        m_pendingOutbound.pop_front();
    }
    m_pendingOutboundServerInstanceNonce = 0;
    m_pendingOutboundConnectionGeneration = 0;
    m_pendingOutboundLifecycleEpoch = 0;
}

VRWorldReplicationService::VRWorldReplicationService(
    World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
    VRAvatarService& aAvatars) noexcept
    : m_world(aWorld), m_transport(aTransport), m_avatars(aAvatars)
{
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&VRWorldReplicationService::OnActivate>(this);
    m_assignObjectsConnection = aDispatcher.sink<AssignObjectsResponse>().connect<&VRWorldReplicationService::OnAssignObjects>(this);
    m_actorTeleportConnection = aDispatcher.sink<NotifyActorTeleport>().connect<&VRWorldReplicationService::OnActorTeleport>(this);
    m_chatConnection = aDispatcher.sink<NotifyChatMessageBroadcast>().connect<&VRWorldReplicationService::OnChatMessage>(this);
    m_dialogueConnection = aDispatcher.sink<NotifyDialogue>().connect<&VRWorldReplicationService::OnDialogue>(this);
    m_lockConnection = aDispatcher.sink<NotifyLockChange>().connect<&VRWorldReplicationService::OnLockChange>(this);
    m_packageConnection = aDispatcher.sink<NotifyNewPackage>().connect<&VRWorldReplicationService::OnNewPackage>(this);
    m_objectInventoryConnection = aDispatcher.sink<NotifyObjectInventoryChanges>().connect<&VRWorldReplicationService::OnObjectInventory>(this);
    m_objectResyncConnection = aDispatcher.sink<NotifyObjectResync>().connect<&VRWorldReplicationService::OnObjectResync>(this);
    m_playerDialogueConnection = aDispatcher.sink<NotifyPlayerDialogue>().connect<&VRWorldReplicationService::OnPlayerDialogue>(this);
    m_questConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&VRWorldReplicationService::OnQuestUpdate>(this);
    m_questResyncConnection = aDispatcher.sink<NotifyQuestResync>().connect<&VRWorldReplicationService::OnQuestResync>(this);
    m_removeWaypointConnection = aDispatcher.sink<NotifyRemoveWaypoint>().connect<&VRWorldReplicationService::OnRemoveWaypoint>(this);
    m_scriptAnimationConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&VRWorldReplicationService::OnScriptAnimation>(this);
    m_setWaypointConnection = aDispatcher.sink<NotifySetWaypoint>().connect<&VRWorldReplicationService::OnSetWaypoint>(this);
    m_subtitleConnection = aDispatcher.sink<NotifySubtitle>().connect<&VRWorldReplicationService::OnSubtitle>(this);
    m_teleportConnection = aDispatcher.sink<NotifyTeleport>().connect<&VRWorldReplicationService::OnTeleport>(this);
    m_teleportCommandConnection = aDispatcher.sink<TeleportCommandResponse>().connect<&VRWorldReplicationService::OnTeleportCommand>(this);
    m_timeConnection = aDispatcher.sink<ServerTimeSettings>().connect<&VRWorldReplicationService::OnTimeSettings>(this);
    m_weatherConnection = aDispatcher.sink<NotifyWeatherChange>().connect<&VRWorldReplicationService::OnWeatherChange>(this);
    m_settingsConnection = aDispatcher.sink<ServerSettings>().connect<&VRWorldReplicationService::OnServerSettings>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&VRWorldReplicationService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&VRWorldReplicationService::OnPartyLeft>(this);
    m_partyInfoConnection = aDispatcher.sink<NotifyPartyInfo>().connect<&VRWorldReplicationService::OnPartyInfo>(this);
    m_playerDialogueEventConnection = aDispatcher.sink<PlayerDialogueEvent>()
        .connect<&VRWorldReplicationService::OnPlayerDialogueEvent>(this);
    m_localGameplayConnection = aDispatcher.sink<SkyrimTogetherVR::LocalGameplayBridgeEvent>()
        .connect<&VRWorldReplicationService::OnLocalGameplay>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRWorldReplicationService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRWorldReplicationService::OnDisconnected>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRWorldReplicationService::OnUpdate>(this);
    m_gameplayResultConnection = aDispatcher.sink<SkyrimTogetherVR::RemoteGameplayBridgeResultEvent>()
        .connect<&VRWorldReplicationService::OnGameplayResult>(this);
}

bool VRWorldReplicationService::SubmitRemoteCommand(
    GameplayBridge::CommandRecord aCommand, const CanonicalRecoveryOperation aRecoveryOperation,
    const std::uint32_t aCanonicalServerId, const std::uint64_t aCanonicalRevision,
    const std::uint8_t aAuthoritativeOpenState, const std::uint32_t aCanonicalOwnerPlayerId,
    const GameId& acCanonicalQuestId) noexcept
{
    ObserveSession();
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(aCommand.Payload.ApplyGameplayAction.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(aCommand.Payload.ApplyGameplayAction.Action);
    if (aCommand.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayAction) ||
        !GameplayBridge::IsActionInDomain(domain, action))
    {
        spdlog::warn("VR world replication refused untrackable gameplay command");
        return false;
    }

    const auto slot = std::find_if(m_pendingRemoteCommands.begin(), m_pendingRemoteCommands.end(),
                                   [](const PendingRemoteCommand& acPending) { return !acPending.Occupied; });
    if (slot == m_pendingRemoteCommands.end())
    {
        spdlog::warn("VR world replication remote command ledger reached {} entries", m_pendingRemoteCommands.size());
        if (aRecoveryOperation == CanonicalRecoveryOperation::Quest && aCanonicalOwnerPlayerId != 0)
            RequestQuestResync(aCanonicalOwnerPlayerId);
        return false;
    }

    *slot = {};
    slot->Command = std::move(aCommand);
    slot->RecoveryOperation = aRecoveryOperation;
    slot->CanonicalServerId = aCanonicalServerId;
    slot->CanonicalRevision = aCanonicalRevision;
    slot->AuthoritativeOpenState = aAuthoritativeOpenState;
    slot->CanonicalOwnerPlayerId = aCanonicalOwnerPlayerId;
    slot->CanonicalQuestId = acCanonicalQuestId;
    // ActionId is claimed only by the bridge for each admission attempt.
    slot->Command.Header.Identity.ActionId = 0;
    slot->LifetimeRemaining = kRemoteCommandLifetimeSeconds;
    slot->Occupied = true;
    TrySubmitPendingRemoteCommand(*slot);
    return true;
}

bool VRWorldReplicationService::IsPendingRemoteCommandCurrent(const PendingRemoteCommand& acPending) const noexcept
{
    const auto& identity = acPending.Command.Header.Identity;
    return acPending.Occupied && m_transport.IsOnline() &&
           m_observedServerInstanceNonce != 0 && m_observedConnectionGeneration != 0 &&
           m_observedLifecycleEpoch != 0 &&
           m_transport.GetServerInstanceNonce() == m_observedServerInstanceNonce &&
           m_transport.GetConnectionGeneration() == m_observedConnectionGeneration &&
           SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() == m_observedLifecycleEpoch &&
           identity.ServerInstanceNonce == m_observedServerInstanceNonce &&
           identity.ConnectionGeneration == m_observedConnectionGeneration &&
           identity.LifecycleEpoch == m_observedLifecycleEpoch;
}

bool VRWorldReplicationService::QueueWorldInventoryTransaction(
    const GameId& acTargetId, const Inventory& acInventory, const bool aReset,
    const std::uint32_t aCanonicalServerId, const std::uint64_t aCanonicalRevision) noexcept try
{
    ObserveSession();
    if (!acInventory.IsDecodedValid ||
        m_pendingWorldInventoryTransactionCount >= kMaximumPendingWorldInventoryTransactions)
    {
        if (m_pendingWorldInventoryTransactionCount >= kMaximumPendingWorldInventoryTransactions)
            spdlog::warn("VR world inventory transaction ledger reached {} entries",
                         kMaximumPendingWorldInventoryTransactions);
        if (aCanonicalServerId != 0 && aCanonicalRevision != 0)
            FailObjectRecovery(aCanonicalServerId, aCanonicalRevision);
        else if (const auto serverId = FindObjectServerId(acTargetId); serverId != 0)
            RequestObjectResync(serverId);
        return false;
    }

    PendingWorldInventoryTransaction pending{};
    pending.TargetId = acTargetId;
    pending.Entries.assign(acInventory.Entries.begin(), acInventory.Entries.end());
    pending.Reset = aReset;
    pending.CanonicalServerId = aCanonicalServerId;
    pending.CanonicalRevision = aCanonicalRevision;

    // Build before queueing so an unmapped form or invalid stack cannot block a
    // target's FIFO behind a transaction that can never be admitted.
    std::vector<GameplayBridge::CommandRecord> commands;
    if (!BuildWorldInventoryTransactionCommands(pending, commands)) {
        if (aCanonicalServerId != 0 && aCanonicalRevision != 0)
            FailObjectRecovery(aCanonicalServerId, aCanonicalRevision);
        else if (const auto serverId = FindObjectServerId(acTargetId); serverId != 0)
            RequestObjectResync(serverId);
        return false;
    }

    const auto& identity = commands.front().Header.Identity;
    pending.ServerInstanceNonce = identity.ServerInstanceNonce;
    pending.ConnectionGeneration = identity.ConnectionGeneration;
    pending.LifecycleEpoch = identity.LifecycleEpoch;
    pending.TargetLocalFormId = commands.front().Payload.ApplyGameplayAction.TargetLocalFormId;

    auto [queue, inserted] = m_pendingWorldInventoryTransactions.try_emplace(acTargetId);
    TP_UNUSED(inserted);
    queue->second.emplace_back(std::move(pending));
    ++m_pendingWorldInventoryTransactionCount;
    if (queue->second.size() != 1)
        return true;

    TrySubmitPendingWorldInventoryTransaction(queue->second.front());
    if (!queue->second.front().Terminal)
        return true;

    auto& terminal = queue->second.front();
    if (!terminal.RecoveryCompletionReported) {
        terminal.RecoveryCompletionReported = true;
        CompleteObjectRecoveryInventory(terminal.CanonicalServerId, terminal.CanonicalRevision, false);
    }
    queue->second.pop_front();
    --m_pendingWorldInventoryTransactionCount;
    if (queue->second.empty())
        m_pendingWorldInventoryTransactions.erase(queue);
    return false;
}
catch (...)
{
    spdlog::debug("VR world inventory transaction construction failed");
    if (aCanonicalServerId != 0 && aCanonicalRevision != 0)
        FailObjectRecovery(aCanonicalServerId, aCanonicalRevision);
    else if (const auto serverId = FindObjectServerId(acTargetId); serverId != 0)
        RequestObjectResync(serverId);
    return false;
}

bool VRWorldReplicationService::BuildWorldInventoryTransactionCommands(
    const PendingWorldInventoryTransaction& acPending,
    std::vector<GameplayBridge::CommandRecord>& arCommands) const noexcept try
{
    arCommands.clear();
    if (!acPending.TargetId || acPending.Entries.size() > GameplayBridge::kMaximumInventoryTransactionItems ||
        (acPending.Entries.empty() && !acPending.Reset))
        return false;

    const auto targetLocalFormId = ToLocalForm(m_world, acPending.TargetId);
    if (targetLocalFormId == 0)
        return false;

    std::size_t totalEffects{};
    for (const auto& item : acPending.Entries)
    {
        if (!item.IsValidMutation() || (acPending.Reset && item.Count < 0) ||
            item.EnchantData.Effects.size() > GameplayBridge::kMaximumInventoryTransactionEffects - totalEffects)
            return false;
        totalEffects += item.EnchantData.Effects.size();

        const auto knownClassification = Inventory::Entry::kEquipmentWeapon | Inventory::Entry::kEquipmentAmmo;
        if ((item.EquipmentFlags & ~knownClassification) != 0 ||
            (item.EquipmentFlags & knownClassification) == knownClassification)
            return false;

        const bool hasEnchantment = static_cast<bool>(item.ExtraEnchantId);
        const bool dynamicEnchantment =
            item.ExtraEnchantId.ModId == (std::numeric_limits<std::uint32_t>::max)();
        if ((dynamicEnchantment && (!hasEnchantment || item.EnchantData.Effects.empty())) ||
            (!dynamicEnchantment && !item.EnchantData.Effects.empty()))
            return false;

        const bool weapon = (item.EquipmentFlags & Inventory::Entry::kEquipmentWeapon) != 0;
        if (hasEnchantment && item.EnchantData.IsWeapon != weapon)
            return false;
    }

    const auto recordCount = 2 + acPending.Entries.size() * 2 + totalEffects;
    if (recordCount > GameplayBridge::kMaximumInventoryTransactionRecords)
        return false;
    arCommands.reserve(recordCount);

    const auto append = [this, targetLocalFormId, &arCommands](const GameplayBridge::GameplayAction aAction,
                                                                GameplayBridge::GameplayActionPayload aPayload) {
        GameplayBridge::CommandRecord command{};
        if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Inventory, aAction, command))
            return false;

        command.Header.Identity.EntityId = 0;
        command.Header.Identity.EntityGeneration = 0;
        command.Header.Identity.Reserved0 = 0;
        command.Header.Identity.SequenceId = 0;
        command.Header.Identity.ActionId = 0;
        aPayload.TargetHandle = {};
        aPayload.SecondaryHandle = {};
        aPayload.TargetLocalFormId = targetLocalFormId;
        aPayload.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Inventory);
        aPayload.Action = static_cast<std::uint16_t>(aAction);
        command.Payload.ApplyGameplayAction = aPayload;
        arCommands.push_back(std::move(command));
        return true;
    };

    GameplayBridge::GameplayActionPayload begin{};
    begin.ValueA = static_cast<std::int32_t>(acPending.Entries.size());
    begin.ActionFlags = acPending.Reset ? GameplayBridge::kInventoryTransactionReset : 0u;
    if (!append(GameplayBridge::GameplayAction::InventoryTransactionBegin, begin))
        return false;

    for (std::size_t itemIndex = 0; itemIndex < acPending.Entries.size(); ++itemIndex)
    {
        const auto& item = acPending.Entries[itemIndex];
        const auto baseFormId = ToLocalForm(m_world, item.BaseId);
        if (baseFormId == 0)
            return false;

        GameplayBridge::GameplayActionPayload itemPayload{};
        itemPayload.LocalFormIdA = baseFormId;
        itemPayload.LocalFormIdB = static_cast<std::uint32_t>(acPending.Entries.size());
        itemPayload.ValueA = item.Count;
        itemPayload.ValueB = static_cast<std::int32_t>(itemIndex);
        itemPayload.ActionFlags =
            (item.IsQuestItem ? GameplayBridge::kInventoryTransactionQuestItem : 0u) |
            (item.ExtraWorn ? GameplayBridge::kInventoryTransactionWorn : 0u) |
            (item.ExtraWornLeft ? GameplayBridge::kInventoryTransactionWornLeft : 0u) |
            ((item.EquipmentFlags & Inventory::Entry::kEquipmentWeapon) != 0 ?
                 GameplayBridge::kInventoryTransactionWeapon : 0u) |
            ((item.EquipmentFlags & Inventory::Entry::kEquipmentAmmo) != 0 ?
                 GameplayBridge::kInventoryTransactionAmmo : 0u);
        if (!append(GameplayBridge::GameplayAction::InventoryTransactionItem, itemPayload))
            return false;

        const bool hasEnchantment = static_cast<bool>(item.ExtraEnchantId);
        const bool dynamicEnchantment =
            item.ExtraEnchantId.ModId == (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t enchantmentFormId{};
        if (hasEnchantment) {
            enchantmentFormId = dynamicEnchantment ? GameplayBridge::kInventoryTransactionDynamicEnchantmentFormId :
                                                    ToLocalForm(m_world, item.ExtraEnchantId);
            if (enchantmentFormId == 0)
                return false;
        }

        const auto poisonFormId = item.ExtraPoisonId ? ToLocalForm(m_world, item.ExtraPoisonId) : 0;
        if (item.ExtraPoisonId && poisonFormId == 0)
            return false;

        GameplayBridge::GameplayActionPayload extraPayload{};
        extraPayload.LocalFormIdA = enchantmentFormId;
        extraPayload.LocalFormIdB = poisonFormId;
        extraPayload.LocalFormIdC = static_cast<std::uint32_t>(item.ExtraSoulLevel);
        extraPayload.LocalFormIdD = static_cast<std::uint32_t>(item.EnchantData.Effects.size());
        extraPayload.ValueA = item.ExtraEnchantCharge;
        extraPayload.ValueB = static_cast<std::int32_t>(item.ExtraPoisonCount);
        extraPayload.ScalarA = item.ExtraCharge;
        extraPayload.ScalarB = item.ExtraHealth;
        extraPayload.ActionFlags =
            (item.ExtraEnchantRemoveUnequip ? GameplayBridge::kInventoryTransactionEnchantRemoveUnequip : 0u) |
            (item.EnchantData.IsWeapon ? GameplayBridge::kInventoryTransactionEnchantIsWeapon : 0u);
        if (!append(GameplayBridge::GameplayAction::InventoryTransactionItemExtra, extraPayload))
            return false;

        for (std::size_t effectIndex = 0; effectIndex < item.EnchantData.Effects.size(); ++effectIndex)
        {
            const auto& effect = item.EnchantData.Effects[effectIndex];
            const auto effectFormId = ToLocalForm(m_world, effect.EffectId);
            if (effectFormId == 0)
                return false;

            GameplayBridge::GameplayActionPayload effectPayload{};
            effectPayload.LocalFormIdA = effectFormId;
            effectPayload.LocalFormIdB = static_cast<std::uint32_t>(itemIndex);
            effectPayload.LocalFormIdC = static_cast<std::uint32_t>(effectIndex);
            effectPayload.LocalFormIdD = static_cast<std::uint32_t>(item.EnchantData.Effects.size());
            effectPayload.ValueA = effect.Area;
            effectPayload.ValueB = effect.Duration;
            effectPayload.ScalarA = effect.Magnitude;
            effectPayload.ScalarB = effect.RawCost;
            if (!append(GameplayBridge::GameplayAction::InventoryTransactionItemEffect, effectPayload))
                return false;
        }
    }

    if (!append(GameplayBridge::GameplayAction::InventoryTransactionEnd, {}))
        return false;
    return arCommands.size() == recordCount;
}
catch (...)
{
    arCommands.clear();
    return false;
}

bool VRWorldReplicationService::IsPendingWorldInventoryTransactionCurrent(
    const PendingWorldInventoryTransaction& acPending) const noexcept
{
    return !acPending.Terminal && m_transport.IsOnline() && m_observedServerInstanceNonce != 0 &&
           m_observedConnectionGeneration != 0 && m_observedLifecycleEpoch != 0 &&
           acPending.ServerInstanceNonce == m_observedServerInstanceNonce &&
           acPending.ConnectionGeneration == m_observedConnectionGeneration &&
           acPending.LifecycleEpoch == m_observedLifecycleEpoch &&
           m_transport.GetServerInstanceNonce() == m_observedServerInstanceNonce &&
           m_transport.GetConnectionGeneration() == m_observedConnectionGeneration &&
           SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() == m_observedLifecycleEpoch;
}

void VRWorldReplicationService::TrySubmitPendingWorldInventoryTransaction(
    PendingWorldInventoryTransaction& arPending) noexcept
{
    if (!IsPendingWorldInventoryTransactionCurrent(arPending))
    {
        arPending.Terminal = true;
        return;
    }
    if (arPending.Attempts >= kMaximumRemoteCommandAttempts)
    {
        arPending.Terminal = true;
        return;
    }

    std::vector<GameplayBridge::CommandRecord> commands;
    if (!BuildWorldInventoryTransactionCommands(arPending, commands))
    {
        arPending.Terminal = true;
        return;
    }

    ++arPending.Attempts;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size()))
    {
        if (arPending.Attempts >= kMaximumRemoteCommandAttempts)
            arPending.Terminal = true;
        else
            arPending.RetryDelay = RemoteCommandRetryDelay(arPending.Attempts);
        return;
    }

    const auto& first = commands.front().Header.Identity;
    const auto& last = commands.back().Header.Identity;
    if (first.ActionId == 0 || last.ActionId != first.ActionId + commands.size() - 1 ||
        first.ServerInstanceNonce != arPending.ServerInstanceNonce ||
        first.ConnectionGeneration != arPending.ConnectionGeneration ||
        first.LifecycleEpoch != arPending.LifecycleEpoch ||
        first.EntityId != 0 || first.EntityGeneration != 0 || first.Reserved0 != 0 || first.SequenceId != 0)
    {
        // A successful admission with an unexpected result identity is ambiguous.
        arPending.Terminal = true;
        return;
    }
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
        const auto& command = commands[index];
        const auto& identity = command.Header.Identity;
        const auto& payload = command.Payload.ApplyGameplayAction;
        if (identity.ActionId != first.ActionId + index ||
            identity.ServerInstanceNonce != first.ServerInstanceNonce ||
            identity.ConnectionGeneration != first.ConnectionGeneration ||
            identity.LifecycleEpoch != first.LifecycleEpoch || identity.EntityId != 0 ||
            identity.EntityGeneration != 0 || identity.Reserved0 != 0 || identity.SequenceId != 0 ||
            payload.TargetHandle.Value != 0 || payload.SecondaryHandle.Value != 0 ||
            payload.TargetLocalFormId == 0 ||
            payload.Domain != static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Inventory))
        {
            arPending.Terminal = true;
            return;
        }
    }

    arPending.TargetLocalFormId = commands.front().Payload.ApplyGameplayAction.TargetLocalFormId;
    arPending.FirstActionId = first.ActionId;
    arPending.EndActionId = last.ActionId;
    arPending.NextResultActionId = first.ActionId;
    arPending.RetryDelay = 0.0;
    arPending.ResultRemaining = kRemoteCommandResultExpirySeconds;
    arPending.AwaitingResult = true;
}

void VRWorldReplicationService::RetryPendingWorldInventoryTransactions(const double aDelta) noexcept
{
    if (!std::isfinite(aDelta) || aDelta < 0.0)
        return;

    const auto delta = std::min(aDelta, kRemoteCommandLifetimeSeconds);
    for (auto queue = m_pendingWorldInventoryTransactions.begin();
         queue != m_pendingWorldInventoryTransactions.end();)
    {
        auto& transactions = queue->second;
        if (transactions.empty())
        {
            queue = m_pendingWorldInventoryTransactions.erase(queue);
            continue;
        }

        auto& pending = transactions.front();
        if (!IsPendingWorldInventoryTransactionCurrent(pending))
        {
            pending.Terminal = true;
        }
        else if (pending.AwaitingResult)
        {
            pending.ResultRemaining -= delta;
            if (pending.ResultRemaining <= 0.0)
            {
                // An unreported End may already have changed the container.
                pending.Terminal = true;
            }
        }
        else
        {
            pending.RetryDelay -= delta;
            if (pending.RetryDelay <= 0.0)
                TrySubmitPendingWorldInventoryTransaction(pending);
        }

        if (pending.Terminal)
        {
            if (!pending.RecoveryCompletionReported) {
                pending.RecoveryCompletionReported = true;
                CompleteObjectRecoveryInventory(pending.CanonicalServerId, pending.CanonicalRevision, false);
            }
            transactions.pop_front();
            --m_pendingWorldInventoryTransactionCount;
        }
        if (transactions.empty())
            queue = m_pendingWorldInventoryTransactions.erase(queue);
        else
            ++queue;
    }
}

void VRWorldReplicationService::HandlePendingWorldInventoryTransactionResult(
    const GameplayBridge::EventRecord& acRecord) noexcept
{
    const auto& identity = acRecord.Header.Identity;
    const auto& result = acRecord.Payload.RemoteGameplayActionState;
    for (auto& [targetId, transactions] : m_pendingWorldInventoryTransactions)
    {
        TP_UNUSED(targetId);
        if (transactions.empty())
            continue;

        auto& pending = transactions.front();
        if (!pending.AwaitingResult || !IsPendingWorldInventoryTransactionCurrent(pending) ||
            identity.ActionId < pending.FirstActionId || identity.ActionId > pending.EndActionId)
            continue;

        const auto expectedAction = [&pending, actionId = identity.ActionId]() {
            if (actionId == pending.FirstActionId)
                return GameplayBridge::GameplayAction::InventoryTransactionBegin;

            std::size_t recordIndex = static_cast<std::size_t>(actionId - pending.FirstActionId - 1);
            for (const auto& item : pending.Entries) {
                if (recordIndex == 0)
                    return GameplayBridge::GameplayAction::InventoryTransactionItem;
                if (recordIndex == 1)
                    return GameplayBridge::GameplayAction::InventoryTransactionItemExtra;
                recordIndex -= 2;
                if (recordIndex < item.EnchantData.Effects.size())
                    return GameplayBridge::GameplayAction::InventoryTransactionItemEffect;
                recordIndex -= item.EnchantData.Effects.size();
            }
            return GameplayBridge::GameplayAction::InventoryTransactionEnd;
        };

        if (identity.ServerInstanceNonce != pending.ServerInstanceNonce ||
            identity.ConnectionGeneration != pending.ConnectionGeneration ||
            identity.LifecycleEpoch != pending.LifecycleEpoch || identity.EntityId != 0 ||
            identity.EntityGeneration != 0 || identity.Reserved0 != 0 || identity.SequenceId != 0 ||
            identity.ActionId != pending.NextResultActionId || result.TargetHandle.Value != 0 ||
            result.TargetLocalFormId != pending.TargetLocalFormId ||
            result.Domain != static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Inventory) ||
            result.Action != static_cast<std::uint16_t>(expectedAction()))
        {
            pending.Terminal = true;
            return;
        }

        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        if (identity.ActionId == pending.EndActionId)
        {
            // End owns engine application. A failure here is never replayable.
            const bool succeeded = status == GameplayBridge::CommandStatus::Success;
            pending.Terminal = true;
            pending.RecoveryCompletionReported = true;
            CompleteObjectRecoveryInventory(pending.CanonicalServerId, pending.CanonicalRevision, succeeded);
            return;
        }
        if (status == GameplayBridge::CommandStatus::Success)
        {
            ++pending.NextResultActionId;
            return;
        }

        // End was admitted atomically with this record. The bridge exposes no
        // cancellation or proof that End has not run, so no result failure can
        // satisfy the safe-replay condition for this batch.
        pending.Terminal = true;
        return;
    }
}

void VRWorldReplicationService::ClearPendingWorldInventoryTransactions() noexcept
{
    m_pendingWorldInventoryTransactions.clear();
    m_pendingWorldInventoryTransactionCount = 0;
}

void VRWorldReplicationService::TrySubmitPendingRemoteCommand(PendingRemoteCommand& arPending) noexcept
{
    if (!IsPendingRemoteCommandCurrent(arPending))
    {
        CompleteCanonicalRecoveryCommand(arPending, false);
        arPending = {};
        return;
    }
    if (arPending.Attempts >= kMaximumRemoteCommandAttempts)
    {
        spdlog::warn("Dropping VR world remote command after {} admission attempts", arPending.Attempts);
        CompleteCanonicalRecoveryCommand(arPending, false);
        arPending = {};
        return;
    }

    auto command = arPending.Command;
    command.Header.Identity.ActionId = 0;
    ++arPending.Attempts;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        arPending.Command.Header.Identity.ActionId = 0;
        arPending.AwaitingResult = false;
        arPending.ResultRemaining = 0.0;
        if (arPending.Attempts >= kMaximumRemoteCommandAttempts)
        {
            spdlog::warn("Dropping VR world remote command after {} admission attempts", arPending.Attempts);
            CompleteCanonicalRecoveryCommand(arPending, false);
            arPending = {};
            return;
        }
        arPending.RetryDelay = RemoteCommandRetryDelay(arPending.Attempts);
        return;
    }

    const auto& expected = arPending.Command.Header.Identity;
    const auto& submitted = command.Header.Identity;
    if (submitted.ServerInstanceNonce != expected.ServerInstanceNonce ||
        submitted.ConnectionGeneration != expected.ConnectionGeneration ||
        submitted.LifecycleEpoch != expected.LifecycleEpoch)
    {
        // A session change after admission cannot be replayed safely.
        CompleteCanonicalRecoveryCommand(arPending, false);
        arPending = {};
        return;
    }

    arPending.Command = command;
    arPending.AwaitingResult = true;
    arPending.RetryDelay = 0.0;
    arPending.ResultRemaining = kRemoteCommandResultExpirySeconds;
}

void VRWorldReplicationService::RetryPendingRemoteCommands(const double aDelta) noexcept
{
    if (!std::isfinite(aDelta) || aDelta < 0.0)
        return;

    const auto delta = std::min(aDelta, kRemoteCommandLifetimeSeconds);
    for (auto& pending : m_pendingRemoteCommands)
    {
        if (!pending.Occupied)
            continue;
        if (!IsPendingRemoteCommandCurrent(pending))
        {
            CompleteCanonicalRecoveryCommand(pending, false);
            pending = {};
            continue;
        }

        pending.LifetimeRemaining -= delta;
        if (pending.LifetimeRemaining <= 0.0)
        {
            spdlog::debug("Expiring VR world remote command before a terminal result");
            CompleteCanonicalRecoveryCommand(pending, false);
            pending = {};
            continue;
        }
        if (pending.AwaitingResult)
        {
            pending.ResultRemaining -= delta;
            if (pending.ResultRemaining <= 0.0)
            {
                // The command may have mutated the engine; do not replay it.
                spdlog::debug("Expiring VR world remote command after result timeout");
                CompleteCanonicalRecoveryCommand(pending, false);
                pending = {};
            }
            continue;
        }

        pending.RetryDelay -= delta;
        if (pending.RetryDelay <= 0.0)
            TrySubmitPendingRemoteCommand(pending);
    }
}

bool VRWorldReplicationService::HandlePendingRemoteCommandResult(
    const GameplayBridge::EventRecord& acRecord) noexcept
{
    const auto& result = acRecord.Payload.RemoteGameplayActionState;
    const auto& identity = acRecord.Header.Identity;
    for (auto& pending : m_pendingRemoteCommands)
    {
        if (!pending.Occupied || !pending.AwaitingResult || !IsPendingRemoteCommandCurrent(pending))
            continue;

        const auto& expectedIdentity = pending.Command.Header.Identity;
        const auto& expectedPayload = pending.Command.Payload.ApplyGameplayAction;
        if (identity.ServerInstanceNonce != expectedIdentity.ServerInstanceNonce ||
            identity.ConnectionGeneration != expectedIdentity.ConnectionGeneration ||
            identity.LifecycleEpoch != expectedIdentity.LifecycleEpoch ||
            identity.EntityId != expectedIdentity.EntityId ||
            identity.EntityGeneration != expectedIdentity.EntityGeneration ||
            identity.Reserved0 != expectedIdentity.Reserved0 ||
            identity.SequenceId != expectedIdentity.SequenceId || identity.ActionId != expectedIdentity.ActionId ||
            result.TargetHandle.Value != expectedPayload.TargetHandle.Value ||
            result.TargetLocalFormId != expectedPayload.TargetLocalFormId ||
            result.Domain != expectedPayload.Domain || result.Action != expectedPayload.Action)
            continue;

        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        if (status == GameplayBridge::CommandStatus::Success)
        {
            CompleteCanonicalRecoveryCommand(pending, true);
            pending = {};
            return true;
        }
        if (IsRetryableRemoteCommandStatus(status) && pending.Attempts < kMaximumRemoteCommandAttempts &&
            pending.LifetimeRemaining > 0.0)
        {
            pending.Command.Header.Identity.ActionId = 0;
            pending.AwaitingResult = false;
            pending.ResultRemaining = 0.0;
            pending.RetryDelay = RemoteCommandRetryDelay(pending.Attempts);
            return true;
        }

        // All other bridge statuses may follow partial or ambiguous engine work.
        CompleteCanonicalRecoveryCommand(pending, false);
        pending = {};
        return true;
    }
    return false;
}

void VRWorldReplicationService::SubmitText(
    GameplayBridge::CommandRecord aBase, const std::uint64_t aTextId, const std::string_view acText) noexcept try
{
    if (SubmitTextTransaction(aBase, aTextId, acText))
        return;

    if (m_pendingText.size() >= kMaximumPendingTextTransactions)
    {
        spdlog::warn("Dropping oldest VR world text transaction after retry queue reached {} entries",
                     kMaximumPendingTextTransactions);
        m_pendingText.pop_front();
    }
    m_pendingText.push_back({aBase, aTextId, std::string(acText), 1});
    m_textRetryTimer = kReconcileIntervalSeconds;
}
catch (...)
{
}

void VRWorldReplicationService::RetryPendingText() noexcept
{
    if (m_pendingText.empty())
        return;

    auto& pending = m_pendingText.front();
    if (pending.Base.Header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        pending.Base.Header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        pending.Base.Header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch())
    {
        m_pendingText.pop_front();
        return;
    }

    if (SubmitTextTransaction(pending.Base, pending.TextId, pending.Text))
    {
        m_pendingText.pop_front();
        return;
    }

    if (++pending.Attempts >= kMaximumReconcileAttempts)
    {
        spdlog::warn("Dropping VR world text transaction after {} enqueue attempts", pending.Attempts);
        m_pendingText.pop_front();
    }
}

void VRWorldReplicationService::OnAssignObjects(const AssignObjectsResponse& acMessage) noexcept try
{
    for (const auto& object : acMessage.Objects)
    {
        const auto objectFormId = ToLocalForm(m_world, object.Id);
        if (!object.IsDecodedValid || !object.Id || object.ServerId == 0)
            continue;
        m_objectServerIds.insert_or_assign(object.Id, object.ServerId);
        if (objectFormId == 0 || object.IsSenderFirst)
            continue;

        if (object.CurrentLockData != LockData{})
        {
            auto [it, inserted] = m_lockStates.try_emplace(objectFormId);
            if (inserted && m_lockStates.size() > kMaximumRetainedLockStates)
            {
                m_lockStates.erase(it);
            }
            else if (CaptureSession(it->second))
            {
                RetainState(it->second, objectFormId, 0, object.CurrentLockData.IsLocked ? 1 : 0,
                            object.CurrentLockData.IsLocked ?
                                static_cast<std::int32_t>(object.CurrentLockData.LockLevel) : -1,
                            0.0F, 0.0F, 0.0F);
            }
        }

        // Reset transactions intentionally include Begin+End for an empty
        // inventory so the native adapter can authoritatively clear a container.
        TP_UNUSED(QueueWorldInventoryTransaction(object.Id, object.CurrentInventory, true));
    }
    Reconcile();
}
catch (...)
{
}

void VRWorldReplicationService::OnActivate(const NotifyActivate& acMessage) noexcept
{
    if (!acMessage.IsDecodedValid || !acMessage.IsValid() ||
        acMessage.ActivatorId == m_transport.GetLocalPlayerId())
        return;
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(
            acMessage.ActivatorId, GameplayBridge::GameplayDomain::Object,
            GameplayBridge::GameplayAction::Activate, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = ToLocalForm(m_world, acMessage.Id);
    payload.ValueA = acMessage.PreActivationOpenState;
    if (payload.TargetLocalFormId != 0)
        SubmitRemoteCommand(command,
                            acMessage.HasPostActivationOpenState ? CanonicalRecoveryOperation::Activation :
                                                                    CanonicalRecoveryOperation::None,
                            0, 0, acMessage.PostActivationOpenState);
}

void VRWorldReplicationService::OnActorTeleport(const NotifyActorTeleport& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::WorldState,
                           GameplayBridge::GameplayAction::Teleport, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = ToLocalForm(m_world, acMessage.FormId);
    payload.LocalFormIdA = ToLocalForm(m_world, acMessage.CellId);
    payload.LocalFormIdB = ToLocalForm(m_world, acMessage.WorldSpaceId);
    payload.ScalarA = acMessage.Position.x;
    payload.ScalarB = acMessage.Position.y;
    payload.ScalarC = acMessage.Position.z;
    if (payload.TargetLocalFormId != 0 && payload.LocalFormIdA != 0)
        SubmitRemoteCommand(command);
}

void VRWorldReplicationService::OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept try
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Dialogue,
                           GameplayBridge::GameplayAction::Dialogue, command))
        return;
    std::string text;
    if (!acMessage.PlayerName.empty())
    {
        text = acMessage.PlayerName.c_str();
        text += ": ";
    }
    text += acMessage.ChatMessage.c_str();
    if (!text.empty())
        SubmitText(command, m_nextTextId++, text);
}
catch (...)
{
}

void VRWorldReplicationService::OnDialogue(const NotifyDialogue& acMessage) noexcept
{
    if (acMessage.SoundFilename.empty() || acMessage.SoundFilename.size() > 512)
        return;
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(
            acMessage.ServerId, GameplayBridge::GameplayDomain::Dialogue,
            GameplayBridge::GameplayAction::Dialogue, command))
        return;
    SubmitText(command, m_nextTextId++, acMessage.SoundFilename.c_str());
}

void VRWorldReplicationService::OnLockChange(const NotifyLockChange& acMessage) noexcept try
{
    const auto formId = ToLocalForm(m_world, acMessage.Id);
    if (formId == 0 || !m_transport.IsOnline() || m_transport.GetServerInstanceNonce() == 0 ||
        m_transport.GetConnectionGeneration() == 0)
        return;

    auto [it, inserted] = m_lockStates.try_emplace(formId);
    if (inserted && m_lockStates.size() > kMaximumRetainedLockStates)
    {
        m_lockStates.erase(it);
        spdlog::debug("VR world reconciliation dropped lock state for {:X}; retention limit reached", formId);
        return;
    }
    if (!CaptureSession(it->second))
        return;

    RetainState(it->second, formId, 0, acMessage.IsLocked ? 1 : 0,
                acMessage.IsLocked ? static_cast<std::int32_t>(acMessage.LockLevel) : -1,
                0.0F, 0.0F, 0.0F);
    Reconcile();
}
catch (...)
{
}

void VRWorldReplicationService::OnNewPackage(const NotifyNewPackage& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(acMessage.ActorId,
            GameplayBridge::GameplayDomain::Dialogue, GameplayBridge::GameplayAction::Package, command))
        return;
    command.Payload.ApplyGameplayAction.LocalFormIdA = ToLocalForm(m_world, acMessage.PackageId);
    if (command.Payload.ApplyGameplayAction.LocalFormIdA != 0)
        SubmitRemoteCommand(command);
}

void VRWorldReplicationService::OnObjectInventory(const NotifyObjectInventoryChanges& acMessage) noexcept
{
    for (const auto& [objectId, inventory] : acMessage.Changes)
    {
        TP_UNUSED(QueueWorldInventoryTransaction(objectId, inventory, false));
    }
}

void VRWorldReplicationService::OnPlayerDialogue(const NotifyPlayerDialogue& acMessage) noexcept try
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Dialogue,
                           GameplayBridge::GameplayAction::Dialogue, command))
        return;
    std::string text{acMessage.Name.c_str()};
    text += ": ";
    text += acMessage.Text.c_str();
    SubmitText(command, m_nextTextId++, text);
}
catch (...)
{
}

void VRWorldReplicationService::OnQuestUpdate(const NotifyQuestUpdate& acMessage) noexcept
{
    if (!acMessage.IsValid())
        return;

    if (!m_latestQuestRevisionByOwner.contains(acMessage.OwnerPlayerId) &&
        m_latestQuestRevisionByOwner.size() >= kMaximumPendingCanonicalResyncs) {
        RecordCanonicalRecoveryDiagnostic("quest owner revision ledger reached its bounded capacity");
        return;
    }

    const auto committed = m_lastQuestSnapshotRevisionByOwner.find(acMessage.OwnerPlayerId);
    if (committed != m_lastQuestSnapshotRevisionByOwner.end() &&
        acMessage.CanonicalRevision <= committed->second)
        return;

    auto& latestRevision = m_latestQuestRevisionByOwner[acMessage.OwnerPlayerId];
    if (acMessage.CanonicalRevision <= latestRevision)
        return;
    latestRevision = acMessage.CanonicalRevision;

    // The update is newer than an in-flight snapshot. Let the native result
    // of the older work fail closed and request this owner's current log.
    if (const auto pending = m_pendingQuestResyncs.find(acMessage.OwnerPlayerId);
        pending != m_pendingQuestResyncs.end() && pending->second.Applying &&
        SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy::DoesQuestUpdateSupersedeSnapshot(
            pending->second.OwnerPlayerId, pending->second.ApplyingRevision,
            acMessage.OwnerPlayerId, acMessage.CanonicalRevision))
        m_pendingQuestResyncs.erase(pending);

    const auto action = acMessage.Status == NotifyQuestUpdate::StageUpdate ?
        GameplayBridge::GameplayAction::SetQuestStage : GameplayBridge::GameplayAction::SetQuestState;
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Quest, action, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = ToLocalForm(m_world, acMessage.Id);
    payload.ValueA = acMessage.Stage;
    payload.ValueB = acMessage.Status;
    payload.ActionFlags = acMessage.ClientQuestType;
    if (payload.TargetLocalFormId == 0 ||
        !SubmitRemoteCommand(command, CanonicalRecoveryOperation::Quest, 0,
                             acMessage.CanonicalRevision, 0, acMessage.OwnerPlayerId, acMessage.Id))
        RequestQuestResync(acMessage.OwnerPlayerId);
}

std::uint32_t VRWorldReplicationService::FindObjectServerId(const GameId& acObjectId) const noexcept
{
    const auto found = m_objectServerIds.find(acObjectId);
    return found != m_objectServerIds.end() ? found->second : 0;
}

void VRWorldReplicationService::RequestObjectResync(const std::uint32_t aServerId) noexcept
{
    if (aServerId == 0 || m_pendingObjectResyncs.contains(aServerId) ||
        m_pendingObjectResyncs.size() >= kMaximumPendingCanonicalResyncs)
        return;

    auto& pending = m_pendingObjectResyncs[aServerId];
    pending.RequestId = m_nextCanonicalResyncRequestId++;
    if (m_nextCanonicalResyncRequestId == 0)
        m_nextCanonicalResyncRequestId = 1;
    if (pending.RequestId == 0)
        pending.RequestId = m_nextCanonicalResyncRequestId++;
    const auto known = m_lastObjectSnapshotRevisionByServer.find(aServerId);
    pending.KnownRevision = known != m_lastObjectSnapshotRevisionByServer.end() ? known->second : 0;
    TP_UNUSED(SendObjectResyncRequest(aServerId, pending));
}

void VRWorldReplicationService::RequestQuestResync(const std::uint32_t aOwnerPlayerId) noexcept
{
    if (aOwnerPlayerId == 0 || m_pendingQuestResyncs.contains(aOwnerPlayerId))
        return;
    if (m_pendingQuestResyncs.size() >= kMaximumPendingCanonicalResyncs) {
        RecordCanonicalRecoveryDiagnostic("quest canonical resync ledger reached its bounded capacity");
        return;
    }

    auto& pending = m_pendingQuestResyncs[aOwnerPlayerId];
    pending.OwnerPlayerId = aOwnerPlayerId;
    pending.RequestId = SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy::NextNonZeroRequestId(
        m_nextCanonicalResyncRequestId);
    const auto known = m_lastQuestSnapshotRevisionByOwner.find(aOwnerPlayerId);
    pending.KnownRevision = known != m_lastQuestSnapshotRevisionByOwner.end() ? known->second : 0;
    TP_UNUSED(SendQuestResyncRequest(pending));
}

bool VRWorldReplicationService::SendObjectResyncRequest(
    const std::uint32_t aServerId, PendingCanonicalResync& arPending) noexcept
{
    if (aServerId == 0 || arPending.RequestId == 0 || !m_transport.IsOnline() ||
        m_transport.IsGameplayCleanupRequired() ||
        !SkyrimTogether::Protocol::HasCapability(
            m_transport.GetNegotiatedGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::RevisionedCanonicalRecovery))
        return false;

    RequestObjectResync request{};
    request.ServerId = aServerId;
    request.RequestId = arPending.RequestId;
    request.KnownRevision = arPending.KnownRevision;
    if (!m_transport.Send(request))
        return false;
    ++arPending.Attempts;
    arPending.RetryElapsed = 0.0;
    arPending.RequestSent = true;
    return true;
}

bool VRWorldReplicationService::SendQuestResyncRequest(PendingQuestResync& arPending) noexcept
{
    if (arPending.OwnerPlayerId == 0 || arPending.RequestId == 0 || m_transport.GetLocalPlayerId() == 0 || !m_transport.IsOnline() ||
        m_transport.IsGameplayCleanupRequired() ||
        !SkyrimTogether::Protocol::HasCapability(
            m_transport.GetNegotiatedGameplayCapabilities(),
            SkyrimTogether::Protocol::GameplayCapability::RevisionedCanonicalRecovery))
        return false;

    const auto serverInstanceNonce = m_transport.GetServerInstanceNonce();
    const auto connectionGeneration = m_transport.GetConnectionGeneration();
    const auto lifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (serverInstanceNonce == 0 || connectionGeneration == 0 || lifecycleEpoch == 0)
        return false;

    RequestQuestResync request{};
    request.OwnerPlayerId = arPending.OwnerPlayerId;
    request.RequestId = arPending.RequestId;
    request.KnownRevision = arPending.KnownRevision;
    if (!m_transport.Send(request))
        return false;
    arPending.ServerInstanceNonce = serverInstanceNonce;
    arPending.ConnectionGeneration = connectionGeneration;
    arPending.LifecycleEpoch = lifecycleEpoch;
    ++arPending.Attempts;
    arPending.RetryElapsed = 0.0;
    arPending.RequestSent = true;
    return true;
}

bool VRWorldReplicationService::IsQuestRecoveryCurrent(const PendingQuestResync& acPending) const noexcept
{
    return acPending.OwnerPlayerId != 0 && acPending.ServerInstanceNonce != 0 &&
           acPending.ConnectionGeneration != 0 && acPending.LifecycleEpoch != 0 &&
           m_transport.IsOnline() &&
           acPending.ServerInstanceNonce == m_transport.GetServerInstanceNonce() &&
           acPending.ConnectionGeneration == m_transport.GetConnectionGeneration() &&
           acPending.LifecycleEpoch == SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
}

void VRWorldReplicationService::OnObjectResync(const NotifyObjectResync& acMessage) noexcept
{
    if (!acMessage.IsDecodedValid || !acMessage.IsValid())
        return;
    const auto pending = m_pendingObjectResyncs.find(acMessage.ServerId);
    if (pending == m_pendingObjectResyncs.end() || pending->second.Applying || !pending->second.RequestSent ||
        pending->second.RequestId != acMessage.RequestId ||
        acMessage.CanonicalRevision < pending->second.KnownRevision ||
        FindObjectServerId(acMessage.Snapshot.Id) != acMessage.ServerId)
        return;
    const auto applied = m_lastObjectSnapshotRevisionByServer.find(acMessage.ServerId);
    if (applied != m_lastObjectSnapshotRevisionByServer.end() &&
        acMessage.CanonicalRevision <= applied->second)
        return;

    const auto localFormId = ToLocalForm(m_world, acMessage.Snapshot.Id);
    if (localFormId == 0)
        return;

    auto& recovery = pending->second;
    recovery.Applying = true;
    recovery.ApplyingRevision = acMessage.CanonicalRevision;
    recovery.PendingApplyMask = kObjectRecoveryInventory | kObjectRecoveryLock |
        (acMessage.Snapshot.HasCurrentOpenState ? kObjectRecoveryOpenState : 0);
    if (!QueueWorldInventoryTransaction(acMessage.Snapshot.Id, acMessage.Snapshot.CurrentInventory, true,
                                        acMessage.ServerId, acMessage.CanonicalRevision))
        return;

    GameplayBridge::CommandRecord lock{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Object,
                           GameplayBridge::GameplayAction::SetLockState, lock)) {
        FailObjectRecovery(acMessage.ServerId, acMessage.CanonicalRevision);
        return;
    }
    auto& payload = lock.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = localFormId;
    payload.ValueA = acMessage.Snapshot.CurrentLockData.IsLocked ? 1 : 0;
    payload.ValueB = acMessage.Snapshot.CurrentLockData.IsLocked ?
        static_cast<std::int32_t>(acMessage.Snapshot.CurrentLockData.LockLevel) : -1;
    if (!SubmitRemoteCommand(lock, CanonicalRecoveryOperation::ObjectLock,
                             acMessage.ServerId, acMessage.CanonicalRevision)) {
        FailObjectRecovery(acMessage.ServerId, acMessage.CanonicalRevision);
        return;
    }

    if (acMessage.Snapshot.HasCurrentOpenState) {
        GameplayBridge::CommandRecord openState{};
        if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Object,
                               GameplayBridge::GameplayAction::SetOpenState, openState)) {
            FailObjectRecovery(acMessage.ServerId, acMessage.CanonicalRevision);
            return;
        }
        auto& openPayload = openState.Payload.ApplyGameplayAction;
        openPayload.TargetLocalFormId = localFormId;
        openPayload.ValueA = acMessage.Snapshot.CurrentOpenState == 1 ||
                                     acMessage.Snapshot.CurrentOpenState == 2 ? 1 : 0;
        if (!SubmitRemoteCommand(openState, CanonicalRecoveryOperation::ObjectOpenState,
                                 acMessage.ServerId, acMessage.CanonicalRevision,
                                 acMessage.Snapshot.CurrentOpenState))
            FailObjectRecovery(acMessage.ServerId, acMessage.CanonicalRevision);
    }
}

void VRWorldReplicationService::OnQuestResync(const NotifyQuestResync& acMessage) noexcept try
{
    if (!acMessage.IsDecodedValid || !acMessage.IsValid())
        return;
    const auto pending = m_pendingQuestResyncs.find(acMessage.OwnerPlayerId);
    if (pending == m_pendingQuestResyncs.end() || pending->second.Applying || !pending->second.RequestSent ||
        !IsQuestRecoveryCurrent(pending->second))
        return;

    const auto committed = m_lastQuestSnapshotRevisionByOwner.find(acMessage.OwnerPlayerId);
    const auto latest = m_latestQuestRevisionByOwner.find(acMessage.OwnerPlayerId);
    if (!SkyrimTogether::Protocol::RevisionedCanonicalRecoveryPolicy::CanCommitQuestSnapshot(
            pending->second.OwnerPlayerId, acMessage.OwnerPlayerId, pending->second.RequestId,
            acMessage.RequestId, pending->second.KnownRevision,
            committed != m_lastQuestSnapshotRevisionByOwner.end() ? committed->second : 0,
            latest != m_latestQuestRevisionByOwner.end() ? latest->second : 0,
            acMessage.CanonicalRevision))
        return;

    std::unordered_map<GameId, std::uint16_t> snapshotStages;
    snapshotStages.reserve(acMessage.Snapshot.Entries.size());
    for (const auto& entry : acMessage.Snapshot.Entries) {
        if (ToLocalForm(m_world, entry.Id) == 0)
            return;
        snapshotStages.emplace(entry.Id, entry.Stage);
    }

    std::vector<QuestRecoveryEntry> entries;
    const auto canonical = m_canonicalQuestStagesByOwner.find(acMessage.OwnerPlayerId);
    const auto canonicalCount = canonical != m_canonicalQuestStagesByOwner.end() ? canonical->second.size() : 0;
    entries.reserve(canonicalCount + snapshotStages.size());
    // Only remove quests this recovery lane previously established. The
    // canonical snapshot is complete for that lane, not authority over local-only quests.
    if (canonical != m_canonicalQuestStagesByOwner.end()) {
        for (const auto& [id, stage] : canonical->second) {
            if (!snapshotStages.contains(id)) {
                if (ToLocalForm(m_world, id) == 0)
                    return;
                entries.push_back({id, stage, true});
            }
        }
    }
    for (const auto& [id, stage] : snapshotStages)
        entries.push_back({id, stage, false});

    auto& recovery = pending->second;
    recovery.Entries = std::move(entries);
    recovery.SnapshotStages = std::move(snapshotStages);
    recovery.NextEntry = 0;
    recovery.Applying = true;
    recovery.ApplyingRevision = acMessage.CanonicalRevision;
    m_latestQuestRevisionByOwner.insert_or_assign(acMessage.OwnerPlayerId, acMessage.CanonicalRevision);
    TryApplyQuestRecovery();
}
catch (...)
{
    const auto pending = m_pendingQuestResyncs.find(acMessage.OwnerPlayerId);
    if (pending != m_pendingQuestResyncs.end())
        FailQuestRecovery(acMessage.OwnerPlayerId, pending->second.ApplyingRevision);
}

void VRWorldReplicationService::TryApplyQuestRecovery() noexcept
{
    auto pending = std::find_if(m_pendingQuestResyncs.begin(), m_pendingQuestResyncs.end(),
                                [](const auto& acEntry) { return acEntry.second.Applying; });
    if (pending == m_pendingQuestResyncs.end())
        return;
    auto& recovery = pending->second;
    if (!IsQuestRecoveryCurrent(recovery)) {
        m_pendingQuestResyncs.erase(pending);
        return;
    }
    if (recovery.NextEntry == recovery.Entries.size()) {
        m_lastQuestSnapshotRevisionByOwner.insert_or_assign(recovery.OwnerPlayerId, recovery.ApplyingRevision);
        m_latestQuestRevisionByOwner.insert_or_assign(recovery.OwnerPlayerId, recovery.ApplyingRevision);
        m_canonicalQuestStagesByOwner.insert_or_assign(recovery.OwnerPlayerId, std::move(recovery.SnapshotStages));
        m_pendingQuestResyncs.erase(pending);
        return;
    }

    const auto& entry = recovery.Entries[recovery.NextEntry];
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Quest,
                           GameplayBridge::GameplayAction::SetQuestState, command)) {
        FailQuestRecovery(recovery.OwnerPlayerId, recovery.ApplyingRevision);
        return;
    }
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = ToLocalForm(m_world, entry.Id);
    payload.ValueA = entry.Stage;
    payload.ValueB = entry.Stop ? NotifyQuestUpdate::Stopped : NotifyQuestUpdate::Started;
    if (payload.TargetLocalFormId == 0 ||
        !SubmitRemoteCommand(command, CanonicalRecoveryOperation::Quest, 0,
                             recovery.ApplyingRevision, 0, recovery.OwnerPlayerId, entry.Id))
        FailQuestRecovery(recovery.OwnerPlayerId, recovery.ApplyingRevision);
}

void VRWorldReplicationService::CompleteObjectRecoveryInventory(
    const std::uint32_t aServerId, const std::uint64_t aRevision, const bool aSucceeded) noexcept
{
    if (aServerId == 0 || aRevision == 0)
        return;
    const auto pending = m_pendingObjectResyncs.find(aServerId);
    if (pending == m_pendingObjectResyncs.end() || !pending->second.Applying ||
        pending->second.ApplyingRevision != aRevision)
        return;
    if (!aSucceeded) {
        FailObjectRecovery(aServerId, aRevision);
        return;
    }

    pending->second.PendingApplyMask &= ~kObjectRecoveryInventory;
    if (pending->second.PendingApplyMask != 0)
        return;
    m_lastObjectSnapshotRevisionByServer.insert_or_assign(aServerId, aRevision);
    m_pendingObjectResyncs.erase(pending);
}

void VRWorldReplicationService::CompleteCanonicalRecoveryCommand(
    const PendingRemoteCommand& acPending, const bool aSucceeded) noexcept
{
    if (acPending.RecoveryOperation == CanonicalRecoveryOperation::Activation) {
        if (!aSucceeded)
            return;
        GameplayBridge::CommandRecord correction{};
        if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Object,
                               GameplayBridge::GameplayAction::SetOpenState, correction)) {
            RecordCanonicalRecoveryDiagnostic("activation post-state correction could not be constructed");
            return;
        }
        auto& payload = correction.Payload.ApplyGameplayAction;
        payload.TargetLocalFormId = acPending.Command.Payload.ApplyGameplayAction.TargetLocalFormId;
        payload.ValueA = acPending.AuthoritativeOpenState == 1 || acPending.AuthoritativeOpenState == 2 ? 1 : 0;
        if (payload.TargetLocalFormId == 0 ||
            !SubmitRemoteCommand(correction, CanonicalRecoveryOperation::ActivationPostState, 0, 0,
                                 acPending.AuthoritativeOpenState))
            RecordCanonicalRecoveryDiagnostic("activation post-state correction could not be admitted");
        return;
    }

    if (acPending.RecoveryOperation == CanonicalRecoveryOperation::ActivationPostState) {
        if (!aSucceeded)
            RecordCanonicalRecoveryDiagnostic("activation post-state correction did not reach a stable native state");
        return;
    }

    if (acPending.RecoveryOperation == CanonicalRecoveryOperation::ObjectLock) {
        const auto pending = m_pendingObjectResyncs.find(acPending.CanonicalServerId);
        if (pending == m_pendingObjectResyncs.end() || !pending->second.Applying ||
            pending->second.ApplyingRevision != acPending.CanonicalRevision)
            return;
        if (!aSucceeded) {
            FailObjectRecovery(acPending.CanonicalServerId, acPending.CanonicalRevision);
            return;
        }

        pending->second.PendingApplyMask &= ~kObjectRecoveryLock;
        if (pending->second.PendingApplyMask != 0)
            return;
        m_lastObjectSnapshotRevisionByServer.insert_or_assign(acPending.CanonicalServerId,
                                                               acPending.CanonicalRevision);
        m_pendingObjectResyncs.erase(pending);
        return;
    }

    if (acPending.RecoveryOperation == CanonicalRecoveryOperation::ObjectOpenState) {
        const auto pending = m_pendingObjectResyncs.find(acPending.CanonicalServerId);
        if (pending == m_pendingObjectResyncs.end() || !pending->second.Applying ||
            pending->second.ApplyingRevision != acPending.CanonicalRevision)
            return;
        if (!aSucceeded) {
            FailObjectRecovery(acPending.CanonicalServerId, acPending.CanonicalRevision);
            return;
        }

        pending->second.PendingApplyMask &= ~kObjectRecoveryOpenState;
        if (pending->second.PendingApplyMask != 0)
            return;
        m_lastObjectSnapshotRevisionByServer.insert_or_assign(acPending.CanonicalServerId,
                                                               acPending.CanonicalRevision);
        m_pendingObjectResyncs.erase(pending);
        return;
    }

    if (acPending.RecoveryOperation != CanonicalRecoveryOperation::Quest ||
        acPending.CanonicalOwnerPlayerId == 0)
        return;

    const auto latest = m_latestQuestRevisionByOwner.find(acPending.CanonicalOwnerPlayerId);
    if (latest == m_latestQuestRevisionByOwner.end() || latest->second != acPending.CanonicalRevision) {
        // A late native result may have overwritten a newer owner update.
        RequestQuestResync(acPending.CanonicalOwnerPlayerId);
        return;
    }

    const auto pending = m_pendingQuestResyncs.find(acPending.CanonicalOwnerPlayerId);
    if (pending != m_pendingQuestResyncs.end() && pending->second.Applying &&
        pending->second.ApplyingRevision == acPending.CanonicalRevision) {
        if (!aSucceeded) {
            FailQuestRecovery(acPending.CanonicalOwnerPlayerId, acPending.CanonicalRevision);
            return;
        }
        ++pending->second.NextEntry;
        TryApplyQuestRecovery();
        return;
    }

    if (!aSucceeded) {
        RequestQuestResync(acPending.CanonicalOwnerPlayerId);
        return;
    }

    if (!acPending.CanonicalQuestId) {
        RequestQuestResync(acPending.CanonicalOwnerPlayerId);
        return;
    }
    auto& stages = m_canonicalQuestStagesByOwner[acPending.CanonicalOwnerPlayerId];
    const auto& payload = acPending.Command.Payload.ApplyGameplayAction;
    if (payload.ValueB == NotifyQuestUpdate::Stopped)
        stages.erase(acPending.CanonicalQuestId);
    else
        stages.insert_or_assign(acPending.CanonicalQuestId, static_cast<std::uint16_t>(payload.ValueA));
    m_lastQuestSnapshotRevisionByOwner.insert_or_assign(acPending.CanonicalOwnerPlayerId,
                                                         acPending.CanonicalRevision);
}

void VRWorldReplicationService::FailObjectRecovery(
    const std::uint32_t aServerId, const std::uint64_t aRevision) noexcept
{
    const auto pending = m_pendingObjectResyncs.find(aServerId);
    if (pending == m_pendingObjectResyncs.end() || !pending->second.Applying ||
        pending->second.ApplyingRevision != aRevision)
        return;
    pending->second.Applying = false;
    pending->second.PendingApplyMask = 0;
    pending->second.ApplyingRevision = 0;
    pending->second.RetryElapsed = 0.0;
    pending->second.RequestSent = false;
    RecordCanonicalRecoveryDiagnostic("object canonical recovery application was ambiguous or failed");
}

void VRWorldReplicationService::FailQuestRecovery(
    const std::uint32_t aOwnerPlayerId, const std::uint64_t aRevision) noexcept
{
    const auto pending = m_pendingQuestResyncs.find(aOwnerPlayerId);
    if (pending == m_pendingQuestResyncs.end() || !pending->second.Applying ||
        pending->second.ApplyingRevision != aRevision)
        return;
    pending->second.Entries.clear();
    pending->second.SnapshotStages.clear();
    pending->second.NextEntry = 0;
    pending->second.Applying = false;
    pending->second.ApplyingRevision = 0;
    pending->second.RetryElapsed = 0.0;
    pending->second.RequestSent = false;
    RecordCanonicalRecoveryDiagnostic("quest canonical recovery did not observe the requested native completion");
}

void VRWorldReplicationService::RecordCanonicalRecoveryDiagnostic(const char* const apReason) noexcept
{
    if (m_canonicalRecoveryDiagnosticCount != (std::numeric_limits<std::uint32_t>::max)())
        ++m_canonicalRecoveryDiagnosticCount;
    const auto count = m_canonicalRecoveryDiagnosticCount;
    if (count != 0 && (count & (count - 1)) == 0)
        spdlog::warn("VR canonical recovery: {} (aggregate count {})", apReason, count);
}

void VRWorldReplicationService::RetryCanonicalResyncs(const double aDelta) noexcept
{
    if (!std::isfinite(aDelta) || aDelta < 0.0)
        return;
    const auto delta = std::min(aDelta, kCanonicalResyncRetrySeconds);
    for (auto pending = m_pendingObjectResyncs.begin(); pending != m_pendingObjectResyncs.end();) {
        if (pending->second.Applying) {
            ++pending;
            continue;
        }
        pending->second.RetryElapsed += delta;
        if (pending->second.RetryElapsed < kCanonicalResyncRetrySeconds) {
            ++pending;
            continue;
        }
        if (pending->second.Attempts >= kMaximumCanonicalResyncAttempts) {
            RecordCanonicalRecoveryDiagnostic("object canonical resync request budget exhausted");
            pending = m_pendingObjectResyncs.erase(pending);
            continue;
        }
        if (!SendObjectResyncRequest(pending->first, pending->second))
            pending->second.RetryElapsed = 0.0;
        ++pending;
    }

    for (auto pending = m_pendingQuestResyncs.begin(); pending != m_pendingQuestResyncs.end();) {
        auto& recovery = pending->second;
        if (!IsQuestRecoveryCurrent(recovery)) {
            pending = m_pendingQuestResyncs.erase(pending);
            continue;
        }
        if (recovery.Applying) {
            ++pending;
            continue;
        }
        recovery.RetryElapsed += delta;
        if (recovery.RetryElapsed < kCanonicalResyncRetrySeconds) {
            ++pending;
            continue;
        }
        if (recovery.Attempts >= kMaximumCanonicalResyncAttempts) {
            RecordCanonicalRecoveryDiagnostic("quest canonical resync request budget exhausted");
            pending = m_pendingQuestResyncs.erase(pending);
            continue;
        }
        if (!SendQuestResyncRequest(recovery))
            recovery.RetryElapsed = 0.0;
        ++pending;
    }
}

void VRWorldReplicationService::ResetCanonicalRecovery() noexcept
{
    m_objectServerIds.clear();
    m_lastObjectSnapshotRevisionByServer.clear();
    m_pendingObjectResyncs.clear();
    m_pendingQuestResyncs.clear();
    m_lastQuestSnapshotRevisionByOwner.clear();
    m_latestQuestRevisionByOwner.clear();
    m_canonicalQuestStagesByOwner.clear();
    m_nextCanonicalResyncRequestId = 1;
    m_canonicalRecoveryDiagnosticCount = 0;
}

void VRWorldReplicationService::OnRemoveWaypoint(const NotifyRemoveWaypoint&) noexcept
{
    m_waypointEcho = {};
    m_waypointEcho.Valid = true;
    m_waypointEcho.Remove = true;
    m_waypointEcho.Remaining = kWaypointEchoSuppressionSeconds;
    GameplayBridge::CommandRecord command{};
    if (BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Party,
                          GameplayBridge::GameplayAction::RemoveWaypoint, command))
        SubmitRemoteCommand(command);
}

void VRWorldReplicationService::OnScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept try
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Object,
                           GameplayBridge::GameplayAction::ScriptAnimation, command) || acMessage.FormID == 0)
        return;
    command.Payload.ApplyGameplayAction.TargetLocalFormId = acMessage.FormID;
    std::string text;
    if (!acMessage.Animation.empty()) {
        text = acMessage.Animation.c_str();
        text += '\n';
    }
    text += acMessage.EventName.c_str();
    SubmitText(command, m_nextTextId++, text);
}
catch (...)
{
}

void VRWorldReplicationService::OnSetWaypoint(const NotifySetWaypoint& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Party,
                           GameplayBridge::GameplayAction::SetWaypoint, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.LocalFormIdA = ToLocalForm(m_world, acMessage.WorldSpaceFormID);
    payload.ScalarA = acMessage.Position.x;
    payload.ScalarB = acMessage.Position.y;
    payload.ScalarC = acMessage.Position.z;
    if (payload.LocalFormIdA != 0)
    {
        m_waypointEcho.LocalWorldspaceFormId = payload.LocalFormIdA;
        m_waypointEcho.PositionX = payload.ScalarA;
        m_waypointEcho.PositionY = payload.ScalarB;
        m_waypointEcho.PositionZ = payload.ScalarC;
        m_waypointEcho.Remaining = kWaypointEchoSuppressionSeconds;
        m_waypointEcho.Remove = false;
        m_waypointEcho.Valid = true;
        SubmitRemoteCommand(command);
    }
}

void VRWorldReplicationService::OnSubtitle(const NotifySubtitle& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(acMessage.ServerId,
            GameplayBridge::GameplayDomain::Dialogue, GameplayBridge::GameplayAction::Subtitle, command))
        return;
    command.Payload.ApplyGameplayAction.LocalFormIdA = acMessage.TopicFormId;
    SubmitText(command, m_nextTextId++, acMessage.Text);
}

void VRWorldReplicationService::OnTeleport(const NotifyTeleport& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::WorldState,
                           GameplayBridge::GameplayAction::Teleport, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.LocalFormIdA = ToLocalForm(m_world, acMessage.CellId);
    payload.LocalFormIdB = ToLocalForm(m_world, acMessage.WorldSpaceId);
    payload.ScalarA = acMessage.Position.x;
    payload.ScalarB = acMessage.Position.y;
    payload.ScalarC = acMessage.Position.z;
    if (payload.LocalFormIdA != 0)
        SubmitRemoteCommand(command);
}

void VRWorldReplicationService::OnTeleportCommand(const TeleportCommandResponse& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::WorldState,
                           GameplayBridge::GameplayAction::Teleport, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.LocalFormIdA = ToLocalForm(m_world, acMessage.CellId);
    payload.LocalFormIdB = ToLocalForm(m_world, acMessage.WorldSpaceId);
    payload.ScalarA = acMessage.Position.x;
    payload.ScalarB = acMessage.Position.y;
    payload.ScalarC = acMessage.Position.z;
    if (payload.LocalFormIdA != 0)
        SubmitRemoteCommand(command);
}

void VRWorldReplicationService::OnTimeSettings(const ServerTimeSettings& acMessage) noexcept
{
    const auto& time = acMessage.timeModel;
    if (!std::isfinite(time.Time) || !std::isfinite(time.TimeScale) || time.Time < 0.0f || time.Time >= 24.0f ||
        time.TimeScale < 0.0F || time.TimeScale > 1000.0F)
        return;
    if (!CaptureSession(m_calendarState))
        return;
    const bool syncDate = m_world.GetServerSettings().SyncPlayerCalendar;
    if (syncDate && (time.Year > 999 || time.Month >= 12 || time.Day == 0 || time.Day > 31))
        return;
    RetainState(m_calendarState, 0, 0, syncDate ? static_cast<std::int32_t>(time.Year) : 0,
                syncDate ? static_cast<std::int32_t>(time.Month) : 0,
                syncDate ? static_cast<float>(time.Day) : 0.0F, time.Time, time.TimeScale,
                syncDate ? 0u : GameplayBridge::kPreserveCalendarDate);
    Reconcile();
}

void VRWorldReplicationService::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    const auto formId = ToLocalForm(m_world, acMessage.Id);
    if (formId == 0 || !CaptureSession(m_weatherState))
        return;
    RetainState(m_weatherState, 0, formId, 0, 0, 0.0F, 0.0F, 0.0F);
    Reconcile();
}

void VRWorldReplicationService::OnServerSettings(const ServerSettings& acSettings) noexcept
{
    RetainServerSettings(acSettings);
    Reconcile();
}

void VRWorldReplicationService::OnPartyJoined(const PartyJoinedEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline())
        return;
    m_partyRoleKnown = true;
    m_partyLeader = acEvent.IsLeader;
    m_pendingQuestResyncs.erase(m_transport.GetLocalPlayerId());
    RequestQuestResync(m_transport.GetLocalPlayerId());
    RetainServerSettings(m_world.GetServerSettings());
    if (acEvent.IsLeader)
        SubmitReleaseWeather();
    else
        TP_UNUSED(SendOutbound(RequestCurrentWeather{}, 0, 0));
}

void VRWorldReplicationService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    m_partyRoleKnown = false;
    m_partyLeader = false;
    RetainServerSettings(m_world.GetServerSettings());
    SubmitReleaseWeather();
}

void VRWorldReplicationService::OnPartyInfo(const NotifyPartyInfo& acMessage) noexcept
{
    if (!m_transport.IsOnline() || !m_world.GetPartyService().IsInParty() ||
        (m_partyRoleKnown && m_partyLeader == acMessage.IsLeader))
        return;
    m_partyRoleKnown = true;
    m_partyLeader = acMessage.IsLeader;
    RetainServerSettings(m_world.GetServerSettings());
    if (m_partyLeader)
        SubmitReleaseWeather();
    else
        TP_UNUSED(SendOutbound(RequestCurrentWeather{}, 0, 0));
}

void VRWorldReplicationService::OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline() || !m_world.GetPartyService().IsInParty() || acEvent.Text.empty() ||
        acEvent.Text.size() > 1024)
        return;
    PlayerDialogueRequest request{};
    request.Text = acEvent.Text;
    TP_UNUSED(SendOutbound(std::move(request), 0, 0));
}

void VRWorldReplicationService::OnLocalGameplay(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept try
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayTextChunk))
    {
        OnLocalGameplayText(record);
        return;
    }
    const auto& payload = record.Payload.LocalGameplayAction;
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(payload.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(payload.Action);
    const auto domainIndex = static_cast<std::size_t>(payload.Domain);
    if (!m_transport.IsOnline() ||
        record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayAction) ||
        record.Header.PayloadSize != GameplayBridge::kFixedPayloadBytes || record.Header.Flags != 0 ||
        record.Header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        record.Header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        record.Header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() ||
        record.Header.Identity.EntityId != 0 || record.Header.Identity.EntityGeneration != 0 ||
        record.Header.Identity.SequenceId != 0 || record.Header.Identity.ActionId == 0 ||
        payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value ||
        payload.TargetLocalFormId == 0 || payload.SecondaryHandle.Value != 0 || payload.Reserved0 != 0 ||
        !IsZeroBytes(payload.ReservedTail, sizeof(payload.ReservedTail)) ||
        !std::isfinite(payload.ScalarA) || !std::isfinite(payload.ScalarB) ||
        !std::isfinite(payload.ScalarC) || !std::isfinite(payload.ScalarD) ||
        domainIndex >= m_lastLocalActionIdByDomain.size() ||
        record.Header.Identity.ActionId <= m_lastLocalActionIdByDomain[domainIndex])
        return;

    if (domain == GameplayBridge::GameplayDomain::WorldState &&
        action == GameplayBridge::GameplayAction::SetWeather)
    {
        if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;
        if (!m_world.GetPartyService().IsInParty() || !m_world.GetPartyService().IsLeader())
        {
            m_lastLocalActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
            return;
        }
        RequestWeatherChange request{};
        if (m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.Id) && request.Id)
            TP_UNUSED(SendOutbound(std::move(request), domainIndex, record.Header.Identity.ActionId));
        return;
    }

    if (domain != GameplayBridge::GameplayDomain::Party ||
        (action != GameplayBridge::GameplayAction::SetWaypoint &&
         action != GameplayBridge::GameplayAction::RemoveWaypoint) ||
        !m_world.GetPartyService().IsInParty())
        return;

    if (action == GameplayBridge::GameplayAction::RemoveWaypoint)
    {
        if (payload.LocalFormIdA != 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
            payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
            payload.ScalarA != 0.0F || payload.ScalarB != 0.0F || payload.ScalarC != 0.0F ||
            payload.ScalarD != 0.0F || payload.ActionFlags != 0)
            return;
        if (m_waypointEcho.Valid && m_waypointEcho.Remove) {
            m_waypointEcho = {};
            m_lastLocalActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
            return;
        }
        TP_UNUSED(SendOutbound(RequestRemoveWaypoint{}, domainIndex, record.Header.Identity.ActionId));
        return;
    }

    if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
        payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
        std::abs(payload.ScalarA) > kMaximumWorldPosition ||
        std::abs(payload.ScalarB) > kMaximumWorldPosition ||
        std::abs(payload.ScalarC) > kMaximumWorldPosition || payload.ScalarD != 0.0F ||
        payload.ActionFlags != 0)
        return;
    if (m_waypointEcho.Valid && !m_waypointEcho.Remove &&
        m_waypointEcho.LocalWorldspaceFormId == payload.LocalFormIdA &&
        std::abs(m_waypointEcho.PositionX - payload.ScalarA) <= kWaypointPositionTolerance &&
        std::abs(m_waypointEcho.PositionY - payload.ScalarB) <= kWaypointPositionTolerance &&
        std::abs(m_waypointEcho.PositionZ - payload.ScalarC) <= kWaypointPositionTolerance) {
        m_waypointEcho = {};
        m_lastLocalActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
        return;
    }
    RequestSetWaypoint request{};
    request.Position = {payload.ScalarA, payload.ScalarB, payload.ScalarC};
    if (m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.WorldSpaceFormID) &&
        request.WorldSpaceFormID)
        TP_UNUSED(SendOutbound(std::move(request), domainIndex, record.Header.Identity.ActionId));
}
catch (...)
{
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRWorldReplicationService::OnLocalGameplayText(const GameplayBridge::EventRecord& acRecord) noexcept try
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
    if (payload.Action == static_cast<std::uint16_t>(GameplayBridge::GameplayAction::Subtitle)) {
        OnLocalSubtitleText(acRecord);
        return;
    }
    constexpr auto domain = GameplayBridge::GameplayDomain::Dialogue;
    const auto domainIndex = static_cast<std::size_t>(domain);
    if (!m_transport.IsOnline() || !m_world.GetPartyService().IsInParty() ||
        header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayTextChunk) ||
        header.PayloadSize != GameplayBridge::kFixedPayloadBytes || header.Flags != 0 ||
        header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() ||
        header.Identity.EntityId != 0 || header.Identity.EntityGeneration != 0 ||
        header.Identity.SequenceId != 0 || header.Identity.ActionId == 0 ||
        payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value ||
        payload.TargetLocalFormId == 0 ||
        payload.Domain != static_cast<std::uint16_t>(domain) ||
        payload.Action != static_cast<std::uint16_t>(GameplayBridge::GameplayAction::Dialogue) ||
        payload.TextId == 0 || payload.ChunkCount == 0 ||
        payload.ChunkCount > GameplayBridge::kMaximumGameplayTextChunks ||
        payload.ChunkIndex >= payload.ChunkCount ||
        payload.ByteCount > GameplayBridge::kGameplayTextBytesPerChunk || payload.Reserved0 != 0 ||
        payload.AuxiliaryLocalFormId != 0 ||
        !IsZeroBytes(payload.Utf8Bytes + payload.ByteCount,
                     GameplayBridge::kGameplayTextBytesPerChunk - payload.ByteCount) ||
        header.Identity.ActionId <= m_lastLocalActionIdByDomain[domainIndex])
        return;

    if (m_dialogueText.ActionId == header.Identity.ActionId &&
        m_dialogueText.TextId == payload.TextId &&
        m_dialogueText.ChunkCount == payload.ChunkCount &&
        m_dialogueText.TargetLocalFormId != payload.TargetLocalFormId)
        return;

    if (m_dialogueText.ActionId != header.Identity.ActionId || m_dialogueText.TextId != payload.TextId ||
        m_dialogueText.ChunkCount != payload.ChunkCount)
    {
        m_dialogueText = {};
        m_dialogueText.ActionId = header.Identity.ActionId;
        m_dialogueText.TextId = payload.TextId;
        m_dialogueText.TargetLocalFormId = payload.TargetLocalFormId;
        m_dialogueText.ChunkCount = payload.ChunkCount;
    }

    const auto offset = static_cast<std::size_t>(payload.ChunkIndex) * GameplayBridge::kGameplayTextBytesPerChunk;
    std::memcpy(m_dialogueText.Bytes.data() + offset, payload.Utf8Bytes, payload.ByteCount);
    m_dialogueText.Lengths[payload.ChunkIndex] = payload.ByteCount;
    m_dialogueText.ReceivedMask |= static_cast<std::uint16_t>(1u << payload.ChunkIndex);
    const auto expectedMask = static_cast<std::uint16_t>((1u << payload.ChunkCount) - 1u);
    if (m_dialogueText.ReceivedMask != expectedMask)
        return;

    std::size_t textLength{};
    for (std::uint16_t index = 0; index < payload.ChunkCount; ++index)
    {
        if (index + 1 != payload.ChunkCount &&
            m_dialogueText.Lengths[index] != GameplayBridge::kGameplayTextBytesPerChunk)
        {
            m_dialogueText = {};
            return;
        }
        textLength += m_dialogueText.Lengths[index];
    }
    if (textLength == 0 || textLength > 512)
    {
        m_dialogueText = {};
        return;
    }

    const auto targetLocalFormId = m_dialogueText.TargetLocalFormId;
    std::string text(m_dialogueText.Bytes.data(), textLength);
    m_dialogueText = {};

    if (targetLocalFormId == kPlayerReferenceFormId)
    {
        PlayerDialogueRequest request{};
        request.Text.assign(text.data(), text.size());
        TP_UNUSED(SendOutbound(std::move(request), domainIndex, header.Identity.ActionId));
        return;
    }

    if (!IsSafeVoiceResourcePath(text)) {
        m_lastLocalActionIdByDomain[domainIndex] = header.Identity.ActionId;
        return;
    }
    auto localActors = m_world.view<FormIdComponent, LocalComponent>(entt::exclude<ObjectComponent>);
    const auto actorIt = std::find_if(localActors.begin(), localActors.end(), [localActors, targetLocalFormId](const auto aEntity) {
        return localActors.get<FormIdComponent>(aEntity).Id == targetLocalFormId;
    });
    if (actorIt == localActors.end()) {
        m_lastLocalActionIdByDomain[domainIndex] = header.Identity.ActionId;
        return;
    }

    DialogueRequest request{};
    request.ServerId = localActors.get<LocalComponent>(*actorIt).Id;
    request.SoundFilename.assign(text.data(), text.size());
    TP_UNUSED(SendOutbound(std::move(request), domainIndex, header.Identity.ActionId));
}
catch (...)
{
    m_dialogueText = {};
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRWorldReplicationService::OnLocalSubtitleText(
    const GameplayBridge::EventRecord& acRecord) noexcept try
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
    constexpr auto domain = GameplayBridge::GameplayDomain::Dialogue;
    constexpr auto action = GameplayBridge::GameplayAction::Subtitle;
    const auto malformed = !m_transport.IsOnline() ||
        header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayTextChunk) ||
        header.PayloadSize != GameplayBridge::kFixedPayloadBytes || header.Flags != 0 ||
        header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() ||
        header.Identity.EntityId != 0 || header.Identity.EntityGeneration != 0 ||
        header.Identity.SequenceId != 0 || header.Identity.ActionId == 0 ||
        header.Identity.ActionId <= m_lastSubtitleActionId ||
        payload.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value ||
        payload.TargetLocalFormId == 0 || payload.Domain != static_cast<std::uint16_t>(domain) ||
        payload.Action != static_cast<std::uint16_t>(action) || payload.TextId == 0 ||
        payload.ChunkCount == 0 || payload.ChunkCount > GameplayBridge::kMaximumGameplayTextChunks ||
        payload.ChunkIndex >= payload.ChunkCount ||
        payload.ByteCount > GameplayBridge::kGameplayTextBytesPerChunk || payload.Reserved0 != 0 ||
        std::memchr(payload.Utf8Bytes, '\0', payload.ByteCount) != nullptr ||
        !IsZeroBytes(payload.Utf8Bytes + payload.ByteCount,
                     GameplayBridge::kGameplayTextBytesPerChunk - payload.ByteCount);
    if (malformed) {
        m_subtitleText = {};
        return;
    }

    const auto matchesAssembly = [&header, &payload, this] {
        return m_subtitleText.Valid &&
               m_subtitleText.ServerInstanceNonce == header.Identity.ServerInstanceNonce &&
               m_subtitleText.ConnectionGeneration == header.Identity.ConnectionGeneration &&
               m_subtitleText.LifecycleEpoch == header.Identity.LifecycleEpoch &&
               m_subtitleText.ActionId == header.Identity.ActionId &&
               m_subtitleText.TextId == payload.TextId &&
               m_subtitleText.SpeakerLocalFormId == payload.TargetLocalFormId &&
               m_subtitleText.TopicLocalFormId == payload.AuxiliaryLocalFormId &&
               m_subtitleText.ChunkCount == payload.ChunkCount;
    };
    if (!matchesAssembly()) {
        m_subtitleText = {};
        if (payload.ChunkIndex != 0)
            return;
        m_subtitleText.ServerInstanceNonce = header.Identity.ServerInstanceNonce;
        m_subtitleText.ConnectionGeneration = header.Identity.ConnectionGeneration;
        m_subtitleText.LifecycleEpoch = header.Identity.LifecycleEpoch;
        m_subtitleText.ActionId = header.Identity.ActionId;
        m_subtitleText.TextId = payload.TextId;
        m_subtitleText.SpeakerLocalFormId = payload.TargetLocalFormId;
        m_subtitleText.TopicLocalFormId = payload.AuxiliaryLocalFormId;
        m_subtitleText.ChunkCount = payload.ChunkCount;
        m_subtitleText.Remaining = kSubtitleAssemblyExpirySeconds;
        m_subtitleText.Valid = true;
    }

    const auto chunkBit = static_cast<std::uint32_t>(1u << payload.ChunkIndex);
    if (payload.ChunkIndex != m_subtitleText.ReceivedCount ||
        (m_subtitleText.ReceivedMask & chunkBit) != 0) {
        m_subtitleText = {};
        return;
    }

    const auto offset = static_cast<std::size_t>(payload.ChunkIndex) * GameplayBridge::kGameplayTextBytesPerChunk;
    std::memcpy(m_subtitleText.Bytes.data() + offset, payload.Utf8Bytes, payload.ByteCount);
    m_subtitleText.Lengths[payload.ChunkIndex] = payload.ByteCount;
    m_subtitleText.ReceivedMask |= chunkBit;
    ++m_subtitleText.ReceivedCount;
    if (m_subtitleText.ReceivedCount != m_subtitleText.ChunkCount)
        return;

    std::size_t textLength{};
    for (std::uint16_t index = 0; index < m_subtitleText.ChunkCount; ++index) {
        if ((index + 1 != m_subtitleText.ChunkCount &&
             m_subtitleText.Lengths[index] != GameplayBridge::kGameplayTextBytesPerChunk) ||
            textLength > kMaximumSubtitleTextBytes - m_subtitleText.Lengths[index]) {
            m_subtitleText = {};
            return;
        }
        textLength += m_subtitleText.Lengths[index];
    }
    if (textLength == 0) {
        m_subtitleText = {};
        return;
    }

    const auto actionId = m_subtitleText.ActionId;
    const auto speakerLocalFormId = m_subtitleText.SpeakerLocalFormId;
    const auto topicLocalFormId = m_subtitleText.TopicLocalFormId;
    std::string text(m_subtitleText.Bytes.data(), textLength);
    m_subtitleText = {};
    if (text.find('\0') != std::string::npos || !IsValidUtf8(text))
        return;

    const auto serverId = speakerLocalFormId == kPlayerReferenceFormId ?
                              m_avatars.GetLocalServerId() :
                              [&]() noexcept {
                                  const auto* ownership = m_world.ctx().find<VRNpcOwnershipService>();
                                  return ownership ?
                                             ownership->GetServerIdForLocalReference(speakerLocalFormId) : 0u;
                              }();
    if (serverId == 0)
        return;

    SubtitleRequest request{};
    request.ServerId = serverId;
    request.Text.assign(text.data(), text.size());
    request.TopicFormId = topicLocalFormId;
    if (SendOutbound(std::move(request), m_lastLocalActionIdByDomain.size(), 0))
        m_lastSubtitleActionId = actionId;
}
catch (...)
{
    ResetSubtitleTextState();
    TP_UNUSED(m_transport.RetireGameplaySession(GameplayBridge::EpochRetireReason::LifecycleReset));
}

void VRWorldReplicationService::SubmitReleaseWeather() noexcept
{
    GameplayBridge::CommandRecord command{};
    if (BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::WorldState,
                          GameplayBridge::GameplayAction::ReleaseWeather, command))
        SubmitRemoteCommand(command);
}

void VRWorldReplicationService::OnConnected(const ConnectedEvent&) noexcept
{
    ObserveSession();
    RequestQuestResync(m_transport.GetLocalPlayerId());
    Reconcile();
}

void VRWorldReplicationService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_nextTextId = 1;
    m_observedServerInstanceNonce = 0;
    m_observedConnectionGeneration = 0;
    m_observedLifecycleEpoch = 0;
    m_reconcileTimer = 0.0;
    m_textRetryTimer = 0.0;
    m_pendingText.clear();
    m_pendingOutbound.clear();
    m_pendingOutboundServerInstanceNonce = 0;
    m_pendingOutboundConnectionGeneration = 0;
    m_pendingOutboundLifecycleEpoch = 0;
    m_waypointEcho = {};
    m_dialogueText = {};
    ResetSubtitleTextState();
    m_lastLocalActionIdByDomain.fill(0);
    m_partyRoleKnown = false;
    m_partyLeader = false;
    ResetRetainedState();
}

void VRWorldReplicationService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    ObserveSession();
    TrySendPendingOutbound();
    RetryCanonicalResyncs(acEvent.Delta);
    RetryPendingWorldInventoryTransactions(acEvent.Delta);
    RetryPendingRemoteCommands(acEvent.Delta);
    if (m_subtitleText.Valid) {
        m_subtitleText.Remaining -= acEvent.Delta;
        if (!std::isfinite(m_subtitleText.Remaining) || m_subtitleText.Remaining <= 0.0)
            m_subtitleText = {};
    }
    if (m_waypointEcho.Valid)
    {
        m_waypointEcho.Remaining -= acEvent.Delta;
        if (m_waypointEcho.Remaining <= 0.0)
            m_waypointEcho = {};
    }
    if (!m_pendingText.empty())
    {
        m_textRetryTimer -= acEvent.Delta;
        if (m_textRetryTimer <= 0.0)
        {
            RetryPendingText();
            m_textRetryTimer = m_pendingText.empty() ? 0.0 : kReconcileIntervalSeconds;
        }
    }
    m_reconcileTimer += acEvent.Delta;
    if (m_reconcileTimer < kReconcileIntervalSeconds)
        return;
    m_reconcileTimer = 0.0;
    Reconcile();
}

bool VRWorldReplicationService::CaptureSession(RetainedState& arState) const noexcept
{
    if (!m_transport.IsOnline())
        return false;
    const auto nonce = m_transport.GetServerInstanceNonce();
    const auto generation = m_transport.GetConnectionGeneration();
    if (nonce == 0 || generation == 0)
        return false;

    if (arState.Valid && (arState.ServerInstanceNonce != nonce || arState.ConnectionGeneration != generation))
        arState = {};
    arState.ServerInstanceNonce = nonce;
    arState.ConnectionGeneration = generation;
    return true;
}

void VRWorldReplicationService::RetainState(
    RetainedState& arState, const std::uint32_t aTargetLocalFormId, const std::uint32_t aLocalFormIdA,
    const std::int32_t aValueA, const std::int32_t aValueB, const float aScalarA, const float aScalarB,
    const float aScalarC, const std::uint32_t aActionFlags) noexcept
{
    const bool changed = !arState.Valid || arState.TargetLocalFormId != aTargetLocalFormId ||
                         arState.LocalFormIdA != aLocalFormIdA || arState.ValueA != aValueA ||
                         arState.ValueB != aValueB || arState.ScalarA != aScalarA ||
                         arState.ScalarB != aScalarB || arState.ScalarC != aScalarC ||
                         arState.ActionFlags != aActionFlags;
    if (!changed)
        return;

    arState.TargetLocalFormId = aTargetLocalFormId;
    arState.LocalFormIdA = aLocalFormIdA;
    arState.ValueA = aValueA;
    arState.ValueB = aValueB;
    arState.ScalarA = aScalarA;
    arState.ScalarB = aScalarB;
    arState.ScalarC = aScalarC;
    arState.ActionFlags = aActionFlags;
    arState.Valid = true;
    arState.Dirty = true;
    arState.Attempts = 0;
    ++arState.Version;
}

void VRWorldReplicationService::ObserveSession() noexcept
{
    if (!m_transport.IsOnline())
        return;

    const auto nonce = m_transport.GetServerInstanceNonce();
    const auto generation = m_transport.GetConnectionGeneration();
    if (nonce == 0 || generation == 0)
        return;

    if ((m_observedServerInstanceNonce != 0 && m_observedServerInstanceNonce != nonce) ||
        (m_observedConnectionGeneration != 0 && m_observedConnectionGeneration != generation))
    {
        ResetSubtitleTextState();
        m_pendingRemoteCommands = {};
        ClearPendingWorldInventoryTransactions();
        ResetCanonicalRecovery();
        m_pendingText.clear();
        m_pendingOutbound.clear();
        m_pendingOutboundServerInstanceNonce = 0;
        m_pendingOutboundConnectionGeneration = 0;
        m_pendingOutboundLifecycleEpoch = 0;
        m_textRetryTimer = 0.0;
        DiscardRetainedStateForSession(nonce, generation);
    }

    m_observedServerInstanceNonce = nonce;
    m_observedConnectionGeneration = generation;

    const auto epoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (epoch != 0 && epoch != m_observedLifecycleEpoch)
    {
        ResetSubtitleTextState();
        m_pendingRemoteCommands = {};
        ClearPendingWorldInventoryTransactions();
        ResetCanonicalRecovery();
        m_pendingText.clear();
        m_pendingOutbound.clear();
        m_pendingOutboundServerInstanceNonce = 0;
        m_pendingOutboundConnectionGeneration = 0;
        m_pendingOutboundLifecycleEpoch = 0;
        m_textRetryTimer = 0.0;
        m_observedLifecycleEpoch = epoch;
        ResetInFlightState();
    }
}

void VRWorldReplicationService::RetainServerSettings(const ServerSettings& acSettings) noexcept
{
    if (CaptureSession(m_deathSystemState))
        RetainState(m_deathSystemState, 0, 0, acSettings.DeathSystemEnabled ? 1 : 0, 0, 0.0F, 0.0F, 0.0F);

    if (acSettings.Difficulty > 5 || !CaptureSession(m_settingsState))
        return;

    RetainState(
        m_settingsState,
        0,
        0,
        static_cast<std::int32_t>(acSettings.Difficulty),
        acSettings.GreetingsEnabled ? 1 : 0,
        0.0F,
        0.0F,
        0.0F,
        (m_partyRoleKnown && m_partyLeader ? GameplayBridge::kWorldEncountersEnabled : 0u) |
            (acSettings.PvpEnabled ? GameplayBridge::kPvpEnabled : 0u));
}

void VRWorldReplicationService::Reconcile() noexcept
{
    if (!m_transport.IsOnline() || m_observedServerInstanceNonce == 0 ||
        m_observedConnectionGeneration == 0 || m_observedLifecycleEpoch == 0)
        return;

    ReconcileState(m_calendarState, static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::WorldState),
                   static_cast<std::uint16_t>(GameplayBridge::GameplayAction::SetCalendar));
    ReconcileState(m_weatherState, static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::WorldState),
                   static_cast<std::uint16_t>(GameplayBridge::GameplayAction::SetWeather));
    ReconcileState(m_settingsState, static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::WorldState),
                   static_cast<std::uint16_t>(GameplayBridge::GameplayAction::ApplyServerSettings));
    ReconcileState(m_deathSystemState, static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::ActorState),
                   static_cast<std::uint16_t>(GameplayBridge::GameplayAction::ConfigureDeathSystem));
    for (auto& [formId, state] : m_lockStates)
    {
        TP_UNUSED(formId);
        ReconcileState(state, static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Object),
                       static_cast<std::uint16_t>(GameplayBridge::GameplayAction::SetLockState));
    }
}

void VRWorldReplicationService::ReconcileState(
    RetainedState& arState, const std::uint16_t aDomain, const std::uint16_t aAction) noexcept
{
    if (!arState.Valid || !arState.Dirty || arState.InFlight || arState.Attempts >= kMaximumReconcileAttempts ||
        arState.ServerInstanceNonce != m_observedServerInstanceNonce ||
        arState.ConnectionGeneration != m_observedConnectionGeneration)
        return;

    const auto domain = static_cast<GameplayBridge::GameplayDomain>(aDomain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(aAction);
    GameplayBridge::CommandRecord command{};
    const bool isDeathSystemPolicy = domain == GameplayBridge::GameplayDomain::ActorState &&
                                     action == GameplayBridge::GameplayAction::ConfigureDeathSystem;
    if ((isDeathSystemPolicy && !m_avatars.BuildLocalGameplayCommand(domain, action, command)) ||
        (!isDeathSystemPolicy && !BuildWorldCommand(m_transport, domain, action, command)))
        return;

    auto& payload = command.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = arState.TargetLocalFormId;
    payload.LocalFormIdA = arState.LocalFormIdA;
    payload.ValueA = arState.ValueA;
    payload.ValueB = arState.ValueB;
    payload.ScalarA = arState.ScalarA;
    payload.ScalarB = arState.ScalarB;
    payload.ScalarC = arState.ScalarC;
    payload.ActionFlags = arState.ActionFlags;
    ++arState.Attempts;
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        if (arState.Attempts >= kMaximumReconcileAttempts)
            arState.Dirty = false;
        return;
    }

    arState.InFlight = true;
    arState.InFlightActionId = command.Header.Identity.ActionId;
    arState.SubmittedVersion = arState.Version;
}

void VRWorldReplicationService::ResetRetainedState() noexcept
{
    m_calendarState = {};
    m_weatherState = {};
    m_settingsState = {};
    m_deathSystemState = {};
    m_waypointEcho = {};
    m_dialogueText = {};
    ResetSubtitleTextState();
    m_pendingRemoteCommands = {};
    ClearPendingWorldInventoryTransactions();
    m_pendingText.clear();
    m_pendingOutbound.clear();
    m_pendingOutboundServerInstanceNonce = 0;
    m_pendingOutboundConnectionGeneration = 0;
    m_pendingOutboundLifecycleEpoch = 0;
    m_textRetryTimer = 0.0;
    m_partyRoleKnown = false;
    m_partyLeader = false;
    m_lastLocalActionIdByDomain.fill(0);
    m_lockStates.clear();
    ResetCanonicalRecovery();
}

void VRWorldReplicationService::ResetSubtitleTextState() noexcept
{
    m_subtitleText = {};
    m_lastSubtitleActionId = 0;
}

void VRWorldReplicationService::DiscardRetainedStateForSession(
    const std::uint64_t aServerInstanceNonce, const std::uint64_t aConnectionGeneration) noexcept
{
    const auto belongsTo = [aServerInstanceNonce, aConnectionGeneration](const RetainedState& acState) {
        return acState.Valid && acState.ServerInstanceNonce == aServerInstanceNonce &&
               acState.ConnectionGeneration == aConnectionGeneration;
    };
    if (!belongsTo(m_calendarState))
        m_calendarState = {};
    if (!belongsTo(m_weatherState))
        m_weatherState = {};
    if (!belongsTo(m_settingsState))
        m_settingsState = {};
    if (!belongsTo(m_deathSystemState))
        m_deathSystemState = {};
    for (auto it = m_lockStates.begin(); it != m_lockStates.end();)
    {
        if (!belongsTo(it->second))
            it = m_lockStates.erase(it);
        else
            ++it;
    }
}

void VRWorldReplicationService::ResetInFlightState() noexcept
{
    const auto reset = [](RetainedState& arState) {
        if (!arState.Valid)
            return;
        arState.Dirty = true;
        arState.InFlight = false;
        arState.InFlightActionId = 0;
        arState.Attempts = 0;
    };
    reset(m_calendarState);
    reset(m_weatherState);
    reset(m_settingsState);
    reset(m_deathSystemState);
    for (auto& [formId, state] : m_lockStates)
    {
        TP_UNUSED(formId);
        reset(state);
    }
}

void VRWorldReplicationService::OnGameplayResult(
    const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::RemoteGameplayActionState) ||
        record.Header.Identity.ServerInstanceNonce != m_observedServerInstanceNonce ||
        record.Header.Identity.ConnectionGeneration != m_observedConnectionGeneration ||
        record.Header.Identity.LifecycleEpoch != m_observedLifecycleEpoch)
        return;

    const auto& result = record.Payload.RemoteGameplayActionState;
    if (HandlePendingRemoteCommandResult(record))
        return;

    RetainedState* state = nullptr;
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
    if (domain == GameplayBridge::GameplayDomain::WorldState && action == GameplayBridge::GameplayAction::SetCalendar &&
        result.TargetLocalFormId == 0)
        state = &m_calendarState;
    else if (domain == GameplayBridge::GameplayDomain::WorldState && action == GameplayBridge::GameplayAction::SetWeather)
        state = &m_weatherState;
    else if (domain == GameplayBridge::GameplayDomain::WorldState &&
             action == GameplayBridge::GameplayAction::ApplyServerSettings && result.TargetLocalFormId == 0)
        state = &m_settingsState;
    else if (domain == GameplayBridge::GameplayDomain::ActorState &&
             action == GameplayBridge::GameplayAction::ConfigureDeathSystem &&
             result.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value && result.TargetLocalFormId == 0)
        state = &m_deathSystemState;
    else if (domain == GameplayBridge::GameplayDomain::Object && action == GameplayBridge::GameplayAction::SetLockState)
    {
        const auto it = m_lockStates.find(result.TargetLocalFormId);
        if (it != m_lockStates.end())
            state = &it->second;
    }

    if (state)
    {
        if (!state->InFlight || state->InFlightActionId != record.Header.Identity.ActionId)
            return;

        state->InFlight = false;
        state->InFlightActionId = 0;
        const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
        if (status == GameplayBridge::CommandStatus::Success)
        {
            state->Attempts = 0;
            state->Dirty = state->SubmittedVersion != state->Version;
            return;
        }

        const bool retryable = status == GameplayBridge::CommandStatus::Inactive ||
                               status == GameplayBridge::CommandStatus::MissingForm ||
                               status == GameplayBridge::CommandStatus::MissingCell ||
                               status == GameplayBridge::CommandStatus::EngineRejected ||
                               status == GameplayBridge::CommandStatus::QueueOverflow;
        state->Dirty = retryable && state->Attempts < kMaximumReconcileAttempts;
        if (domain == GameplayBridge::GameplayDomain::Object) {
            for (const auto& [objectId, serverId] : m_objectServerIds) {
                if (ToLocalForm(m_world, objectId) == result.TargetLocalFormId) {
                    RequestObjectResync(serverId);
                    break;
                }
            }
        }
        return;
    }

    HandlePendingWorldInventoryTransactionResult(record);
    TP_UNUSED(HandlePendingRemoteCommandResult(record));
}
