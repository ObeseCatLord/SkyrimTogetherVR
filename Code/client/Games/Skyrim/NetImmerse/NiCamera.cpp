#include <NetImmerse/NiCamera.h>
#include <TiltedOnlinePCH.h>

using TWorldPtToScreenPt3 = bool(const float (*)[4], const NiRect<float>&, const NiPoint3&, float&, float&, float&, float);
static TWorldPtToScreenPt3* s_WorldPtToScreenPt3;

bool NiCamera::WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float tolerance)
{
    return WorldPtToScreenPt3(GetWorldToCameraMatrixData(), GetPortData(), in, out.x, out.y, out.z, tolerance);
}

bool NiCamera::WorldPtToScreenPt3(const float acMatrix[4][4], const NiRect<float>& acPort, const NiPoint3& acPoint, float& aXOut, float& aYOut, float& aZOut, float aZeroTolerance)
{
    if (!s_WorldPtToScreenPt3 || !acMatrix)
        return false;

    return s_WorldPtToScreenPt3(acMatrix, acPort, acPoint, aXOut, aYOut, aZOut, aZeroTolerance);
}

static TiltedPhoques::Initializer s_Init(
    []()
    {
        POINTER_SKYRIMSE(TWorldPtToScreenPt3, s_w2s, 70640);
        s_WorldPtToScreenPt3 = s_w2s.Get();
    });
