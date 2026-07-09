#pragma once

#include <Games/Primitives.h>
#include <NetImmerse/NiPointer.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

struct NiNode;
struct NiObject;
struct NiCamera;
struct TESCameraState;

struct TESCamera
{
    using CommonLibCameraOffsets = Skyrim::RuntimeLayout::TESCameraCommonLibNgOffsets;
    using LocalCameraOffsets = Skyrim::RuntimeLayout::TESCameraLocalShimOffsets;

    virtual ~TESCamera(){};

    virtual void SetCameraRoot(NiPointer<NiNode> acRoot) { (void)acRoot; };
    virtual void Update(){};

    NiCamera* GetNiCamera() noexcept;

    [[nodiscard]] NiNode* GetCameraRoot() noexcept { return cameraRoot.object; }
    [[nodiscard]] const NiNode* GetCameraRoot() const noexcept { return cameraRoot.object; }
    [[nodiscard]] TESCameraState* GetCurrentStateData() noexcept { return currentState; }
    [[nodiscard]] const TESCameraState* GetCurrentStateData() const noexcept { return currentState; }
    [[nodiscard]] bool IsEnabledData() const noexcept { return enabled; }

    float rotationInputZ;             // 08 - CommonLib `rotationInput.x`
    float rotationInputX;             // 0C - CommonLib `rotationInput.y`
    NiPoint3 translationInput;        // 10
    float zoomInput;                  // 1C
    NiPointer<NiNode> cameraRoot;     // 20
    TESCameraState* currentState;     // 28 - pointer-sized smart pointer storage
    bool enabled;                     // 30
    std::uint8_t pad31;               // 31
    std::uint16_t pad32;              // 32
    std::uint32_t pad34;              // 34
};

static_assert(offsetof(TESCamera, rotationInputZ) == TESCamera::CommonLibCameraOffsets::RotationInput);
static_assert(offsetof(TESCamera, translationInput) == TESCamera::CommonLibCameraOffsets::TranslationInput);
static_assert(offsetof(TESCamera, zoomInput) == TESCamera::CommonLibCameraOffsets::ZoomInput);
static_assert(offsetof(TESCamera, cameraRoot) == TESCamera::CommonLibCameraOffsets::CameraRoot);
static_assert(offsetof(TESCamera, currentState) == TESCamera::CommonLibCameraOffsets::CurrentState);
static_assert(offsetof(TESCamera, enabled) == TESCamera::CommonLibCameraOffsets::Enabled);
static_assert(sizeof(TESCamera) == TESCamera::CommonLibCameraOffsets::Size);
