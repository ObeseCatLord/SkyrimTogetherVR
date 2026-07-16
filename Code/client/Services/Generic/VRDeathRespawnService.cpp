#include <TiltedOnlinePCH.h>

#include <Services/VRDeathRespawnService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/LocalGameplayBridgeEvent.h>
#include <Events/RemoteGameplayBridgeResultEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Services/TransportService.h>
#include <Services/VRAvatarService.h>
#include <Services/VRLocalGameplayService.h>
#include <Structs/GameId.h>
#include <VRGameplayBridge.h>
#include <World.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace GameplayBridge = SkyrimTogetherVR::GameplayBridge;

namespace
{
constexpr double kRespawnDelay = 5.0;
constexpr double kCommandRetryDelay = 0.25;
constexpr std::uint8_t kMaximumCommandAttempts = 3;
constexpr std::int32_t kMaximumGoldLoss = 10'000;
constexpr std::uint32_t kGoldFormId = 0xF;

[[nodiscard]] bool IsZero(const void* apData, const std::size_t aSize) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(apData);
    for (std::size_t index = 0; index < aSize; ++index)
    {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool IsRetryable(const GameplayBridge::CommandStatus aStatus) noexcept
{
    return aStatus == GameplayBridge::CommandStatus::Inactive ||
           aStatus == GameplayBridge::CommandStatus::MissingForm ||
           aStatus == GameplayBridge::CommandStatus::MissingCell ||
           aStatus == GameplayBridge::CommandStatus::EngineRejected ||
           aStatus == GameplayBridge::CommandStatus::QueueOverflow;
}
} // namespace

VRDeathRespawnService::VRDeathRespawnService(
    World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, VRAvatarService& aAvatars,
    VRLocalGameplayService& aLocalGameplay) noexcept
    : m_world(aWorld), m_transport(aTransport), m_avatars(aAvatars), m_localGameplay(aLocalGameplay)
{
    m_localGameplayConnection = aDispatcher.sink<SkyrimTogetherVR::LocalGameplayBridgeEvent>()
        .connect<&VRDeathRespawnService::OnLocalGameplayBridgeEvent>(this);
    m_notifyRespawnConnection = aDispatcher.sink<NotifyPlayerRespawn>()
        .connect<&VRDeathRespawnService::OnNotifyPlayerRespawn>(this);
    m_gameplayResultConnection = aDispatcher.sink<SkyrimTogetherVR::RemoteGameplayBridgeResultEvent>()
        .connect<&VRDeathRespawnService::OnGameplayResult>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRDeathRespawnService::OnUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRDeathRespawnService::OnDisconnected>(this);
}

void VRDeathRespawnService::OnLocalGameplayBridgeEvent(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept
{
    if (!IsValidLocalDeathState(acEvent))
        return;

    const auto& record = acEvent.Record;
    const auto localServerId = m_avatars.GetLocalServerId();
    if (m_sessionServerId != localServerId ||
        m_sessionLifecycleEpoch != record.Header.Identity.LifecycleEpoch)
        ResetSessionState();
    m_sessionServerId = localServerId;
    m_sessionLifecycleEpoch = record.Header.Identity.LifecycleEpoch;
    if (m_hasDeathActionId && record.Header.Identity.ActionId <= m_lastDeathActionId)
        return;

    m_hasDeathActionId = true;
    m_lastDeathActionId = record.Header.Identity.ActionId;
    const bool isDead = record.Payload.LocalGameplayAction.ValueA != 0;
    if (!isDead)
    {
        if (!m_serverRespawnPending)
            m_deathObserved = false;
        if (m_respawnActionId == 0 && !m_serverRespawnPending)
            CancelRespawnTimer();
        else
            m_respawnRemaining = 0.0;
        return;
    }

    if (m_deathObserved || !m_transport.IsOnline() || !m_world.GetServerSettings().DeathSystemEnabled)
        return;

    m_deathObserved = true;
    m_pendingServerId = localServerId;
    m_respawnRemaining = kRespawnDelay;
}

void VRDeathRespawnService::OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) noexcept
{
    if (!m_transport.IsOnline() || acMessage.GoldLost <= 0)
        return;

    if (m_pendingGoldLoss != 0 || m_goldActionId != 0)
    {
        spdlog::warn("Ignoring overlapping VR respawn gold notification while {} gold remains", m_pendingGoldLoss);
        return;
    }
    m_pendingGoldLoss = acMessage.GoldLost;
    m_totalGoldLoss = acMessage.GoldLost;
    m_goldAttempts = 0;
    SubmitGoldLoss();
}

void VRDeathRespawnService::OnGameplayResult(
    const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept
{
    const auto& record = acEvent.Record;
    if (record.Header.Kind != static_cast<std::uint16_t>(GameplayBridge::EventKind::RemoteGameplayActionState) ||
        record.Header.Identity.ServerInstanceNonce != m_transport.GetServerInstanceNonce() ||
        record.Header.Identity.ConnectionGeneration != m_transport.GetConnectionGeneration() ||
        record.Header.Identity.LifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch())
        return;

    const auto& result = record.Payload.RemoteGameplayActionState;
    const auto domain = static_cast<GameplayBridge::GameplayDomain>(result.Domain);
    const auto action = static_cast<GameplayBridge::GameplayAction>(result.Action);
    const auto status = static_cast<GameplayBridge::CommandStatus>(result.Status);
    if (record.Header.Identity.ActionId == m_respawnActionId &&
        domain == GameplayBridge::GameplayDomain::ActorState && action == GameplayBridge::GameplayAction::Respawn &&
        result.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value)
    {
        m_respawnActionId = 0;
        if (status == GameplayBridge::CommandStatus::Success)
        {
            m_serverRespawnPending = true;
            m_respawnAttempts = 0;
            SubmitServerRespawnRequest();
        }
        else if (IsRetryable(status) && m_respawnAttempts < kMaximumCommandAttempts)
            m_respawnRemaining = kCommandRetryDelay;
        else
        {
            spdlog::error("VR local respawn failed with bridge status {} after {} attempt(s)",
                          result.Status, m_respawnAttempts);
            CancelRespawnTimer();
            m_deathObserved = false;
        }
        return;
    }

    if (record.Header.Identity.ActionId != m_goldActionId ||
        domain != GameplayBridge::GameplayDomain::Inventory ||
        action != GameplayBridge::GameplayAction::InventoryDelta ||
        result.TargetHandle.Value != GameplayBridge::kLocalPlayerHandle.Value)
        return;

    m_goldActionId = 0;
    if (status == GameplayBridge::CommandStatus::Success)
    {
        m_pendingGoldLoss -= m_pendingGoldChunk;
        m_pendingGoldChunk = 0;
        m_goldAttempts = 0;
        if (m_pendingGoldLoss > 0)
            m_goldRetryRemaining = kCommandRetryDelay;
        else
        {
            SubmitHudMessage(fmt::format("You died and lost {} gold.", m_totalGoldLoss));
            m_totalGoldLoss = 0;
        }
    }
    else
    {
        m_localGameplay.CancelGoldInventoryDeltaSuppression();
        m_pendingGoldChunk = 0;
        if (IsRetryable(status) && m_goldAttempts < kMaximumCommandAttempts)
            m_goldRetryRemaining = kCommandRetryDelay;
        else
        {
            spdlog::error("VR respawn gold application failed with bridge status {} after {} attempt(s)",
                          result.Status, m_goldAttempts);
            m_pendingGoldLoss = 0;
            m_totalGoldLoss = 0;
        }
    }
}

void VRDeathRespawnService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline())
    {
        CancelRespawnTimer();
        return;
    }
    if ((m_sessionServerId != 0 && m_sessionServerId != m_avatars.GetLocalServerId()) ||
        (m_sessionLifecycleEpoch != 0 &&
         m_sessionLifecycleEpoch != SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch()))
    {
        ResetSessionState();
        return;
    }

    if (m_goldRetryRemaining > 0.0)
    {
        m_goldRetryRemaining -= acEvent.Delta;
        if (m_goldRetryRemaining <= 0.0 && !m_localGameplay.HasGoldInventoryDeltaSuppression())
            SubmitGoldLoss();
        else if (m_goldRetryRemaining <= 0.0)
            m_goldRetryRemaining = kCommandRetryDelay;
    }

    if (m_hudRetryRemaining > 0.0)
    {
        m_hudRetryRemaining -= acEvent.Delta;
        if (m_hudRetryRemaining <= 0.0)
            SubmitPendingHudMessage();
    }

    if (m_serverRespawnPending)
    {
        if (m_respawnRemaining > 0.0)
            m_respawnRemaining -= acEvent.Delta;
        if (m_respawnRemaining <= 0.0)
            SubmitServerRespawnRequest();
        return;
    }

    if (!m_world.GetServerSettings().DeathSystemEnabled)
    {
        CancelRespawnTimer();
        return;
    }

    if (m_respawnRemaining <= 0.0)
        return;

    m_respawnRemaining -= acEvent.Delta;
    if (m_respawnRemaining <= 0.0)
        SubmitRespawn();
}

void VRDeathRespawnService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    ResetSessionState();
}

bool VRDeathRespawnService::IsValidLocalDeathState(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept
{
    const auto& record = acEvent.Record;
    const auto& header = record.Header;
    const auto& payload = record.Payload.LocalGameplayAction;
    GameId targetId{};
    return m_avatars.GetLocalServerId() != 0 && header.Kind == static_cast<std::uint16_t>(GameplayBridge::EventKind::LocalGameplayAction) &&
           header.PayloadSize == GameplayBridge::kFixedPayloadBytes && header.Flags == 0 && header.Identity.EntityId == 0 &&
           header.Identity.EntityGeneration == 0 && header.Identity.SequenceId == 0 && header.Identity.ActionId != 0 &&
           payload.TargetHandle.Value == GameplayBridge::kLocalPlayerHandle.Value && payload.SecondaryHandle.Value == 0 &&
           payload.TargetLocalFormId != 0 &&
           m_world.GetModSystem().GetServerModId(payload.TargetLocalFormId, targetId) && targetId &&
           payload.Domain == static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::ActorState) &&
           payload.Action == static_cast<std::uint16_t>(GameplayBridge::GameplayAction::SetDeathState) &&
           payload.LocalFormIdA == 0 && payload.LocalFormIdB == 0 && payload.LocalFormIdC == 0 && payload.LocalFormIdD == 0 &&
           (payload.ValueA == 0 || payload.ValueA == 1) && payload.ValueB == 0 && payload.ScalarA == 0.0F &&
           payload.ScalarB == 0.0F && payload.ScalarC == 0.0F && payload.ScalarD == 0.0F && payload.ActionFlags == 0 &&
           payload.Reserved0 == 0 && IsZero(payload.ReservedTail, sizeof(payload.ReservedTail));
}

void VRDeathRespawnService::CancelRespawnTimer() noexcept
{
    m_pendingServerId = 0;
    m_respawnRemaining = 0.0;
}

void VRDeathRespawnService::ResetSessionState() noexcept
{
    CancelRespawnTimer();
    m_lastDeathActionId = 0;
    m_hasDeathActionId = false;
    m_deathObserved = false;
    m_sessionServerId = 0;
    m_sessionLifecycleEpoch = 0;
    m_respawnActionId = 0;
    m_goldActionId = 0;
    m_goldRetryRemaining = 0.0;
    m_hudRetryRemaining = 0.0;
    m_pendingGoldLoss = 0;
    m_totalGoldLoss = 0;
    m_pendingGoldChunk = 0;
    m_respawnAttempts = 0;
    m_goldAttempts = 0;
    m_hudAttempts = 0;
    m_serverRespawnPending = false;
    m_nextHudTextId = 1;
    m_pendingHudMessage.clear();
    m_localGameplay.CancelGoldInventoryDeltaSuppression();
}

void VRDeathRespawnService::SubmitServerRespawnRequest() noexcept
{
    if (!m_serverRespawnPending)
        return;

    if (!m_transport.IsOnline() || !m_transport.Send(PlayerRespawnRequest{}))
    {
        m_respawnRemaining = kCommandRetryDelay;
        return;
    }

    m_serverRespawnPending = false;
    CancelRespawnTimer();
    m_deathObserved = false;
}

void VRDeathRespawnService::SubmitRespawn() noexcept
{
    const auto localServerId = m_avatars.GetLocalServerId();
    if (m_respawnActionId != 0 || m_pendingServerId == 0 || m_pendingServerId != localServerId)
    {
        CancelRespawnTimer();
        return;
    }

    GameplayBridge::CommandRecord command{};
    if (m_avatars.BuildLocalGameplayCommand(
            GameplayBridge::GameplayDomain::ActorState, GameplayBridge::GameplayAction::Respawn, command) &&
        SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        m_respawnActionId = command.Header.Identity.ActionId;
        ++m_respawnAttempts;
        m_respawnRemaining = 0.0;
        return;
    }

    if (++m_respawnAttempts < kMaximumCommandAttempts)
        m_respawnRemaining = kCommandRetryDelay;
    else
    {
        spdlog::error("Unable to enqueue VR local respawn after {} attempt(s)", m_respawnAttempts);
        CancelRespawnTimer();
        m_deathObserved = false;
    }
}

void VRDeathRespawnService::SubmitGoldLoss() noexcept
{
    if (m_pendingGoldLoss <= 0 || m_goldActionId != 0 || !m_transport.IsOnline())
        return;
    m_goldRetryRemaining = 0.0;
    m_pendingGoldChunk = std::min(m_pendingGoldLoss, kMaximumGoldLoss);
    GameplayBridge::CommandRecord command{};
    if (!m_avatars.BuildLocalGameplayCommand(
            GameplayBridge::GameplayDomain::Inventory, GameplayBridge::GameplayAction::InventoryDelta, command))
    {
        m_pendingGoldChunk = 0;
        if (++m_goldAttempts < kMaximumCommandAttempts)
            m_goldRetryRemaining = kCommandRetryDelay;
        else
        {
            spdlog::error("Unable to build VR respawn gold command after {} attempt(s)", m_goldAttempts);
            m_pendingGoldLoss = 0;
            m_totalGoldLoss = 0;
        }
        return;
    }

    command.Payload.ApplyGameplayAction.LocalFormIdA = kGoldFormId;
    command.Payload.ApplyGameplayAction.ValueA = -m_pendingGoldChunk;
    m_localGameplay.ArmGoldInventoryDeltaSuppression(-m_pendingGoldChunk);
    if (!SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommand(command))
    {
        m_localGameplay.CancelGoldInventoryDeltaSuppression();
        m_pendingGoldChunk = 0;
        if (++m_goldAttempts < kMaximumCommandAttempts)
            m_goldRetryRemaining = kCommandRetryDelay;
        else
        {
            spdlog::error("Unable to enqueue VR respawn gold application after {} attempt(s)", m_goldAttempts);
            m_pendingGoldLoss = 0;
            m_totalGoldLoss = 0;
        }
        return;
    }
    m_goldActionId = command.Header.Identity.ActionId;
    ++m_goldAttempts;
}

void VRDeathRespawnService::SubmitHudMessage(const std::string_view acMessage) noexcept
{
    if (acMessage.empty() || !m_transport.IsOnline())
        return;
    m_pendingHudMessage.assign(acMessage);
    m_hudAttempts = 0;
    SubmitPendingHudMessage();
}

void VRDeathRespawnService::SubmitPendingHudMessage() noexcept
{
    if (m_pendingHudMessage.empty() || !m_transport.IsOnline())
        return;
    m_hudRetryRemaining = 0.0;
    const std::string_view message{m_pendingHudMessage};
    const auto byteCount = std::min<std::size_t>(
        message.size(), GameplayBridge::kGameplayTextBytesPerChunk * GameplayBridge::kMaximumGameplayTextChunks);
    const auto chunkCount = static_cast<std::uint16_t>(std::max<std::size_t>(
        1, (byteCount + GameplayBridge::kGameplayTextBytesPerChunk - 1) / GameplayBridge::kGameplayTextBytesPerChunk));
    std::vector<GameplayBridge::CommandRecord> commands(chunkCount);
    const auto textId = m_nextHudTextId++;
    if (m_nextHudTextId == 0)
        m_nextHudTextId = 1;
    for (std::uint16_t index = 0; index < chunkCount; ++index)
    {
        auto& command = commands[index];
        command.Header.Kind = static_cast<std::uint16_t>(GameplayBridge::CommandKind::ApplyGameplayTextChunk);
        command.Header.PayloadSize = GameplayBridge::kFixedPayloadBytes;
        auto& identity = command.Header.Identity;
        identity.ServerInstanceNonce = m_transport.GetServerInstanceNonce();
        identity.ConnectionGeneration = m_transport.GetConnectionGeneration();
        identity.LifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
        auto& payload = command.Payload.ApplyGameplayTextChunk;
        payload.Domain = static_cast<std::uint16_t>(GameplayBridge::GameplayDomain::Dialogue);
        payload.Action = static_cast<std::uint16_t>(GameplayBridge::GameplayAction::Dialogue);
        payload.TextId = textId;
        payload.ChunkIndex = index;
        payload.ChunkCount = chunkCount;
        const auto offset = static_cast<std::size_t>(index) * GameplayBridge::kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
            GameplayBridge::kGameplayTextBytesPerChunk, byteCount - std::min(offset, byteCount)));
        if (payload.ByteCount != 0)
            std::memcpy(payload.Utf8Bytes, message.data() + offset, payload.ByteCount);
    }
    if (SkyrimTogetherVR::GameplayBridgeClient::TrySubmitCommandBatch(commands.data(), commands.size()))
    {
        m_pendingHudMessage.clear();
        m_hudAttempts = 0;
        return;
    }

    if (++m_hudAttempts < kMaximumCommandAttempts)
        m_hudRetryRemaining = kCommandRetryDelay;
    else
    {
        spdlog::warn("Unable to enqueue VR respawn gold HUD message after {} attempt(s)", m_hudAttempts);
        m_pendingHudMessage.clear();
    }
}
