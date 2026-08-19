#pragma once

#include <cmath>
#include <cstdint>

namespace SkyrimTogetherVR::GameplayAdapter::FaderRecoveryPolicy
{
inline constexpr std::uint64_t kFaderHardTimeoutMs = 10000;
inline constexpr std::uint64_t kStableContextDwellMs = 1000;
inline constexpr std::uint64_t kHideVerificationTimeoutMs = 2000;
inline constexpr float kStablePositionTolerance = 8.0F;

struct PlayerContext
{
    std::uintptr_t Player{};
    std::uintptr_t Base{};
    std::uintptr_t Cell{};
    std::uint32_t PlayerFormId{};
    std::uint32_t BaseFormId{};
    std::uint32_t CellFormId{};
    float PositionX{};
    float PositionY{};
    float PositionZ{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Player != 0 && Base != 0 && Cell != 0 && PlayerFormId != 0 && BaseFormId != 0 && CellFormId != 0 && std::isfinite(PositionX) && std::isfinite(PositionY) &&
               std::isfinite(PositionZ);
    }

    [[nodiscard]] bool HasSamePlayerIdentity(const PlayerContext& acOther) const noexcept
    {
        return Player == acOther.Player && Base == acOther.Base && PlayerFormId == acOther.PlayerFormId && BaseFormId == acOther.BaseFormId;
    }

    [[nodiscard]] bool HasSameContext(const PlayerContext& acOther) const noexcept
    {
        return HasSamePlayerIdentity(acOther) && Cell == acOther.Cell && CellFormId == acOther.CellFormId;
    }

    [[nodiscard]] bool IsPositionStableWith(const PlayerContext& acOther) const noexcept
    {
        return std::fabs(PositionX - acOther.PositionX) <= kStablePositionTolerance && std::fabs(PositionY - acOther.PositionY) <= kStablePositionTolerance &&
               std::fabs(PositionZ - acOther.PositionZ) <= kStablePositionTolerance;
    }
};

struct Observation
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    bool UiAvailable{};
    bool ExactHudAndFader{};
    bool FaderOnStack{};
    bool FaderActive{};
    bool TransitionActive{};
    bool CanQueueHide{};
    PlayerContext Context{};
    std::uint64_t LifecycleGeneration{};
    std::uint64_t MenuGeneration{};
};

enum class Action
{
    None,
    Candidate,
    Hide,
    Verified,
    Suppressed,
};

class StateMachine
{
public:
    [[nodiscard]] Action Observe(const Observation& acObservation, const std::uint64_t aNowMs) noexcept
    {
        const bool sessionActive = acObservation.ServerInstanceNonce != 0 && acObservation.ConnectionGeneration != 0;
        if (!sessionActive)
        {
            m_serverInstanceNonce = 0;
            m_connectionGeneration = 0;
            m_lifecycleGeneration = acObservation.LifecycleGeneration;
            m_menuGeneration = acObservation.MenuGeneration;
            m_lastObservedContext = {};
            ResetForLifecycle();
            return Action::None;
        }

        const bool hadSession = m_serverInstanceNonce != 0 && m_connectionGeneration != 0;
        const bool sessionChanged = acObservation.ServerInstanceNonce != m_serverInstanceNonce || acObservation.ConnectionGeneration != m_connectionGeneration;
        const bool sessionRolloverWithFader = sessionChanged && hadSession && (m_faderOnStack || acObservation.FaderOnStack);
        if (sessionChanged)
        {
            m_serverInstanceNonce = acObservation.ServerInstanceNonce;
            m_connectionGeneration = acObservation.ConnectionGeneration;
            ResetForLifecycle();
        }

        const bool lifecycleChanged = acObservation.LifecycleGeneration != m_lifecycleGeneration;
        if (lifecycleChanged)
        {
            m_lifecycleGeneration = acObservation.LifecycleGeneration;
            ResetForLifecycle();
        }

        const bool cellContextChanged = m_lastObservedContext.IsValid() && acObservation.Context.IsValid() && m_lastObservedContext.HasSamePlayerIdentity(acObservation.Context) &&
                                        !m_lastObservedContext.HasSameContext(acObservation.Context);
        if (acObservation.Context.IsValid())
            m_lastObservedContext = acObservation.Context;

        if (acObservation.MenuGeneration != m_menuGeneration)
        {
            m_menuGeneration = acObservation.MenuGeneration;
            ResetCandidate();
        }

        // Do not infer a close, verify a hide timeout, or start a new fader
        // generation from an incomplete UI observation.
        if (!acObservation.UiAvailable)
        {
            ResetCandidate();
            return Action::None;
        }

        if (!acObservation.FaderOnStack)
        {
            const auto action = m_state == State::HideIssued ? Action::Verified : Action::None;
            m_faderOnStack = false;
            m_faderActive = false;
            m_state = State::Monitoring;
            m_faderOnStackSinceMs = 0;
            m_generationIdentity = {};
            ResetCandidate();
            return action;
        }

        if (!m_faderOnStack)
        {
            m_faderOnStack = true;
            BeginGeneration(aNowMs, acObservation.FaderActive);
        }
        else if (m_faderActive != acObservation.FaderActive)
        {
            m_faderActive = acObservation.FaderActive;
            ResetCandidate();
        }

        if (sessionRolloverWithFader || cellContextChanged)
            m_recoveryEvidence = true;

        if (!m_generationIdentity.IsValid() && acObservation.Context.IsValid())
        {
            m_generationIdentity = acObservation.Context;
        }
        else if (m_generationIdentity.IsValid() && acObservation.Context.IsValid() && !m_generationIdentity.HasSamePlayerIdentity(acObservation.Context))
        {
            BeginGeneration(aNowMs, acObservation.FaderActive);
            m_generationIdentity = acObservation.Context;
        }

        if (m_state == State::HideIssued)
        {
            if (ElapsedAtLeast(aNowMs, m_hideIssuedAtMs, kHideVerificationTimeoutMs))
            {
                m_state = State::Suppressed;
                return Action::Suppressed;
            }
            return Action::None;
        }
        if (m_state == State::Suppressed)
            return Action::None;

        if (!CanBecomeCandidate(acObservation, aNowMs))
        {
            ResetCandidate();
            return Action::None;
        }

        if (!m_hasCandidate)
        {
            m_candidateSinceMs = aNowMs;
            m_candidateContext = acObservation.Context;
            m_hasCandidate = true;
            return Action::Candidate;
        }
        if (!m_candidateContext.HasSameContext(acObservation.Context) || !m_candidateContext.IsPositionStableWith(acObservation.Context))
        {
            m_candidateSinceMs = aNowMs;
            m_candidateContext = acObservation.Context;
            return Action::Candidate;
        }
        if (!ElapsedAtLeast(aNowMs, m_candidateSinceMs, kStableContextDwellMs) || !acObservation.CanQueueHide)
            return Action::None;

        m_state = State::HideIssued;
        m_hideIssuedAtMs = aNowMs;
        return Action::Hide;
    }

    [[nodiscard]] std::uint64_t Generation() const noexcept { return m_generation; }

private:
    enum class State
    {
        Monitoring,
        HideIssued,
        Suppressed,
    };

    [[nodiscard]] static bool ElapsedAtLeast(const std::uint64_t aNowMs, const std::uint64_t aSinceMs, const std::uint64_t aDurationMs) noexcept
    {
        return aNowMs >= aSinceMs && aNowMs - aSinceMs >= aDurationMs;
    }

    [[nodiscard]] bool CanBecomeCandidate(const Observation& acObservation, const std::uint64_t aNowMs) const noexcept
    {
        if (!acObservation.UiAvailable || !acObservation.ExactHudAndFader || !acObservation.FaderOnStack || acObservation.TransitionActive || !acObservation.Context.IsValid())
            return false;

        if (!m_recoveryEvidence)
            return false;

        // isActive is an engine-side activity bit, not a presentation proof. A
        // lone Fader is eligible only after the same hard timeout whether the
        // bit reports active or inactive.
        return ElapsedAtLeast(aNowMs, m_faderOnStackSinceMs, kFaderHardTimeoutMs);
    }

    void BeginGeneration(const std::uint64_t aNowMs, const bool aFaderActive) noexcept
    {
        ++m_generation;
        m_state = State::Monitoring;
        m_faderOnStackSinceMs = aNowMs;
        m_faderActive = aFaderActive;
        m_hideIssuedAtMs = 0;
        m_recoveryEvidence = false;
        ResetCandidate();
    }

    void ResetCandidate() noexcept
    {
        m_hasCandidate = false;
        m_candidateSinceMs = 0;
        m_candidateContext = {};
    }

    void ResetForLifecycle() noexcept
    {
        m_faderOnStack = false;
        m_faderActive = false;
        m_state = State::Monitoring;
        m_faderOnStackSinceMs = 0;
        m_generationIdentity = {};
        m_hideIssuedAtMs = 0;
        m_recoveryEvidence = false;
        ResetCandidate();
    }

    State m_state{State::Monitoring};
    PlayerContext m_candidateContext{};
    PlayerContext m_generationIdentity{};
    PlayerContext m_lastObservedContext{};
    std::uint64_t m_serverInstanceNonce{};
    std::uint64_t m_connectionGeneration{};
    std::uint64_t m_lifecycleGeneration{};
    std::uint64_t m_menuGeneration{};
    std::uint64_t m_generation{};
    std::uint64_t m_faderOnStackSinceMs{};
    std::uint64_t m_candidateSinceMs{};
    std::uint64_t m_hideIssuedAtMs{};
    bool m_hasCandidate{};
    bool m_faderOnStack{};
    bool m_faderActive{};
    bool m_recoveryEvidence{};
};
} // namespace SkyrimTogetherVR::GameplayAdapter::FaderRecoveryPolicy
