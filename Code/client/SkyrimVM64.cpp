#if defined(TP_SKYRIM_VR_STARTUP_MODE_TEST)
#include <atomic>
#include <cstdint>
#else
#include <TiltedOnlinePCH.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>

#include "TiltedOnlineApp.h"
#include "VRTickBridge.h"
#endif

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#if !defined(TP_SKYRIM_VR_STARTUP_MODE_TEST)
struct Main;
#endif

#if TP_SKYRIM_VR

enum class VrUpdateMode : std::uint8_t
{
    Off,
    Observe,
    Active,
};

namespace SkyrimTogetherVR::StartupMode
{
enum class MainDrawTransition
{
    None,
    BeginMain,
    AlreadyAttempted,
    OwnerMismatch,
};

class MainDrawStartupGate
{
  public:
    MainDrawTransition TryBegin(const VrUpdateMode aMode, const bool aOutermost, const std::uint32_t aThreadId) noexcept
    {
        if (aMode != VrUpdateMode::Active || !aOutermost || aThreadId == 0)
            return MainDrawTransition::None;

        auto expectedOwner = std::uint32_t{0};
        if (!m_ownerThreadId.compare_exchange_strong(expectedOwner, aThreadId, std::memory_order_acq_rel) && expectedOwner != aThreadId)
            return MainDrawTransition::OwnerMismatch;

        return m_beginMainAttempted.exchange(true, std::memory_order_acq_rel) ? MainDrawTransition::AlreadyAttempted : MainDrawTransition::BeginMain;
    }

  private:
    std::atomic_uint32_t m_ownerThreadId{0};
    std::atomic_bool m_beginMainAttempted{false};
};
} // namespace SkyrimTogetherVR::StartupMode

#if !defined(TP_SKYRIM_VR_STARTUP_MODE_TEST)

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;
extern void RunTiltedApp();

TP_THIS_FUNCTION(TMainDraw, void, Main, std::uint32_t, bool);
static TMainDraw* MainDraw = nullptr;
static std::once_flag s_mainDrawLogOnce;
static std::atomic_uint64_t s_mainDrawCallCount{0};
static std::atomic_uint64_t s_mainDrawOwnerCallCount{0};
static std::atomic_uint64_t s_mainDrawMismatchCallCount{0};
static std::atomic_uint64_t s_mainDrawUnavailableCallCount{0};
static std::atomic_uint64_t s_mainDrawLastTickMs{0};
static std::atomic_uint64_t s_mainDrawMaximumGapMs{0};
static std::atomic_uint64_t s_mainDrawMaximumOriginalUs{0};
static std::atomic_uint32_t s_mainDrawDepth{0};
static std::atomic_uint32_t s_mainDrawMaximumDepth{0};
static std::atomic_flag s_clientUpdateInProgress = ATOMIC_FLAG_INIT;
static std::atomic_uint64_t s_clientUpdateCount{0};
static VrUpdateMode s_updateMode{VrUpdateMode::Observe};
static SkyrimTogetherVR::StartupMode::MainDrawStartupGate s_mainDrawStartupGate;

template <class T>
static void UpdateMaximum(std::atomic<T>& arMaximum, T aValue) noexcept
{
    auto current = arMaximum.load(std::memory_order_relaxed);
    while (current < aValue && !arMaximum.compare_exchange_weak(current, aValue, std::memory_order_relaxed))
    {
    }
}

static const char* GetVrUpdateModeName() noexcept
{
    switch (s_updateMode)
    {
    case VrUpdateMode::Off: return "off";
    case VrUpdateMode::Observe: return "observe";
    case VrUpdateMode::Active: return "active";
    }

    return "unknown";
}

static VrUpdateMode ReadVrUpdateMode() noexcept
{
    const char* pValue = std::getenv("STVR_VM_UPDATE_MODE");
    if (!pValue || !pValue[0])
        return VrUpdateMode::Observe;

    std::string value(pValue);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "off")
        return VrUpdateMode::Off;
    if (value == "active")
        return VrUpdateMode::Active;
    if (value != "observe")
        spdlog::warn("Unknown STVR_VM_UPDATE_MODE='{}'; using observer mode", value);
    return VrUpdateMode::Observe;
}

void TP_MAKE_THISCALL(HookMainDraw, Main, std::uint32_t aUnk, bool aMainMenuOpen)
{
    const auto entryDepth = s_mainDrawDepth.fetch_add(1, std::memory_order_acq_rel) + 1;
    UpdateMaximum(s_mainDrawMaximumDepth, entryDepth);
    const auto originalStart = std::chrono::steady_clock::now();
    TiltedPhoques::ThisCall(MainDraw, apThis, aUnk, aMainMenuOpen);
    const auto originalUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - originalStart).count());
    UpdateMaximum(s_mainDrawMaximumOriginalUs, originalUs);
    const auto depthBeforeExit = s_mainDrawDepth.fetch_sub(1, std::memory_order_acq_rel);
    const bool outermost = entryDepth == 1 && depthBeforeExit == 1;

    const auto callCount = s_mainDrawCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto now = GetTickCount64();
    const auto previousTick = s_mainDrawLastTickMs.exchange(now, std::memory_order_relaxed);
    const auto gapMs = previousTick > 0 && now >= previousTick ? now - previousTick : 0;
    UpdateMaximum(s_mainDrawMaximumGapMs, gapMs);

    const auto threadId = GetCurrentThreadId();
    const auto observedOwnerThreadId = SkyrimTogetherVR::TickBridge::GetActivationThreadId();
    std::call_once(
        s_mainDrawLogOnce,
        [threadId, observedOwnerThreadId, entryDepth]()
        {
            spdlog::info(
                "SkyrimTogetherVR Main::Draw observer reached: mode={} thread={} owner={} depth={}",
                GetVrUpdateModeName(),
                threadId,
                observedOwnerThreadId,
                entryDepth);
        });

    if (callCount <= 2 || callCount % 6000 == 0)
    {
        const auto diagnostics = SkyrimTogetherVR::TickBridge::GetDiagnostics();
        spdlog::info(
            "SkyrimTogetherVR Main::Draw cadence: call={} mode={} thread={} owner={} depth={} maxDepth={} gapMs={} maxGapMs={} originalUs={} maxOriginalUs={} mainMenu={} permit={} produced={} consumed={}",
            callCount,
            GetVrUpdateModeName(),
            threadId,
            observedOwnerThreadId,
            entryDepth,
            s_mainDrawMaximumDepth.load(std::memory_order_relaxed),
            gapMs,
            s_mainDrawMaximumGapMs.load(std::memory_order_relaxed),
            originalUs,
            s_mainDrawMaximumOriginalUs.load(std::memory_order_relaxed),
            aMainMenuOpen,
            diagnostics.PermitPending,
            diagnostics.ProducedSequence,
            diagnostics.ConsumedSequence);
    }

    if (!outermost)
        return;

    if (s_updateMode != VrUpdateMode::Active)
        return;

    const auto startupTransition = s_mainDrawStartupGate.TryBegin(s_updateMode, outermost, threadId);
    if (startupTransition == SkyrimTogetherVR::StartupMode::MainDrawTransition::OwnerMismatch)
    {
        const auto count = s_mainDrawMismatchCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 2 || count % 6000 == 0)
            spdlog::warn("SkyrimTogetherVR Main::Draw startup-owner mismatch: call={} thread={}", count, threadId);
        return;
    }
    if (startupTransition == SkyrimTogetherVR::StartupMode::MainDrawTransition::BeginMain)
        RunTiltedApp();

    const auto ownerThreadId = SkyrimTogetherVR::TickBridge::GetActivationThreadId();
    if (ownerThreadId == 0)
    {
        const auto count = s_mainDrawUnavailableCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 2 || count % 6000 == 0)
            spdlog::info("SkyrimTogetherVR Main::Draw owner unavailable: call={} mode={} thread={}", count, GetVrUpdateModeName(), threadId);
        return;
    }
    if (threadId != ownerThreadId)
    {
        const auto count = s_mainDrawMismatchCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 2 || count % 6000 == 0)
            spdlog::warn("SkyrimTogetherVR Main::Draw owner mismatch: call={} mode={} thread={} owner={}", count, GetVrUpdateModeName(), threadId, ownerThreadId);
        return;
    }

    SkyrimTogetherVR::TickBridge::RecordOwnerHeartbeat();
    const auto ownerCount = s_mainDrawOwnerCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (ownerCount <= 2 || ownerCount % 6000 == 0)
        spdlog::info("SkyrimTogetherVR Main::Draw owner cadence: call={} mode={} thread={}", ownerCount, GetVrUpdateModeName(), threadId);

    if (s_updateMode != VrUpdateMode::Active || !g_appInstance)
        return;

    if (s_clientUpdateInProgress.test_and_set(std::memory_order_acquire))
    {
        spdlog::error("SkyrimTogetherVR rejected a reentrant Main::Draw client update on thread {}", threadId);
        return;
    }

    struct UpdateGuard
    {
        ~UpdateGuard() { s_clientUpdateInProgress.clear(std::memory_order_release); }
    } updateGuard;

    const auto beforeConsume = SkyrimTogetherVR::TickBridge::GetDiagnostics();
    if (!beforeConsume.Ready || beforeConsume.ActivationThreadId != threadId)
        return;

    std::uint64_t sequence = 0;
    if (!SkyrimTogetherVR::TickBridge::TryConsumeUpdatePermit(sequence))
        return;

    const auto beforeUpdate = SkyrimTogetherVR::TickBridge::GetDiagnostics();
    if (!beforeUpdate.Ready || beforeUpdate.ActivationThreadId != threadId || !g_appInstance)
        return;

    g_appInstance->Update();
    SkyrimTogetherVR::TickBridge::RecordOwnerUpdateCompleted(sequence);
    const auto updateCount = s_clientUpdateCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (updateCount <= 2 || updateCount % 6000 == 0)
        spdlog::info("SkyrimTogetherVR Main::Draw client update completed: count={} sequence={} thread={}", updateCount, sequence, threadId);
}

static void InstallVrMainDrawObserver()
{
    POINTER_SKYRIMSE(TMainDraw, cMainDraw, 35560);
    MainDraw = cMainDraw.Get();

    spdlog::info("Installing SkyrimTogetherVR Main::Draw owner observer: mainDraw=0x{:X}",
        reinterpret_cast<std::uintptr_t>(MainDraw));
    TP_HOOK(&MainDraw, HookMainDraw);
}

void InstallVrMainLoopBringupHooks()
{
    s_updateMode = ReadVrUpdateMode();
    spdlog::info("SkyrimTogetherVR VR update-owner runtime mode: {}", GetVrUpdateModeName());
    if (s_updateMode == VrUpdateMode::Off)
        return;

    InstallVrMainDrawObserver();
}

bool IsVrMainDrawStartupActiveMode() noexcept
{
    return s_updateMode == VrUpdateMode::Active;
}

#endif

#else

#include "TiltedOnlineApp.h"
#include <GameVM.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

struct VMContext
{
    char pad[0x680];
    uint8_t inactive; // 0x680
};

TP_THIS_FUNCTION(TVMUpdate, int, VMContext, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

int TP_MAKE_THISCALL(HookVMUpdate, VMContext, float a2)
{
    if (apThis->inactive == 0 && g_appInstance)
        g_appInstance->Update();

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static void InstallMainLoopHooks()
{
    POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
    POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926);
    POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412);

    VMUpdate = cVMUpdate.Get();
    MainLoop = cMainLoop.Get();
    VMDestructor = cVMDestructor.Get();

    TP_HOOK(&VMUpdate, HookVMUpdate);
    TP_HOOK(&MainLoop, HookMainLoop);
    TP_HOOK(&VMDestructor, HookVMDestructor);
}

void InstallVrMainLoopBringupHooks()
{
    InstallMainLoopHooks();
}

static TiltedPhoques::Initializer s_mainHooks([]() { InstallMainLoopHooks(); });

#endif
