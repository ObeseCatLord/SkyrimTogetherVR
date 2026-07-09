
#include <Camera/TESCamera.h>
#include <NetImmerse/NiNode.h>
#include <TiltedOnlinePCH.h>

NiCamera* TESCamera::GetNiCamera() noexcept
{
    POINTER_SKYRIMSE(NiRTTI, NiCameraRTTI, 410506);
    auto* pCameraRoot = GetCameraRoot();
    if (!pCameraRoot)
        return nullptr;

    // usually the first child should be the camera
    for (const auto& child : pCameraRoot->GetChildrenData())
    {
        auto* pChild = child.object;
        if (pChild && pChild->GetRTTI() == NiCameraRTTI.Get())
            return reinterpret_cast<NiCamera*>(pChild);
    }

    return nullptr;
}
