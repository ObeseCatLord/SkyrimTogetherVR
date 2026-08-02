
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Games/References.h>
#include <Games/TES.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>

#include <TimeManager.h>

#include <Forms/TESNPC.h>
#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/NotifySettingsChange.h>
#include <Structs/GameplayCapabilities.h>
#include <Packet.hpp>

#include <ScriptExtender.h>
#include <VRGameplayBridge.h>
#include <VRRuntimeDiagnostics.h>
#include <Services/DiscordService.h>
#include <Services/VRLifecycleService.h>

// #include <imgui_internal.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
#define TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_POSE_SERVICE
#define TP_SKYRIM_VR_ENABLE_POSE_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
#define TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE 0
#endif

static constexpr wchar_t kMO2DllName[] = L"usvfs_x64.dll";

using TiltedPhoques::Packet;

namespace
{
constexpr std::size_t kMaximumOutboundPacketBytes = 1u << 16;
constexpr std::size_t kMaximumOutboundQueuePackets = 256;
constexpr std::size_t kMaximumOutboundQueueBytes = 8u << 20;
constexpr std::uint32_t kGameplayRetirementRetryIntervalFrames = 30;
}

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_ownerThreadId(std::this_thread::get_id())
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&TransportService::HandleUpdate>(this);
    m_settingsChangeConnection = m_dispatcher.sink<NotifySettingsChange>().connect<&TransportService::HandleNotifySettingsChange>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&TransportService::HandleConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&TransportService::HandleDisconnected>(this);

    m_sessionId = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^
                  static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (m_sessionId == 0)
        m_sessionId = 1;

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
}

bool TransportService::Send(const ClientMessage& acMessage) noexcept
{
    if (std::this_thread::get_id() != m_ownerThreadId)
        return false;

#if TP_SKYRIM_VR
    if (!m_world.ctx().at<VRLifecycleService>().IsReady())
        return false;
#endif

    if (!IsConnected() || m_outboundQueue.size() >= kMaximumOutboundQueuePackets)
        return false;

    try
    {
        PendingOutboundPacket packet{};
        if (SerializeOutboundPacket(acMessage, &packet.Data) != OutboundPacketPreflightResult::Fits)
            return false;

        const auto packetBytes = packet.Data.size();
        if (m_outboundQueueBytes > kMaximumOutboundQueueBytes - packetBytes)
            return false;

        packet.ConnectionAttempt = m_connectionAttemptGeneration;
        packet.ConnectionGeneration = m_connectionGeneration;
        m_outboundQueue.emplace_back(std::move(packet));
        m_outboundQueueBytes += packetBytes;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

TransportService::OutboundPacketPreflightResult TransportService::PreflightOutboundPacket(
    const ClientMessage& acMessage) const noexcept
{
    return SerializeOutboundPacket(acMessage, nullptr);
}

TransportService::OutboundPacketPreflightResult TransportService::SerializeOutboundPacket(
    const ClientMessage& acMessage, std::vector<std::uint8_t>* apSerialized) const noexcept
{
    try
    {
        static thread_local ScratchAllocator s_allocator(1 << 18);

        struct ScopedReset
        {
            ~ScopedReset() { s_allocator.Reset(); }
        } allocatorGuard;

        ScopedAllocator _{s_allocator};
        Buffer buffer(kMaximumOutboundPacketBytes);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // The networking layer owns the packet marker.
        acMessage.Serialize(writer);
        const auto packetBytes = static_cast<std::size_t>(writer.Size());
        if (packetBytes == 0)
            return OutboundPacketPreflightResult::Unserializable;
        if (packetBytes > kMaximumOutboundPacketBytes)
            return OutboundPacketPreflightResult::Oversized;
        if (apSerialized)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(buffer.GetWriteData());
            apSerialized->assign(begin, begin + packetBytes);
        }
        return OutboundPacketPreflightResult::Fits;
    }
    catch (...)
    {
        return OutboundPacketPreflightResult::Unserializable;
    }
}

void TransportService::DrainOutboundQueue() noexcept
{
    if (m_drainingOutboundQueue || !IsConnected())
        return;

    m_drainingOutboundQueue = true;
    try
    {
        while (!m_outboundQueue.empty() && IsConnected())
        {
            auto& pending = m_outboundQueue.front();
            const bool currentAttempt = pending.ConnectionAttempt == m_connectionAttemptGeneration;
            const bool currentGeneration = pending.ConnectionGeneration == m_connectionGeneration;
            if (!currentAttempt || !currentGeneration)
            {
                m_outboundQueueBytes -= pending.Data.size();
                m_outboundQueue.pop_front();
                continue;
            }

            auto packetData = std::move(pending);
            m_outboundQueueBytes -= packetData.Data.size();
            m_outboundQueue.pop_front();
            TiltedPhoques::PacketView packet(
                reinterpret_cast<char*>(packetData.Data.data()),
                static_cast<std::uint32_t>(packetData.Data.size()));
            Client::Send(&packet, packetData.Flags);
        }
    }
    catch (...)
    {
        ClearOutboundQueue();
        return;
    }
    m_drainingOutboundQueue = false;
}

void TransportService::ClearOutboundQueue() noexcept
{
    m_outboundQueue.clear();
    m_outboundQueueBytes = 0;
    m_drainingOutboundQueue = false;
}

void TransportService::ClearGameplayRetirementState() noexcept
{
    m_gameplayRetirement = {};
}

void TransportService::BeginGameplayRetirement(
    const SkyrimTogetherVR::GameplayBridge::EpochRetireReason aReason,
    const std::uint64_t aOriginLifecycleEpoch) noexcept
{
    ClearGameplayRetirementState();

    const auto expectedLifecycleEpoch = aOriginLifecycleEpoch + 1;
    if (m_serverInstanceNonce == 0 || m_connectionGeneration == 0)
    {
        m_gameplayCleanupRequired = false;
        return;
    }

    auto& request = m_gameplayRetirement;
    request.ServerInstanceNonce = m_serverInstanceNonce;
    request.ConnectionGeneration = m_connectionGeneration;
    if (aOriginLifecycleEpoch != 0 && expectedLifecycleEpoch != 0)
    {
        request.OriginLifecycleEpoch = aOriginLifecycleEpoch;
        request.ExpectedLifecycleEpoch = expectedLifecycleEpoch;
    }
    request.Reason = static_cast<std::uint32_t>(aReason);
    request.RetryFramesRemaining = kGameplayRetirementRetryIntervalFrames;
    request.Pending = true;
    m_gameplayCleanupRequired = true;
}

void TransportService::CompleteGameplayRetirement() noexcept
{
    m_gameplayRetirement.Pending = false;
    m_gameplayRetirement.Completed = true;
    m_gameplayRetirement.RetryFramesRemaining = 0;
    m_gameplayCleanupRequired = false;

#if TP_SKYRIM_VR
    if (m_gameplayIdentityClearPending)
    {
        SkyrimTogetherVR::GameplayBridgeClient::UpdateSessionIdentity(0, 0);
        m_serverInstanceNonce = 0;
        m_connectionGeneration = 0;
        m_negotiatedGameplayCapabilities = 0;
        m_gameplayIdentityClearPending = false;
        ClearGameplayRetirementState();
    }
#endif
}

bool TransportService::IsGameplayRetirementRequestCurrent() const noexcept
{
    const auto& request = m_gameplayRetirement;
    return request.ServerInstanceNonce != 0 && request.ConnectionGeneration != 0 &&
           request.ServerInstanceNonce == m_serverInstanceNonce &&
           request.ConnectionGeneration == m_connectionGeneration;
}

bool TransportService::AttemptGameplayRetirement() noexcept
{
#if TP_SKYRIM_VR
    auto& request = m_gameplayRetirement;
    if (!request.Pending || !IsGameplayRetirementRequestCurrent())
        return false;

    if (request.OriginLifecycleEpoch == 0 || request.ExpectedLifecycleEpoch == 0)
    {
        request.RetryFramesRemaining = kGameplayRetirementRetryIntervalFrames;
        m_gameplayCleanupRequired = true;
        return false;
    }

    TP_UNUSED(SkyrimTogetherVR::GameplayBridgeClient::RetireSession(
        static_cast<SkyrimTogetherVR::GameplayBridge::EpochRetireReason>(request.Reason)));

    if (IsGameplayRetirementRequestCurrent() &&
        SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch() == request.ExpectedLifecycleEpoch)
    {
        CompleteGameplayRetirement();
        return true;
    }

    request.RetryFramesRemaining = kGameplayRetirementRetryIntervalFrames;
    m_gameplayCleanupRequired = true;
    return false;
#else
    return true;
#endif
}

void TransportService::RetryGameplayRetirement() noexcept
{
#if TP_SKYRIM_VR
    if (std::this_thread::get_id() != m_ownerThreadId)
        return;

    auto& request = m_gameplayRetirement;
    if (request.Completed)
    {
        // The cache only spans the fault wave that completed this transition.
        ClearGameplayRetirementState();
        return;
    }

    if (!request.Pending)
    {
        if (m_gameplayIdentityClearPending &&
            m_serverInstanceNonce != 0 && m_connectionGeneration != 0)
        {
            BeginGameplayRetirement(
                SkyrimTogetherVR::GameplayBridge::EpochRetireReason::Disconnect,
                SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch());
            TP_UNUSED(AttemptGameplayRetirement());
        }
        return;
    }

    if (!IsGameplayRetirementRequestCurrent())
    {
        ClearGameplayRetirementState();
        m_gameplayCleanupRequired = false;
        return;
    }

    const auto observedLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (request.OriginLifecycleEpoch == 0 || request.ExpectedLifecycleEpoch == 0)
    {
        if (observedLifecycleEpoch == 0)
        {
            if (request.RetryFramesRemaining != 0)
                --request.RetryFramesRemaining;
            return;
        }

        const auto reason = static_cast<SkyrimTogetherVR::GameplayBridge::EpochRetireReason>(request.Reason);
        BeginGameplayRetirement(reason, observedLifecycleEpoch);
        request.RetryFramesRemaining = 0;
    }

    if (observedLifecycleEpoch == request.ExpectedLifecycleEpoch)
    {
        CompleteGameplayRetirement();
        return;
    }

    if (observedLifecycleEpoch != request.OriginLifecycleEpoch)
    {
        // A different epoch is not evidence that this request completed. Rebase
        // the pending ownership to the current epoch and retain cleanup status.
        const auto reason = static_cast<SkyrimTogetherVR::GameplayBridge::EpochRetireReason>(request.Reason);
        BeginGameplayRetirement(reason, observedLifecycleEpoch);
        return;
    }

    if (request.RetryFramesRemaining != 0)
    {
        --request.RetryFramesRemaining;
        return;
    }

    // A failed synchronous call may already have submitted its command. Pump
    // before submitting another retirement command so that transition wins.
    if (SkyrimTogetherVR::GameplayBridgeClient::PumpCommands(
            SkyrimTogetherVR::GameplayBridge::kDefaultCommandRingCapacity) !=
        SkyrimTogetherVR::GameplayBridge::CommandPumpResult::Success)
    {
        request.RetryFramesRemaining = kGameplayRetirementRetryIntervalFrames;
        return;
    }

    const auto pumpedLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    if (pumpedLifecycleEpoch == request.ExpectedLifecycleEpoch)
    {
        CompleteGameplayRetirement();
        return;
    }

    if (pumpedLifecycleEpoch != request.OriginLifecycleEpoch)
    {
        const auto reason = static_cast<SkyrimTogetherVR::GameplayBridge::EpochRetireReason>(request.Reason);
        BeginGameplayRetirement(reason, pumpedLifecycleEpoch);
        return;
    }

    TP_UNUSED(AttemptGameplayRetirement());
#endif
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from server");
        return;
    }

    const auto opcode = pMessage->GetOpcode();
    auto& handler = m_messageHandlers[opcode];
    if (!handler)
    {
        spdlog::error("No handler registered for server opcode {}", static_cast<uint32_t>(opcode));
        return;
    }

    handler(pMessage);
}

void TransportService::OnConnected()
{
    ClearOutboundQueue();
#if TP_SKYRIM_VR
    if (!m_connected && m_serverInstanceNonce != 0 && m_connectionGeneration != 0)
        m_gameplayIdentityClearPending = true;

    RetryGameplayRetirement();
    if (m_gameplayCleanupRequired || m_gameplayRetirement.Pending ||
        m_gameplayIdentityClearPending)
    {
        spdlog::warn("Deferring authentication until the prior CommonLib gameplay session is retired");
        ConnectionErrorEvent errorEvent;
        errorEvent.ErrorDetail = "{\"error\":\"gameplay_cleanup_required\"}";
        Client::Close();
        m_dispatcher.trigger(errorEvent);
        return;
    }
#endif

    AuthenticationRequest request{};
    request.Version = BUILD_COMMIT;
    request.GameplayProtocolRevision = SkyrimTogether::Protocol::kGameplayProtocolRevision;
    m_requestedGameplayCapabilities = SkyrimTogether::Protocol::kCoreCapabilities;
#if TP_SKYRIM_VR && TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC
    using SkyrimTogether::Protocol::GameplayCapability;
    using SkyrimTogether::Protocol::ToMask;
    m_requestedGameplayCapabilities |= SkyrimTogether::Protocol::kRemoteAvatarCoreCapabilities |
                                       ToMask(GameplayCapability::VREquipmentRelay) |
                                       ToMask(GameplayCapability::VRGrabRelay) |
                                       ToMask(GameplayCapability::VRAppearanceRelay);
#if TP_SKYRIM_VR_ENABLE_POSE_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRPoseRelay);
#endif
#if TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRMovementRelay);
#endif
#if TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRActivationRelay);
#endif
#if TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRMagicRelay);
#endif
#if TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRCombatPlanckRelay);
#endif
#if TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRProjectileRelay);
#endif
#if TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE
    m_requestedGameplayCapabilities |= ToMask(GameplayCapability::VRHiggsRelay);
#endif
    const auto nativeGameplayCapabilities = SkyrimTogetherVR::GameplayBridgeClient::GetActiveCapabilities();
    if (SkyrimTogetherVR::GameplayBridgeClient::IsReady() &&
        SkyrimTogetherVR::GameplayBridge::HasCapability(
            nativeGameplayCapabilities, SkyrimTogetherVR::GameplayBridge::Capability::NpcOwnership) &&
        SkyrimTogetherVR::GameplayBridge::HasCapability(
            nativeGameplayCapabilities, SkyrimTogetherVR::GameplayBridge::Capability::InventoryStackTransactions))
        m_requestedGameplayCapabilities |= SkyrimTogether::Protocol::kVRNpcOwnershipCapabilities;
    // Preserve the protocol identifier for future compatibility, but do not
    // advertise exact actor actions: VR ForceAction replay lacks a proven ABI.
#endif
    request.GameplayCapabilities = m_requestedGameplayCapabilities;
    request.ClientSessionNonce = m_sessionId;
    do
    {
        request.ConnectionAttempt = ++m_connectionAttemptGeneration;
    } while (request.ConnectionAttempt == 0);
#if TP_SKYRIM_VR
    constexpr auto kSkseVrCoreLoadTimeout = std::chrono::milliseconds(250);
    request.SKSEActive = IsScriptExtenderLoaded();
    if (!request.SKSEActive && GetScriptExtenderLoadResult() == ScriptExtenderLoadResult::kModuleLoaded)
    {
        request.SKSEActive = WaitForScriptExtenderLoaded(kSkseVrCoreLoadTimeout);
        if (!request.SKSEActive)
        {
            spdlog::warn("SKSEVR core was not visible {} ms after connection; authentication will report SKSE inactive", kSkseVrCoreLoadTimeout.count());
        }
    }
#else
    request.SKSEActive = IsScriptExtenderLoaded();
#endif
    request.MO2Active = GetModuleHandleW(kMO2DllName);

    request.Token = m_serverPassword;
    m_serverPassword = "";

    // null if discord is not active
    // TODO: think about user opt out
    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;

#if TP_SKYRIM_VR
    // The stable lifecycle gate owns player/cell readiness. Cell/grid state is
    // sent immediately after authentication by the VR player-cell service.
    request.Username = "Skyrim VR Player";
    request.WorldSpaceId = {};
    request.CellId = {};
    request.Level = 1;
    request.PlayerTime = TimeModel{};
#else
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || !pPlayer->GetBaseFormData() || !pPlayer->GetParentCellData())
        spdlog::warn("Building authentication request before the local player is fully loaded");

    if (pPlayer && pPlayer->GetBaseFormData())
    {
        auto* pNpc = Cast<TESNPC>(pPlayer->GetBaseFormData());
        if (pNpc)
        {
            request.Username = pNpc->GetFullNameData().GetFullNameStringData();
        }
    }

    if (request.Username.empty())
        request.Username = "Skyrim VR Player";
#endif

    auto* const cpModManager = ModManager::Get();

    if (cpModManager)
    {
        for (auto* pMod : cpModManager->mods)
        {
            if (!pMod || !pMod->IsLoaded())
                continue;

            auto& entry = request.UserMods.ModList.emplace_back();
            entry.Id = pMod->GetId();
            entry.IsLite = pMod->IsLite();
            entry.Filename = pMod->filename;
        }
    }

#if TP_SKYRIM_VR
    spdlog::info("SkyrimTogetherVR VR authentication snapshot ready: {} loaded mods, fallback level {}", request.UserMods.ModList.size(), request.Level);
#else
    auto& modSystem = m_world.GetModSystem();
    if (pPlayer && pPlayer->GetWorldSpace())
        modSystem.GetServerModId(pPlayer->GetWorldSpace()->GetFormIdData(), request.WorldSpaceId);

    if (pPlayer && pPlayer->GetParentCellData())
        modSystem.GetServerModId(pPlayer->GetParentCellData()->GetFormIdData(), request.CellId);

    request.Level = pPlayer ? pPlayer->GetLevel() : 1;

    auto* pGameTime = TimeData::Get();
    if (pGameTime)
    {
        request.PlayerTime.TimeScale = pGameTime->GetTimeScaleData()->GetValueData();
        request.PlayerTime.Time = pGameTime->GetGameHourData()->GetValueData();
        request.PlayerTime.Year = pGameTime->GetGameYearData()->GetValueData();
        request.PlayerTime.Month = pGameTime->GetGameMonthData()->GetValueData();
        request.PlayerTime.Day = pGameTime->GetGameDayData()->GetValueData();
    }
#endif

    if (!Send(request))
    {
        spdlog::error("SkyrimTogetherVR authentication request was not queued because the transport is no longer connected");
        ConnectionErrorEvent errorEvent;
        errorEvent.ErrorDetail = "{\"error\":\"authentication_not_queued\"}";
        m_dispatcher.trigger(errorEvent);
        return;
    }

    spdlog::info("SkyrimTogetherVR authentication request queued");
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    ClearOutboundQueue();
    m_connected = false;
    m_localPlayerId = 0;
#if TP_SKYRIM_VR
    if (m_gameplayRetirement.Completed)
        ClearGameplayRetirementState();

    if (m_serverInstanceNonce != 0 && m_connectionGeneration != 0 &&
        !m_gameplayIdentityClearPending)
        m_gameplayIdentityClearPending = true;

    if (m_gameplayIdentityClearPending &&
        !RetireGameplaySession(SkyrimTogetherVR::GameplayBridge::EpochRetireReason::Disconnect))
    {
        m_gameplayCleanupRequired = true;
        spdlog::warn("SkyrimTogetherVR CommonLib gameplay session could not be retired during disconnect");
    }
#else
    m_serverInstanceNonce = 0;
    m_connectionGeneration = 0;
#endif
    m_negotiatedGameplayCapabilities = 0;

    spdlog::warn("Disconnected from server {}", static_cast<std::underlying_type_t<EDisconnectReason>>(aReason));

    m_dispatcher.trigger(DisconnectedEvent());
}

void TransportService::OnUpdate()
{
    DrainOutboundQueue();
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
#if TP_SKYRIM_VR
    RetryGameplayRetirement();
#endif
    Update();
}

void TransportService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.transport.begin");
    m_localPlayerId = acEvent.PlayerId;
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.transport.done");
}

void TransportService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_localPlayerId = NULL;
}

void TransportService::HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept
{
    using AR = AuthenticationResponse::ResponseType;
    if (acMessage.Type == AR::kAccepted)
    {
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.accept.begin");
        const auto expectedNegotiatedCapabilities =
            acMessage.ServerCapabilities & m_requestedGameplayCapabilities;
        const bool validProtocol =
            acMessage.GameplayProtocolRevision == SkyrimTogether::Protocol::kGameplayProtocolRevision &&
            (acMessage.ServerCapabilities & SkyrimTogether::Protocol::kCoreCapabilities) == SkyrimTogether::Protocol::kCoreCapabilities &&
            acMessage.NegotiatedCapabilities == expectedNegotiatedCapabilities &&
            (acMessage.NegotiatedCapabilities & ~m_requestedGameplayCapabilities) == 0 &&
            (acMessage.NegotiatedCapabilities & SkyrimTogether::Protocol::kCoreCapabilities) == SkyrimTogether::Protocol::kCoreCapabilities &&
            acMessage.ServerInstanceNonce != 0 && acMessage.ConnectionGeneration != 0 &&
            acMessage.ClientSessionNonce == m_sessionId && acMessage.ConnectionAttempt == m_connectionAttemptGeneration;
        if (!validProtocol)
        {
            spdlog::error(
                "Rejected invalid authentication acceptance: revision={}, serverCapabilities={:#x}, negotiatedCapabilities={:#x}, serverNonce={}, connectionGeneration={}, sessionMatch={}, attemptMatch={}",
                acMessage.GameplayProtocolRevision,
                acMessage.ServerCapabilities,
                acMessage.NegotiatedCapabilities,
                acMessage.ServerInstanceNonce,
                acMessage.ConnectionGeneration,
                acMessage.ClientSessionNonce == m_sessionId,
                acMessage.ConnectionAttempt == m_connectionAttemptGeneration);

            ConnectionErrorEvent errorEvent;
            errorEvent.ErrorDetail = "{\"error\":\"invalid_authentication_acceptance\"}";
            Client::Close();
            m_dispatcher.trigger(errorEvent);
            return;
        }

        const bool authenticationGenerationChanged =
            m_serverInstanceNonce != acMessage.ServerInstanceNonce ||
            m_connectionGeneration != acMessage.ConnectionGeneration;

#if TP_SKYRIM_VR
        if (authenticationGenerationChanged &&
            m_serverInstanceNonce != 0 && m_connectionGeneration != 0)
            m_gameplayIdentityClearPending = true;

        RetryGameplayRetirement();
        if (m_gameplayCleanupRequired || m_gameplayRetirement.Pending ||
            m_gameplayIdentityClearPending)
        {
            spdlog::error("Rejected authentication acceptance while the prior CommonLib gameplay session is retiring");
            ConnectionErrorEvent errorEvent;
            errorEvent.ErrorDetail = "{\"error\":\"gameplay_cleanup_required\"}";
            Client::Close();
            m_dispatcher.trigger(errorEvent);
            return;
        }
#endif

        if (authenticationGenerationChanged)
        {
            ClearGameplayRetirementState();
            m_gameplayCleanupRequired = false;
            m_gameplayIdentityClearPending = false;
        }

        m_connected = true;
        m_localPlayerId = acMessage.PlayerId;
        m_connectionGeneration = acMessage.ConnectionGeneration;
        m_serverInstanceNonce = acMessage.ServerInstanceNonce;
        m_negotiatedGameplayCapabilities = acMessage.NegotiatedCapabilities;
#if TP_SKYRIM_VR
        SkyrimTogetherVR::GameplayBridgeClient::UpdateSessionIdentity(
            m_serverInstanceNonce,
            m_connectionGeneration);
#endif
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.identity.done");

        m_world.SetServerSettings(acMessage.Settings);
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.world_settings.done");

        m_dispatcher.trigger(acMessage.UserMods);
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.user_mods.done");
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.settings.begin");
        m_dispatcher.trigger(acMessage.Settings);
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.settings.done");
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.connected_dispatch.begin");
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId));
        SkyrimTogetherVR::LogRuntimeCheckpoint("auth.connected_dispatch.done");
        return; // quit the function here.
    }

    // error finding

    TiltedPhoques::String ErrorInfo;

    ErrorInfo = "{";

    switch (acMessage.Type)
    {
    case AR::kWrongVersion:
        ErrorInfo += "\"error\": \"wrong_version\", \"data\": {";
        ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);
        ErrorInfo += "}";
        break;
    case AR::kModsMismatch:
    {
        ErrorInfo += "\"error\": \"mods_mismatch\", \"data\": {\"mods\": [";
        bool first = true;
        for (const auto& m : acMessage.UserMods.ModList)
        {
            if (!first)
                ErrorInfo += ",";
            ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
            first = false;
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kClientModsDisallowed:
    {
        ErrorInfo += "\"error\": \"client_mods_disallowed\", \"data\": { \"mods\": [";
        if (acMessage.SKSEActive)
            ErrorInfo += "\"SKSE\"";
        if (acMessage.MO2Active)
            if (acMessage.SKSEActive)
                ErrorInfo += ",";
        ErrorInfo += "\"MO2\"";
        ErrorInfo += "]}";
        break;
    }
    case AR::kWrongPassword:
    {
        ErrorInfo += "\"error\": \"wrong_password\"";
        break;
    }
    case AR::kServerFull:
    {
        ErrorInfo += "\"error\": \"server_full\"";
        break;
    }
    case AR::kProtocolMismatch:
    {
        ErrorInfo += fmt::format(
            "\"error\": \"protocol_mismatch\", \"data\": {{\"expectedRevision\": {}, \"serverCapabilities\": {}, \"clientCapabilities\": {}}}",
            acMessage.GameplayProtocolRevision,
            acMessage.ServerCapabilities,
            m_requestedGameplayCapabilities);
        break;
    }
    default: ErrorInfo += "\"error\": \"no_reason\""; break;
    }

    ErrorInfo += "}";

    ConnectionErrorEvent errorEvent;
    if (!ErrorInfo.empty())
    {
        spdlog::error(ErrorInfo.c_str());
        errorEvent.ErrorDetail = std::move(ErrorInfo);
    }

    m_dispatcher.trigger(errorEvent);
}

bool TransportService::RetireGameplaySession(const SkyrimTogetherVR::GameplayBridge::EpochRetireReason aReason) noexcept
{
#if TP_SKYRIM_VR
    if (std::this_thread::get_id() != m_ownerThreadId)
        return false;

    const auto retiringLifecycleEpoch = SkyrimTogetherVR::GameplayBridgeClient::GetLifecycleEpoch();
    auto& request = m_gameplayRetirement;
    if (request.Completed)
    {
        if (IsGameplayRetirementRequestCurrent() &&
            retiringLifecycleEpoch == request.ExpectedLifecycleEpoch)
            return true;

        ClearGameplayRetirementState();
    }

    if (request.Pending)
    {
        if (!IsGameplayRetirementRequestCurrent())
        {
            ClearGameplayRetirementState();
            m_gameplayCleanupRequired = false;
        }
        else if (request.OriginLifecycleEpoch == 0 || request.ExpectedLifecycleEpoch == 0)
        {
            m_gameplayCleanupRequired = true;
            return false;
        }
        else if (retiringLifecycleEpoch == request.ExpectedLifecycleEpoch)
        {
            CompleteGameplayRetirement();
            return true;
        }
        else if (retiringLifecycleEpoch == request.OriginLifecycleEpoch)
        {
            m_gameplayCleanupRequired = true;
            return false;
        }
    }

    BeginGameplayRetirement(aReason, retiringLifecycleEpoch);
    return AttemptGameplayRetirement();
#else
    TP_UNUSED(aReason);
    m_gameplayCleanupRequired = false;
    return true;
#endif
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}
