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

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct Main;

#if TP_SKYRIM_VR

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

enum class VrVmUpdateMode
{
    Off,
    Observe,
    Active,
};

struct VrVmUpdateContext;

TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVrVmUpdate, void, VrVmUpdateContext, float);
static TMainLoop* MainLoop = nullptr;
static TVrVmUpdate* VrVmUpdate = nullptr;
static std::once_flag s_mainLoopLogOnce;
static std::atomic_uint64_t s_mainLoopCallCount{0};
static const auto s_mainLoopStartTime = std::chrono::steady_clock::now();
static std::atomic_uint64_t s_vmUpdateOwnerCallCount{0};
static std::atomic_uint64_t s_vmUpdateMismatchCallCount{0};
static std::atomic_uint64_t s_vmUpdateUnavailableCallCount{0};
static std::once_flag s_vmUpdateOwnerLogOnce;
static VrVmUpdateMode s_vmUpdateMode{VrVmUpdateMode::Observe};

static const char* GetVrVmUpdateModeName() noexcept
{
    switch (s_vmUpdateMode)
    {
    case VrVmUpdateMode::Off: return "off";
    case VrVmUpdateMode::Observe: return "observe";
    case VrVmUpdateMode::Active: return "active";
    }

    return "unknown";
}

static VrVmUpdateMode ReadVrVmUpdateMode() noexcept
{
    const char* pValue = std::getenv("STVR_VM_UPDATE_MODE");
    if (!pValue || !pValue[0])
        return VrVmUpdateMode::Observe;

    std::string value(pValue);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "off")
        return VrVmUpdateMode::Off;
    if (value == "active")
        return VrVmUpdateMode::Active;
    if (value != "observe")
        spdlog::warn("Unknown STVR_VM_UPDATE_MODE='{}'; using observer mode", value);
    return VrVmUpdateMode::Observe;
}

void TP_MAKE_THISCALL(HookVrVmUpdate, VrVmUpdateContext, float aDelta)
{
    const auto threadId = GetCurrentThreadId();
    const auto ownerThreadId = SkyrimTogetherVR::TickBridge::GetActivationThreadId();
    if (ownerThreadId == 0)
    {
        const auto count = s_vmUpdateUnavailableCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 2 || count % 6000 == 0)
            spdlog::info("SkyrimTogetherVR VM-update owner unavailable: call={} mode={} thread={}", count, GetVrVmUpdateModeName(), threadId);
    }
    else if (threadId != ownerThreadId)
    {
        const auto count = s_vmUpdateMismatchCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 2 || count % 6000 == 0)
            spdlog::warn("SkyrimTogetherVR VM-update worker call ignored: call={} mode={} thread={} owner={}", count, GetVrVmUpdateModeName(), threadId, ownerThreadId);
    }
    else
    {
        std::call_once(
            s_vmUpdateOwnerLogOnce,
            [threadId]()
            {
                spdlog::info("SkyrimTogetherVR VM-update owner reached: mode={} thread={}", GetVrVmUpdateModeName(), threadId);
            });

        const auto count = s_vmUpdateOwnerCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 2 || count % 6000 == 0)
            spdlog::info("SkyrimTogetherVR VM-update owner cadence: call={} mode={} thread={}", count, GetVrVmUpdateModeName(), threadId);

        if (s_vmUpdateMode == VrVmUpdateMode::Active && g_appInstance && SkyrimTogetherVR::TickBridge::ConsumeUpdatePermit())
            g_appInstance->Update();
    }

    TiltedPhoques::ThisCall(VrVmUpdate, apThis, aDelta);
}

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

static void InstallVrVmUpdateObserver()
{
    s_vmUpdateMode = ReadVrVmUpdateMode();
    spdlog::info("SkyrimTogetherVR VM-update runtime mode: {}", GetVrVmUpdateModeName());
    if (s_vmUpdateMode == VrVmUpdateMode::Off)
        return;

    POINTER_SKYRIMSE(TVrVmUpdate, cVrVmUpdate, 53926);
    VrVmUpdate = cVrVmUpdate.Get();
    spdlog::info("Installing opaque SkyrimTogetherVR VM-update observer: target={}", fmt::ptr(VrVmUpdate));
    TP_HOOK(&VrVmUpdate, HookVrVmUpdate);
}

void InstallVrMainLoopBringupHooks()
{
    InstallVrMainLoopObserver();
    InstallVrVmUpdateObserver();
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
