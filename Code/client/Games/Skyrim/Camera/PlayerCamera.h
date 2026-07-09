#pragma once

#include <Camera/TESCamera.h>
#include <RuntimeLayout.h>
#include <cstdint>

struct PlayerCamera : public TESCamera
{
    using CommonLibPlayerCameraOffsets = Skyrim::RuntimeLayout::PlayerCameraCommonLibNgOffsets;
    using LocalPlayerCameraOffsets = Skyrim::RuntimeLayout::PlayerCameraLocalShimOffsets;

    static PlayerCamera* Get() noexcept;

    bool IsFirstPerson() noexcept;

    bool WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float zeroTolerance = 1e-5f);

    void ForceFirstPerson() noexcept;
    void ForceThirdPerson() noexcept;

private:
    // CommonLibSSE-NG models PlayerCamera's tail, but the staged VR port does
    // not currently read it. Keep it opaque so callers cannot accidentally use
    // old SE-era shadow fields.
    std::uint8_t playerCameraRuntimeData[CommonLibPlayerCameraOffsets::Size - CommonLibPlayerCameraOffsets::BaseSize];
};

static_assert(sizeof(TESCamera) == PlayerCamera::CommonLibPlayerCameraOffsets::BaseSize);
static_assert(sizeof(PlayerCamera) == PlayerCamera::CommonLibPlayerCameraOffsets::Size);
