#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

#include <entt/entt.hpp>
#include <vr_common/VRGameplayBridge.h>

struct ConnectedEvent;
struct DisconnectedEvent;
struct AssignObjectsResponse;
struct NotifyActivate;
struct NotifyActorTeleport;
struct NotifyChatMessageBroadcast;
struct NotifyDialogue;
struct NotifyLockChange;
struct NotifyNewPackage;
struct NotifyObjectInventoryChanges;
struct NotifyPlayerDialogue;
struct NotifyPartyInfo;
struct NotifyQuestUpdate;
struct NotifyRemoveWaypoint;
struct NotifyScriptAnimation;
struct NotifySetWaypoint;
struct NotifySubtitle;
struct NotifyTeleport;
struct NotifyWeatherChange;
struct PartyJoinedEvent;
struct PartyLeftEvent;
struct PlayerDialogueEvent;
struct ServerSettings;
struct ServerTimeSettings;
struct TeleportCommandResponse;
struct TransportService;
struct VRAvatarService;
struct World;
struct UpdateEvent;
namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
struct RemoteGameplayBridgeResultEvent;
}

struct VRWorldReplicationService
{
    VRWorldReplicationService(World& aWorld, entt::dispatcher& aDispatcher,
                              TransportService& aTransport, VRAvatarService& aAvatars) noexcept;
    ~VRWorldReplicationService() noexcept = default;

    TP_NOCOPYMOVE(VRWorldReplicationService);

private:
    struct RetainedState
    {
        std::uint64_t ServerInstanceNonce{0};
        std::uint64_t ConnectionGeneration{0};
        std::uint64_t Version{0};
        std::uint64_t SubmittedVersion{0};
        std::uint64_t InFlightActionId{0};
        std::uint32_t TargetLocalFormId{0};
        std::uint32_t LocalFormIdA{0};
        std::int32_t ValueA{0};
        std::int32_t ValueB{0};
        float ScalarA{0.0F};
        float ScalarB{0.0F};
        float ScalarC{0.0F};
        std::uint32_t ActionFlags{0};
        std::uint8_t Attempts{0};
        bool Valid{false};
        bool Dirty{false};
        bool InFlight{false};
    };

    void OnActivate(const NotifyActivate& acMessage) noexcept;
    void OnAssignObjects(const AssignObjectsResponse& acMessage) noexcept;
    void OnActorTeleport(const NotifyActorTeleport& acMessage) noexcept;
    void OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept;
    void OnDialogue(const NotifyDialogue& acMessage) noexcept;
    void OnLockChange(const NotifyLockChange& acMessage) noexcept;
    void OnNewPackage(const NotifyNewPackage& acMessage) noexcept;
    void OnObjectInventory(const NotifyObjectInventoryChanges& acMessage) noexcept;
    void OnPlayerDialogue(const NotifyPlayerDialogue& acMessage) noexcept;
    void OnQuestUpdate(const NotifyQuestUpdate& acMessage) noexcept;
    void OnRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept;
    void OnScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept;
    void OnSetWaypoint(const NotifySetWaypoint& acMessage) noexcept;
    void OnSubtitle(const NotifySubtitle& acMessage) noexcept;
    void OnTeleport(const NotifyTeleport& acMessage) noexcept;
    void OnTeleportCommand(const TeleportCommandResponse& acMessage) noexcept;
    void OnTimeSettings(const ServerTimeSettings& acMessage) noexcept;
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;
    void OnServerSettings(const ServerSettings& acSettings) noexcept;
    void OnPartyJoined(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeft(const PartyLeftEvent& acEvent) noexcept;
    void OnPartyInfo(const NotifyPartyInfo& acMessage) noexcept;
    void OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) noexcept;
    void OnLocalGameplay(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnLocalGameplayText(const SkyrimTogetherVR::GameplayBridge::EventRecord& acRecord) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;

    [[nodiscard]] bool CaptureSession(RetainedState& arState) const noexcept;
    void RetainState(RetainedState& arState, std::uint32_t aTargetLocalFormId, std::uint32_t aLocalFormIdA,
                     std::int32_t aValueA, std::int32_t aValueB, float aScalarA, float aScalarB,
                     float aScalarC, std::uint32_t aActionFlags = 0) noexcept;
    void SubmitReleaseWeather() noexcept;
    void RetainServerSettings(const ServerSettings& acSettings) noexcept;
    void ObserveSession() noexcept;
    void Reconcile() noexcept;
    void ReconcileState(RetainedState& arState, std::uint16_t aDomain, std::uint16_t aAction) noexcept;
    void ResetRetainedState() noexcept;
    void DiscardRetainedStateForSession(std::uint64_t aServerInstanceNonce,
                                        std::uint64_t aConnectionGeneration) noexcept;
    void ResetInFlightState() noexcept;
    void SubmitText(SkyrimTogetherVR::GameplayBridge::CommandRecord aBase,
                    std::uint64_t aTextId, std::string_view acText) noexcept;
    void RetryPendingText() noexcept;

    World& m_world;
    TransportService& m_transport;
    VRAvatarService& m_avatars;
    std::uint64_t m_nextTextId{1};
    std::uint64_t m_observedServerInstanceNonce{0};
    std::uint64_t m_observedConnectionGeneration{0};
    std::uint64_t m_observedLifecycleEpoch{0};
    double m_reconcileTimer{0.0};
    double m_textRetryTimer{0.0};
    RetainedState m_calendarState{};
    RetainedState m_weatherState{};
    RetainedState m_settingsState{};
    struct WaypointEchoSuppression
    {
        std::uint32_t LocalWorldspaceFormId{0};
        float PositionX{0.0F};
        float PositionY{0.0F};
        float PositionZ{0.0F};
        double Remaining{0.0};
        bool Remove{false};
        bool Valid{false};
    };
    WaypointEchoSuppression m_waypointEcho{};
    struct DialogueTextAssembly
    {
        std::uint64_t ActionId{0};
        std::uint64_t TextId{0};
        std::uint32_t TargetLocalFormId{0};
        std::uint16_t ChunkCount{0};
        std::uint16_t ReceivedMask{0};
        std::array<std::uint16_t, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks> Lengths{};
        std::array<char, SkyrimTogetherVR::GameplayBridge::kMaximumGameplayTextChunks *
                             SkyrimTogetherVR::GameplayBridge::kGameplayTextBytesPerChunk> Bytes{};
    };
    DialogueTextAssembly m_dialogueText{};
    struct PendingTextTransaction
    {
        SkyrimTogetherVR::GameplayBridge::CommandRecord Base{};
        std::uint64_t TextId{0};
        std::string Text{};
        std::uint8_t Attempts{0};
    };
    std::deque<PendingTextTransaction> m_pendingText{};
    bool m_partyRoleKnown{false};
    bool m_partyLeader{false};
    std::array<std::uint64_t, 18> m_lastLocalActionIdByDomain{};
    std::unordered_map<std::uint32_t, RetainedState> m_lockStates{};
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_assignObjectsConnection;
    entt::scoped_connection m_actorTeleportConnection;
    entt::scoped_connection m_chatConnection;
    entt::scoped_connection m_dialogueConnection;
    entt::scoped_connection m_lockConnection;
    entt::scoped_connection m_packageConnection;
    entt::scoped_connection m_objectInventoryConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_questConnection;
    entt::scoped_connection m_removeWaypointConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_setWaypointConnection;
    entt::scoped_connection m_subtitleConnection;
    entt::scoped_connection m_teleportConnection;
    entt::scoped_connection m_teleportCommandConnection;
    entt::scoped_connection m_timeConnection;
    entt::scoped_connection m_weatherConnection;
    entt::scoped_connection m_settingsConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_partyInfoConnection;
    entt::scoped_connection m_playerDialogueEventConnection;
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_gameplayResultConnection;
};
