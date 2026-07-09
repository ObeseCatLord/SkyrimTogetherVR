
#include <base/threading/ThreadUtils.h>
#include <Games/Skyrim/VR/VRHookPolicy.h>

namespace
{
struct BSTaskletManager
{
    char pad0[0x30];
    void* threadHandles[6];
};

static void (*Construct_TaskletManager)(BSTaskletManager*);

static void Hook_Construct_TaskletManager(BSTaskletManager* apSelf)
{
    Construct_TaskletManager(apSelf);

    for (int i = 0; i < 6; i++)
    {
        if (!apSelf->threadHandles[i])
            continue;

        auto name = fmt::format("TaskletThread{}", i);
        Base::SetThreadName(apSelf->threadHandles[i], name.c_str());
    }
}
} // namespace

static TiltedPhoques::Initializer s_BSThreadInit(
    []()
    {
#if TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR, TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR_VR_RESOLVED)
        const VersionDbPtr<uint8_t> getTaskletManagerInstance(69554);

        // tasklet naming
        TiltedPhoques::SwapCall(getTaskletManagerInstance.Get() + 0x63, Construct_TaskletManager, &Hook_Construct_TaskletManager);
#endif
    });
