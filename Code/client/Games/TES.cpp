#include <TiltedOnlinePCH.h>

#include <Games/TES.h>

TES* TES::Get() noexcept
{
#if TP_SKYRIM_VR
    POINTER_SKYRIMSE(TES*, tes, 516923);
#else
    POINTER_SKYRIMSE(TES*, tes, 400441);
#endif

    return *tes.Get();
}

ProcessLists* ProcessLists::Get() noexcept
{
#if TP_SKYRIM_VR
    POINTER_SKYRIMSE(ProcessLists*, processLists, 514167);
#else
    POINTER_SKYRIMSE(ProcessLists*, processLists, 400315);
#endif

    return *processLists.Get();
}
