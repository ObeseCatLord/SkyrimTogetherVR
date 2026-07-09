#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"

#include <mutex>
#include <VR/VRPlayerPose.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

#include <GameVM.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
#define TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY
#define TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY 0
#endif

struct Main;
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

#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
static std::once_flag s_vmUpdateLogOnce;
static std::once_flag s_mainLoopLogOnce;
static std::once_flag s_vmDestructorLogOnce;
static std::once_flag s_vrPoseLogOnce;
#endif

int TP_MAKE_THISCALL(HookVMUpdate, VMContext, float a2)
{
#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
    std::call_once(
        s_vmUpdateLogOnce,
        []()
        {
            spdlog::info("SkyrimTogetherVR VM update hook reached; ticking World::Update");
        });
    if (apThis->inactive == 0 && g_appInstance)
        g_appInstance->Update();
    std::call_once(
        s_vrPoseLogOnce,
        []()
        {
            VRPlayerPose pose{};
            if (SkyrimVR::CaptureLocalPlayerPose(pose))
            {
                spdlog::info(
                    "SkyrimTogetherVR pose nodes: hmd={:x}, left={:x}, right={:x}, spellOrigin={:x}, arrowOrigin={:x}, leftWeapon={:x}, rightWeapon={:x}",
                    pose.Hmd.Address(),
                    pose.LeftHand.Address(),
                    pose.RightHand.Address(),
                    pose.SpellOrigin.Address(),
                    pose.ArrowOrigin.Address(),
                    pose.LeftWeaponOffset.Address(),
                    pose.RightWeaponOffset.Address());
            }
            else
            {
                spdlog::info("SkyrimTogetherVR pose nodes are not available yet");
            }
        });
#else
    if (apThis->inactive == 0 && g_appInstance)
        g_appInstance->Update();
#endif

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
    std::call_once(
        s_mainLoopLogOnce,
        []()
        {
            spdlog::info("SkyrimTogetherVR main-loop hook reached");
        });
#else
    TP_EMPTY_HOOK_PLACEHOLDER
#endif

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
    std::call_once(
        s_vmDestructorLogOnce,
        []()
        {
            spdlog::info("SkyrimTogetherVR VM destructor hook reached");
        });
#else
    TP_EMPTY_HOOK_PLACEHOLDER
#endif

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

#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
    spdlog::info(
        "Installing SkyrimTogetherVR VM/main-loop bring-up hooks: vmUpdate={}, mainLoop={}, vmDestructor={}",
        fmt::ptr(VMUpdate),
        fmt::ptr(MainLoop),
        fmt::ptr(VMDestructor));
#endif

    TP_HOOK(&VMUpdate, HookVMUpdate);
    TP_HOOK(&MainLoop, HookMainLoop);
    TP_HOOK(&VMDestructor, HookVMDestructor);
}

void InstallVrMainLoopBringupHooks()
{
    InstallMainLoopHooks();
}

static TiltedPhoques::Initializer s_mainHooks([]() { InstallMainLoopHooks(); });
