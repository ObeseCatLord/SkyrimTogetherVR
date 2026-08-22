#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

struct DisconnectedEvent;
struct NotifyPlayerRespawn;
struct TransportService;
struct UpdateEvent;
struct VRAvatarService;
struct VRLocalGameplayService;
struct World;

namespace SkyrimTogetherVR
{
struct LocalGameplayBridgeEvent;
struct RemoteGameplayBridgeResultEvent;
}

namespace SkyrimTogetherVR::RespawnGoldReconciliation
{
constexpr std::uint8_t kMaximumAttempts = 3;
constexpr double kInitialRetryDelaySeconds = 0.25;
constexpr double kMaximumRetryDelaySeconds = 1.0;

struct SessionScope
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
};

[[nodiscard]] constexpr bool IsValid(const SessionScope& acScope) noexcept
{
    return acScope.ServerInstanceNonce != 0 && acScope.ConnectionGeneration != 0 && acScope.LifecycleEpoch != 0;
}

[[nodiscard]] constexpr bool IsSame(const SessionScope& acLeft, const SessionScope& acRight) noexcept
{
    return acLeft.ServerInstanceNonce == acRight.ServerInstanceNonce &&
           acLeft.ConnectionGeneration == acRight.ConnectionGeneration &&
           acLeft.LifecycleEpoch == acRight.LifecycleEpoch;
}

enum class ResultKind : std::uint8_t
{
    Success,
    RetryableFailure,
    TerminalFailure,
    TimedOut,
};

enum class Disposition : std::uint8_t
{
    Ignored,
    Retry,
    SubmitNextChunk,
    Completed,
    RetireSession,
};

struct Resolution
{
    Disposition Next{Disposition::Ignored};
    double RetryDelaySeconds{};
};

struct PendingGoldLoss
{
    SessionScope Scope{};
    std::uint64_t ActionId{};
    std::int32_t RemainingGold{};
    std::int32_t ActiveChunk{};
    std::uint8_t Attempts{};
    bool Terminal{};
    bool Completed{};

    [[nodiscard]] constexpr bool HasPendingLoss() const noexcept
    {
        return IsValid(Scope) && RemainingGold > 0;
    }
};

[[nodiscard]] constexpr double RetryDelayForAttempts(const std::uint8_t aAttempts) noexcept
{
    return aAttempts <= 1 ? kInitialRetryDelaySeconds :
        (aAttempts == 2 ? kInitialRetryDelaySeconds * 2.0 : kMaximumRetryDelaySeconds);
}

[[nodiscard]] constexpr bool Begin(
    PendingGoldLoss& arWork, const SessionScope& acScope, const std::int32_t aGoldLoss) noexcept
{
    if (!IsValid(acScope) || aGoldLoss <= 0 || arWork.HasPendingLoss())
        return false;

    arWork = {};
    arWork.Scope = acScope;
    arWork.RemainingGold = aGoldLoss;
    return true;
}

[[nodiscard]] constexpr bool BeginAttempt(PendingGoldLoss& arWork, const std::int32_t aChunk) noexcept
{
    if (!arWork.HasPendingLoss() || arWork.Terminal || arWork.Completed || arWork.ActionId != 0 || aChunk <= 0 ||
        aChunk > arWork.RemainingGold || arWork.Attempts >= kMaximumAttempts)
        return false;

    arWork.ActiveChunk = aChunk;
    ++arWork.Attempts;
    return true;
}

[[nodiscard]] constexpr bool BindAction(PendingGoldLoss& arWork, const std::uint64_t aActionId) noexcept
{
    if (!arWork.HasPendingLoss() || arWork.Terminal || arWork.Completed || arWork.ActiveChunk <= 0 ||
        arWork.ActionId != 0 || aActionId == 0)
        return false;

    arWork.ActionId = aActionId;
    return true;
}

[[nodiscard]] constexpr bool MatchesResult(
    const PendingGoldLoss& acWork, const SessionScope& acScope, const std::uint64_t aActionId) noexcept
{
    return acWork.HasPendingLoss() && !acWork.Terminal && !acWork.Completed && acWork.ActionId != 0 &&
           acWork.ActionId == aActionId && IsSame(acWork.Scope, acScope);
}

[[nodiscard]] constexpr Resolution ResolveSubmissionFailure(PendingGoldLoss& arWork) noexcept
{
    if (!arWork.HasPendingLoss() || arWork.Terminal || arWork.Completed || arWork.ActionId != 0 ||
        arWork.ActiveChunk <= 0)
        return {};

    arWork.ActiveChunk = 0;
    if (arWork.Attempts < kMaximumAttempts)
        return {Disposition::Retry, RetryDelayForAttempts(arWork.Attempts)};

    arWork.Terminal = true;
    return {Disposition::RetireSession, 0.0};
}

[[nodiscard]] constexpr Resolution ResolveResult(PendingGoldLoss& arWork, const SessionScope& acScope,
                                                  const std::uint64_t aActionId,
                                                  const ResultKind aResult) noexcept
{
    if (!MatchesResult(arWork, acScope, aActionId))
        return {};

    const auto chunk = arWork.ActiveChunk;
    arWork.ActionId = 0;
    arWork.ActiveChunk = 0;
    if (aResult == ResultKind::Success)
    {
        if (chunk <= 0 || chunk > arWork.RemainingGold)
        {
            arWork.Terminal = true;
            return {Disposition::RetireSession, 0.0};
        }

        arWork.RemainingGold -= chunk;
        arWork.Attempts = 0;
        if (arWork.RemainingGold != 0)
            return {Disposition::SubmitNextChunk, kInitialRetryDelaySeconds};

        arWork.Completed = true;
        return {Disposition::Completed, 0.0};
    }

    if (aResult == ResultKind::RetryableFailure && arWork.Attempts < kMaximumAttempts)
        return {Disposition::Retry, RetryDelayForAttempts(arWork.Attempts)};

    // A timeout is ambiguous: the native command may have mutated the game
    // after its result was lost, so replay would risk charging twice.
    arWork.Terminal = true;
    return {Disposition::RetireSession, 0.0};
}
} // namespace SkyrimTogetherVR::RespawnGoldReconciliation

/**
 * Owns the VR local-player death-to-respawn flow without using legacy game
 * wrappers. Bridge observations drive the timer and bridge commands perform
 * the local respawn and authoritative gold removal.
 */
struct VRDeathRespawnService
{
    VRDeathRespawnService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                          VRAvatarService& aAvatars, VRLocalGameplayService& aLocalGameplay) noexcept;
    ~VRDeathRespawnService() noexcept = default;

    TP_NOCOPYMOVE(VRDeathRespawnService);

private:
    void OnLocalGameplayBridgeEvent(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) noexcept;
    void OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) noexcept;
    void OnGameplayResult(const SkyrimTogetherVR::RemoteGameplayBridgeResultEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    [[nodiscard]] bool IsValidLocalDeathState(const SkyrimTogetherVR::LocalGameplayBridgeEvent& acEvent) const noexcept;
    void CancelRespawnTimer() noexcept;
    void ResetSessionState() noexcept;
    void SubmitRespawn() noexcept;
    void SubmitServerRespawnRequest() noexcept;
    void SubmitGoldLoss() noexcept;
    [[nodiscard]] SkyrimTogetherVR::RespawnGoldReconciliation::SessionScope GetGoldSessionScope() const noexcept;
    [[nodiscard]] bool IsGoldWorkCurrent() const noexcept;
    void ForceGoldLossSessionRetirement(std::string_view acReason) noexcept;
    void SubmitHudMessage(std::string_view acMessage) noexcept;
    void SubmitPendingHudMessage() noexcept;

    World& m_world;
    TransportService& m_transport;
    VRAvatarService& m_avatars;
    VRLocalGameplayService& m_localGameplay;
    std::uint32_t m_sessionServerId{0};
    std::uint32_t m_pendingServerId{0};
    std::uint64_t m_sessionLifecycleEpoch{0};
    std::uint64_t m_lastDeathActionId{0};
    std::uint64_t m_respawnActionId{0};
    std::uint64_t m_nextHudTextId{1};
    double m_respawnRemaining{0.0};
    double m_goldRetryRemaining{0.0};
    double m_respawnResultElapsed{0.0};
    double m_goldResultElapsed{0.0};
    double m_hudRetryRemaining{0.0};
    std::int32_t m_totalGoldLoss{0};
    std::uint8_t m_respawnAttempts{0};
    std::uint8_t m_hudAttempts{0};
    bool m_hasDeathActionId{false};
    bool m_deathObserved{false};
    bool m_serverRespawnPending{false};
    bool m_goldTerminalRetirementRequested{false};
    SkyrimTogetherVR::RespawnGoldReconciliation::PendingGoldLoss m_goldWork{};
    std::string m_pendingHudMessage{};
    entt::scoped_connection m_localGameplayConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_gameplayResultConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
};
