#include <TiltedOnlinePCH.h>

#include <atomic>
#include <chrono>
#include <mutex>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct Main;

#if TP_SKYRIM_VR

// This target is only observed in Skyrim VR. Its cadence must be proven before
// any client or engine work is scheduled from it.
TP_THIS_FUNCTION(TMainLoop, short, Main);
static TMainLoop* MainLoop = nullptr;
static std::once_flag s_mainLoopLogOnce;
static std::atomic_uint64_t s_mainLoopCallCount{0};
static const auto s_mainLoopStartTime = std::chrono::steady_clock::now();

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    const auto callCount = s_mainLoopCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (callCount <= 2 || callCount % 300 == 0)
    {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_mainLoopStartTime).count();
        spdlog::info("SkyrimTogetherVR main-loop cadence: call={} elapsedMs={} thread={}", callCount, elapsedMs, GetCurrentThreadId());
    }

    std::call_once(
        s_mainLoopLogOnce,
        []()
        {
            spdlog::info("SkyrimTogetherVR main-loop observer reached");
        });

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

static void InstallVrMainLoopObserver()
{
    POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
    MainLoop = cMainLoop.Get();

    spdlog::info("Installing SkyrimTogetherVR main-loop observer: mainLoop={}", fmt::ptr(MainLoop));
    TP_HOOK(&MainLoop, HookMainLoop);
}

void InstallVrMainLoopBringupHooks()
{
    InstallVrMainLoopObserver();
}

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
