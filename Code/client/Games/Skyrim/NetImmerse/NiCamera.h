#pragma once

#include <NetImmerse/NiAVObject.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

struct NiNode;

template <typename T> struct NiRect
{
    T left;
    T right;
    T top;
    T bottom;
};

static_assert(sizeof(NiRect<float>) == 0x10);

struct NiFrustum
{
    float fLeft;   // 00
    float fRight;  // 04
    float fTop;    // 08
    float fBottom; // 0C
    float fNear;   // 10
    float fFar;    // 14
    bool bOrtho;   // 18
    std::uint8_t pad19[3];
};

static_assert(sizeof(NiFrustum) == 0x1C);

struct NiCamera : public NiAVObject
{
    using CommonLibCameraOffsets = Skyrim::RuntimeLayout::NiCameraCommonLibNgOffsets;
    using LocalCameraOffsets = Skyrim::RuntimeLayout::NiCameraLocalShimOffsets;

    struct RuntimeData2
    {
        NiFrustum viewFrustum;      // 00
        float minNearPlaneDist;     // 1C
        float maxFarNearRatio;      // 20
        NiRect<float> port;         // 24
        float lodAdjust;            // 34
    };

    virtual ~NiCamera() = default;

    bool WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float zeroTolerance = 1e-5f);

    static bool WorldPtToScreenPt3(const float acMatrix[4][4], const NiRect<float>& acPort, const NiPoint3& acPoint, float& aXOut, float& aYOut, float& aZOut, float aZeroTolerance = 1e-5f);

    [[nodiscard]] const float (*GetWorldToCameraMatrixData() const noexcept)[4] { return worldToCam; }
    [[nodiscard]] const RuntimeData2& GetRuntimeData2() const noexcept { return runtimeData2; }
    [[nodiscard]] const NiRect<float>& GetPortData() const noexcept { return runtimeData2.port; }

    float worldToCam[4][4]; // SE 110, VR 138
#if TP_SKYRIM_VR
    NiFrustum* viewFrustumPtr;        // 178
    std::uint8_t unknownArrays[0x48]; // 180 - three CommonLib BSTArray<void*> slots
    std::uint32_t unknown1C8;         // 1C8
#endif
    RuntimeData2 runtimeData2; // SE 150, VR 1CC
};

static_assert(sizeof(NiCamera::RuntimeData2) == 0x38);
static_assert(offsetof(NiCamera::RuntimeData2, viewFrustum) == NiCamera::CommonLibCameraOffsets::ViewFrustum);
static_assert(offsetof(NiCamera::RuntimeData2, minNearPlaneDist) == NiCamera::CommonLibCameraOffsets::MinNearPlaneDist);
static_assert(offsetof(NiCamera::RuntimeData2, maxFarNearRatio) == NiCamera::CommonLibCameraOffsets::MaxFarNearRatio);
static_assert(offsetof(NiCamera::RuntimeData2, port) == NiCamera::CommonLibCameraOffsets::Port);
static_assert(offsetof(NiCamera::RuntimeData2, lodAdjust) == NiCamera::CommonLibCameraOffsets::LodAdjust);

#if TP_SKYRIM_VR
static_assert(offsetof(NiCamera, worldToCam) == NiCamera::CommonLibCameraOffsets::VRRuntimeData + NiCamera::CommonLibCameraOffsets::WorldToCam);
static_assert(offsetof(NiCamera, viewFrustumPtr) == NiCamera::CommonLibCameraOffsets::VRRuntimeData + NiCamera::CommonLibCameraOffsets::VRViewFrustumPtr);
static_assert(offsetof(NiCamera, unknownArrays) == NiCamera::CommonLibCameraOffsets::VRRuntimeData + NiCamera::CommonLibCameraOffsets::VRUnknownArrays);
static_assert(offsetof(NiCamera, unknown1C8) == NiCamera::CommonLibCameraOffsets::VRRuntimeData + NiCamera::CommonLibCameraOffsets::VRUnknown1C8);
static_assert(offsetof(NiCamera, runtimeData2) == NiCamera::CommonLibCameraOffsets::VRRuntimeData2);
static_assert(sizeof(NiCamera) == NiCamera::CommonLibCameraOffsets::VRSize);
#else
static_assert(offsetof(NiCamera, worldToCam) == NiCamera::CommonLibCameraOffsets::SERuntimeData + NiCamera::CommonLibCameraOffsets::WorldToCam);
static_assert(offsetof(NiCamera, runtimeData2) == NiCamera::CommonLibCameraOffsets::SERuntimeData2);
static_assert(sizeof(NiCamera) == NiCamera::CommonLibCameraOffsets::SESize);
#endif
