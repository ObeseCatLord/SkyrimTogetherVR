#include <TiltedOnlinePCH.h>

#include <Services/VRLifecycleService.h>

#include <Forms/TESForm.h>
#include <Forms/TESObjectCELL.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <PlayerCharacter.h>
#include <VR/VRMemorySafety.h>
#include <VR/VRPlayerReadiness.h>
#include <World.h>
#include <vr_common/VRHandoffPath.h>

#include <fstream>
#include <atomic>

namespace
{
constexpr uint32_t kRequiredStableTicks = 5;
constexpr auto kRequiredStableDuration = std::chrono::milliseconds(250);
constexpr double kStatusWriteInterval = 1.0;
constexpr char kStatusFileName[] = "SkyrimTogetherVR.lifecycle";

const char* GetBlockingMenuName() noexcept
{
    auto* pUI = UI::Get();
    if (!pUI || !SkyrimTogetherVR::IsReadableVrMemory(pUI, sizeof(void*)))
    {
        static std::atomic_flag s_reportedUnreadableUi = ATOMIC_FLAG_INIT;
        if (!s_reportedUnreadableUi.test_and_set(std::memory_order_relaxed))
            spdlog::warn("SkyrimTogetherVR UI singleton is not readable yet; deferring menu probes");
        return "ui_unavailable";
    }

    static const BSFixedString s_mainMenu("Main Menu");
    static const BSFixedString s_raceSexMenu("RaceSex Menu");
    static const BSFixedString s_loadingMenu("Loading Menu");
    static const BSFixedString s_faderMenu("Fader Menu");

    if (pUI->GetMenuOpen(s_mainMenu))
        return "main_menu";
    if (pUI->GetMenuOpen(s_raceSexMenu))
        return "racesex_menu";
    if (pUI->GetMenuOpen(s_loadingMenu))
        return "loading_menu";
    if (pUI->GetMenuOpen(s_faderMenu))
        return "fader_menu";
    return nullptr;
}
} // namespace

VRLifecycleService::VRLifecycleService(World& aWorld) noexcept
    : m_world(aWorld)
    , m_statusPath(SkyrimTogetherVR::Handoff::GetDirectory() / kStatusFileName)
{
    std::error_code ec;
    std::filesystem::create_directories(m_statusPath.parent_path(), ec);

#if TP_SKYRIM_VR
    spdlog::info("SkyrimTogetherVR lifecycle load-event sink is disabled until BSTEventSource::AddEventSink has an exact VR target");
#else
    if (auto* pEvents = EventDispatcherManager::Get())
        pEvents->GetLoadGameEventData().RegisterSink(this);
    else
        spdlog::warn("VRLifecycleService could not register TESLoadGameEvent sink");
#endif

    spdlog::info("SkyrimTogetherVR lifecycle status file: {}", m_statusPath.string());
    WriteStatusFile();
}

const char* VRLifecycleService::GetStateName() const noexcept
{
    switch (m_state)
    {
    case State::Boot: return "boot";
    case State::Suspended: return "suspended";
    case State::Stabilizing: return "stabilizing";
    case State::Ready: return "ready";
    case State::Teardown: return "teardown";
    }

    return "unknown";
}

void VRLifecycleService::Update(double aDelta) noexcept
{
    if (m_state == State::Teardown)
        return;

    const auto threadId = GetCurrentThreadId();
    if (m_ownerThreadId == 0)
    {
        m_ownerThreadId = threadId;
        m_statusDirty = true;
        spdlog::info("SkyrimTogetherVR lifecycle latched update owner thread {}", threadId);
    }
    else if (m_ownerThreadId != threadId)
    {
        Suspend("owner_thread_changed", true);
        spdlog::critical("SkyrimTogetherVR lifecycle rejected update on thread {}; owner is {}", threadId, m_ownerThreadId);
        WriteStatusFile();
        return;
    }

    if (m_loadInvalidated.exchange(false, std::memory_order_acq_rel))
    {
        Suspend("load_event", true);
        m_statusTimer = kStatusWriteInterval;
    }

    if (const char* pBlockingMenu = GetBlockingMenuName())
    {
        Suspend(pBlockingMenu);
    }
    else
    {
        auto* pPlayer = SkyrimTogetherVR::TryGetReadablePlayerForVR();
        auto* pBase = pPlayer ? pPlayer->GetBaseFormData() : nullptr;
        auto* pCell = pPlayer ? pPlayer->GetParentCellData() : nullptr;
        if (!pPlayer || !pBase || !pCell)
        {
            Suspend("player_unavailable");
        }
        else
        {
            Sample sample{};
            sample.Player = reinterpret_cast<std::uintptr_t>(pPlayer);
            sample.Base = reinterpret_cast<std::uintptr_t>(pBase);
            sample.Cell = reinterpret_cast<std::uintptr_t>(pCell);
            sample.PlayerFormId = pPlayer->GetFormIdData();
            sample.BaseFormId = pBase->GetFormIdData();
            sample.CellFormId = pCell->GetFormIdData();

            if (sample.PlayerFormId == 0 || sample.BaseFormId == 0 || sample.CellFormId == 0)
            {
                Suspend("player_identity_incomplete");
            }
            else if (m_state != State::Stabilizing && m_state != State::Ready)
            {
                StartStabilizing(sample);
            }
            else if (!(sample == (m_state == State::Ready ? m_readySample : m_candidateSample)))
            {
                Suspend("player_identity_changed", true);
                StartStabilizing(sample);
            }
            else if (m_state == State::Stabilizing)
            {
                ++m_stableTickCount;
                const auto stableFor = std::chrono::steady_clock::now() - m_candidateSince;
                if (m_stableTickCount >= kRequiredStableTicks && stableFor >= kRequiredStableDuration)
                {
                    m_readySample = m_candidateSample;
                    m_state = State::Ready;
                    m_suspendReason.clear();
                    m_statusDirty = true;
                    spdlog::info(
                        "SkyrimTogetherVR lifecycle ready: epoch={} player={:X} base={:X} cell={:X} thread={}", m_epoch,
                        m_readySample.PlayerFormId, m_readySample.BaseFormId, m_readySample.CellFormId, m_ownerThreadId);
                }
            }
        }
    }

    m_statusTimer += aDelta;
    if (m_statusDirty || m_statusTimer >= kStatusWriteInterval)
    {
        m_statusTimer = 0.0;
        WriteStatusFile();
    }
}

void VRLifecycleService::BeginTeardown() noexcept
{
    if (m_state == State::Teardown)
        return;

    ++m_epoch;
    m_state = State::Teardown;
    m_suspendReason = "teardown";
    m_candidateSample = {};
    m_readySample = {};
    m_stableTickCount = 0;
    m_statusDirty = true;
    WriteStatusFile();
}

BSTEventResult VRLifecycleService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    m_loadInvalidated.store(true, std::memory_order_release);
    return BSTEventResult::kOk;
}

void VRLifecycleService::Suspend(const char* apReason, bool aAdvanceEpoch) noexcept
{
    const bool stateChanged = m_state != State::Suspended;
    const bool reasonChanged = m_suspendReason != apReason;
    if (stateChanged || aAdvanceEpoch)
        ++m_epoch;

    if (stateChanged || reasonChanged || aAdvanceEpoch)
    {
        spdlog::info("SkyrimTogetherVR lifecycle suspended: epoch={} reason={}", m_epoch, apReason);
        m_statusDirty = true;
    }

    m_state = State::Suspended;
    m_suspendReason = apReason;
    m_candidateSample = {};
    m_readySample = {};
    m_stableTickCount = 0;
}

void VRLifecycleService::StartStabilizing(const Sample& acSample) noexcept
{
    m_state = State::Stabilizing;
    m_candidateSample = acSample;
    m_candidateSince = std::chrono::steady_clock::now();
    m_stableTickCount = 1;
    m_suspendReason = "stabilizing";
    m_statusDirty = true;
}

void VRLifecycleService::WriteStatusFile() noexcept
{
    std::error_code ec;
    std::filesystem::create_directories(m_statusPath.parent_path(), ec);

    std::ofstream file(m_statusPath, std::ios::trunc);
    if (!file)
        return;

    file << "state=" << GetStateName() << "\n";
    file << "ready=" << (IsReady() ? "1" : "0") << "\n";
    file << "epoch=" << m_epoch << "\n";
    file << "ownerThreadId=" << m_ownerThreadId << "\n";
    file << "stableTickCount=" << m_stableTickCount << "\n";
    file << "playerFormId=" << m_readySample.PlayerFormId << "\n";
    file << "playerBaseFormId=" << m_readySample.BaseFormId << "\n";
    file << "playerCellFormId=" << m_readySample.CellFormId << "\n";
    if (!m_suspendReason.empty())
        file << "reason=" << m_suspendReason << "\n";

    m_statusDirty = false;
}
