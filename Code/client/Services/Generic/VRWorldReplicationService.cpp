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
#include <Messages/NotifyPartyInfo.h>
#include <Messages/NotifyPlayerDialogue.h>
#include <Messages/NotifyQuestUpdate.h>
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
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestWeatherChange.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/TeleportCommandResponse.h>
#include <Services/TransportService.h>
#include <Services/VRAvatarService.h>
#include <Structs/ServerSettings.h>
#include <VRGameplayBridge.h>
#include <World.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr std::size_t kMaximumRetainedLockStates = 128;
constexpr std::uint8_t kMaximumReconcileAttempts = 3;
constexpr double kReconcileIntervalSeconds = 0.25;
constexpr std::size_t kMaximumPendingTextTransactions = 64;
constexpr double kWaypointEchoSuppressionSeconds = 3.0;
constexpr float kWaypointPositionTolerance = 1.0F;
constexpr float kMaximumWorldPosition = 10'000'000.0F;
constexpr std::uint32_t kPlayerReferenceFormId = 0x00000014;

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

void Submit(GameplayBridge::CommandRecord& arCommand) noexcept
{
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(arCommand))
        spdlog::warn("VR world replication rejected domain {} action {}", arCommand.Payload.ApplyGameplayAction.Domain,
                     arCommand.Payload.ApplyGameplayAction.Action);
}

[[nodiscard]] bool SubmitTextTransaction(
    GameplayBridge::CommandRecord aBase,
    const std::uint64_t aTextId,
    const std::string_view acText) noexcept
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
} // namespace

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
    m_playerDialogueConnection = aDispatcher.sink<NotifyPlayerDialogue>().connect<&VRWorldReplicationService::OnPlayerDialogue>(this);
    m_questConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&VRWorldReplicationService::OnQuestUpdate>(this);
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

void VRWorldReplicationService::SubmitText(
    GameplayBridge::CommandRecord aBase, const std::uint64_t aTextId, const std::string_view acText) noexcept
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

void VRWorldReplicationService::OnAssignObjects(const AssignObjectsResponse& acMessage) noexcept
{
    for (const auto& object : acMessage.Objects)
    {
        const auto objectFormId = ToLocalForm(m_world, object.Id);
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

        GameplayBridge::CommandRecord reset{};
        if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Inventory,
                               GameplayBridge::GameplayAction::InventoryReset, reset))
            return;
        reset.Payload.ApplyGameplayAction.TargetLocalFormId = objectFormId;
        Submit(reset);

        for (const auto& item : object.CurrentInventory.Entries)
        {
            if (item.Count <= 0 || item.Count > 10'000)
                continue;
            GameplayBridge::CommandRecord command{};
            if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Inventory,
                                   GameplayBridge::GameplayAction::InventoryDelta, command))
                return;
            auto& payload = command.Payload.ApplyGameplayAction;
            payload.TargetLocalFormId = objectFormId;
            payload.LocalFormIdA = ToLocalForm(m_world, item.BaseId);
            payload.ValueA = item.Count;
            payload.ActionFlags = GameplayBridge::kInventorySnapshotEntry |
                                  (item.IsQuestItem ? GameplayBridge::kInventoryQuestItem : 0u);
            if (payload.LocalFormIdA != 0)
                Submit(command);
        }
    }
    Reconcile();
}

void VRWorldReplicationService::OnActivate(const NotifyActivate& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(
            acMessage.ActivatorId, GameplayBridge::GameplayDomain::Object,
            GameplayBridge::GameplayAction::Activate, command))
        return;
    auto& payload = command.Payload.ApplyGameplayAction;
    payload.TargetLocalFormId = ToLocalForm(m_world, acMessage.Id);
    payload.ValueA = acMessage.PreActivationOpenState;
    if (payload.TargetLocalFormId != 0)
        Submit(command);
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
        Submit(command);
}

void VRWorldReplicationService::OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept
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

void VRWorldReplicationService::OnLockChange(const NotifyLockChange& acMessage) noexcept
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

void VRWorldReplicationService::OnNewPackage(const NotifyNewPackage& acMessage) noexcept
{
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildRemoteGameplayCommandForServerId(acMessage.ActorId,
            GameplayBridge::GameplayDomain::Dialogue, GameplayBridge::GameplayAction::Package, command))
        return;
    command.Payload.ApplyGameplayAction.LocalFormIdA = ToLocalForm(m_world, acMessage.PackageId);
    if (command.Payload.ApplyGameplayAction.LocalFormIdA != 0)
        Submit(command);
}

void VRWorldReplicationService::OnObjectInventory(const NotifyObjectInventoryChanges& acMessage) noexcept
{
    for (const auto& [objectId, inventory] : acMessage.Changes)
    {
        const auto objectFormId = ToLocalForm(m_world, objectId);
        if (objectFormId == 0)
            continue;
        for (const auto& item : inventory.Entries)
        {
            GameplayBridge::CommandRecord command{};
            if (!BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::Inventory,
                                   GameplayBridge::GameplayAction::InventoryDelta, command))
                return;
            auto& payload = command.Payload.ApplyGameplayAction;
            payload.TargetLocalFormId = objectFormId;
            payload.LocalFormIdA = ToLocalForm(m_world, item.BaseId);
            payload.ValueA = item.Count;
            payload.ActionFlags = item.IsQuestItem ? GameplayBridge::kInventoryQuestItem : 0u;
            if (payload.LocalFormIdA != 0 && payload.ValueA != 0)
                Submit(command);
        }
    }
}

void VRWorldReplicationService::OnPlayerDialogue(const NotifyPlayerDialogue& acMessage) noexcept
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

void VRWorldReplicationService::OnQuestUpdate(const NotifyQuestUpdate& acMessage) noexcept
{
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
    if (payload.TargetLocalFormId != 0)
        Submit(command);
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
        Submit(command);
}

void VRWorldReplicationService::OnScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
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
        Submit(command);
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
        Submit(command);
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
        Submit(command);
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
    RetainServerSettings(m_world.GetServerSettings());
    if (acEvent.IsLeader)
        SubmitReleaseWeather();
    else
        m_transport.Send(RequestCurrentWeather{});
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
        m_transport.Send(RequestCurrentWeather{});
}

void VRWorldReplicationService::OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline() || !m_world.GetPartyService().IsInParty() || acEvent.Text.empty() ||
        acEvent.Text.size() > 1024)
        return;
    PlayerDialogueRequest request{};
    request.Text = acEvent.Text;
    m_transport.Send(request);
}

void VRWorldReplicationService::OnLocalGameplay(
    const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
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
        m_lastLocalActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
        if (!m_world.GetPartyService().IsInParty() || !m_world.GetPartyService().IsLeader())
            return;
        RequestWeatherChange request{};
        if (m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.Id) && request.Id)
            m_transport.Send(request);
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
        m_lastLocalActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
        if (m_waypointEcho.Valid && m_waypointEcho.Remove) {
            m_waypointEcho = {};
            return;
        }
        m_transport.Send(RequestRemoveWaypoint{});
        return;
    }

    if (payload.LocalFormIdA == 0 || payload.LocalFormIdB != 0 || payload.LocalFormIdC != 0 ||
        payload.LocalFormIdD != 0 || payload.ValueA != 0 || payload.ValueB != 0 ||
        std::abs(payload.ScalarA) > kMaximumWorldPosition ||
        std::abs(payload.ScalarB) > kMaximumWorldPosition ||
        std::abs(payload.ScalarC) > kMaximumWorldPosition || payload.ScalarD != 0.0F ||
        payload.ActionFlags != 0)
        return;
    m_lastLocalActionIdByDomain[domainIndex] = record.Header.Identity.ActionId;
    if (m_waypointEcho.Valid && !m_waypointEcho.Remove &&
        m_waypointEcho.LocalWorldspaceFormId == payload.LocalFormIdA &&
        std::abs(m_waypointEcho.PositionX - payload.ScalarA) <= kWaypointPositionTolerance &&
        std::abs(m_waypointEcho.PositionY - payload.ScalarB) <= kWaypointPositionTolerance &&
        std::abs(m_waypointEcho.PositionZ - payload.ScalarC) <= kWaypointPositionTolerance) {
        m_waypointEcho = {};
        return;
    }
    RequestSetWaypoint request{};
    request.Position = {payload.ScalarA, payload.ScalarB, payload.ScalarC};
    if (m_world.GetModSystem().GetServerModId(payload.LocalFormIdA, request.WorldSpaceFormID) &&
        request.WorldSpaceFormID)
        m_transport.Send(request);
}

void VRWorldReplicationService::OnLocalGameplayText(const GameplayBridge::EventRecord& acRecord) noexcept
{
    const auto& header = acRecord.Header;
    const auto& payload = acRecord.Payload.LocalGameplayTextChunk;
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
    m_lastLocalActionIdByDomain[domainIndex] = header.Identity.ActionId;
    m_dialogueText = {};

    if (targetLocalFormId == kPlayerReferenceFormId)
    {
        PlayerDialogueRequest request{};
        request.Text.assign(text.data(), text.size());
        m_transport.Send(request);
        return;
    }

    if (!IsSafeVoiceResourcePath(text))
        return;
    auto localActors = m_world.view<FormIdComponent, LocalComponent>(entt::exclude<ObjectComponent>);
    const auto actorIt = std::find_if(localActors.begin(), localActors.end(), [localActors, targetLocalFormId](const auto aEntity) {
        return localActors.get<FormIdComponent>(aEntity).Id == targetLocalFormId;
    });
    if (actorIt == localActors.end())
        return;

    DialogueRequest request{};
    request.ServerId = localActors.get<LocalComponent>(*actorIt).Id;
    request.SoundFilename.assign(text.data(), text.size());
    m_transport.Send(request);
}

void VRWorldReplicationService::SubmitReleaseWeather() noexcept
{
    GameplayBridge::CommandRecord command{};
    if (BuildWorldCommand(m_transport, GameplayBridge::GameplayDomain::WorldState,
                          GameplayBridge::GameplayAction::ReleaseWeather, command))
        Submit(command);
}

void VRWorldReplicationService::OnConnected(const ConnectedEvent&) noexcept
{
    ObserveSession();
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
    m_waypointEcho = {};
    m_dialogueText = {};
    m_lastLocalActionIdByDomain.fill(0);
    m_partyRoleKnown = false;
    m_partyLeader = false;
    ResetRetainedState();
}

void VRWorldReplicationService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    ObserveSession();
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
        m_pendingText.clear();
        m_textRetryTimer = 0.0;
        DiscardRetainedStateForSession(nonce, generation);
    }

    m_observedServerInstanceNonce = nonce;
    m_observedConnectionGeneration = generation;

    const auto epoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (epoch != 0 && epoch != m_observedLifecycleEpoch)
    {
        m_pendingText.clear();
        m_textRetryTimer = 0.0;
        m_observedLifecycleEpoch = epoch;
        ResetInFlightState();
    }
}

void VRWorldReplicationService::RetainServerSettings(const ServerSettings& acSettings) noexcept
{
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
        m_partyRoleKnown && m_partyLeader ? GameplayBridge::kWorldEncountersEnabled : 0u);
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

    GameplayBridge::CommandRecord command{};
    if (!BuildWorldCommand(m_transport, static_cast<GameplayBridge::GameplayDomain>(aDomain),
                           static_cast<GameplayBridge::GameplayAction>(aAction), command))
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
    m_waypointEcho = {};
    m_dialogueText = {};
    m_pendingText.clear();
    m_textRetryTimer = 0.0;
    m_partyRoleKnown = false;
    m_partyLeader = false;
    m_lastLocalActionIdByDomain.fill(0);
    m_lockStates.clear();
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
    else if (domain == GameplayBridge::GameplayDomain::Object && action == GameplayBridge::GameplayAction::SetLockState)
    {
        const auto it = m_lockStates.find(result.TargetLocalFormId);
        if (it != m_lockStates.end())
            state = &it->second;
    }

    if (!state || !state->InFlight || state->InFlightActionId != record.Header.Identity.ActionId)
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
}
