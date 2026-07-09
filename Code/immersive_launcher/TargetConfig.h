// Copyright (C) 2021 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.
#pragma once

#include <cstdint>
#include <limits>
#include <BuildInfo.h>

#if !defined(TP_SKYRIM_VR) || TP_SKYRIM_VR != 1
#error "SkyrimTogetherVR launcher must be built with TP_SKYRIM_VR=1"
#endif

#define CLIENT_DLL 0

struct TargetConfig
{
    const wchar_t* dllClientName;
    const wchar_t* fullGameName;
    uint32_t steamAppId;
    uint32_t exeLoadSz;
    // Needs to be kept up to date.
    uint32_t exeDiskSz;
};

// clang-format off

static constexpr TargetConfig CurrentTarget{
    nullptr,
    L"Skyrim VR",
    611670, 0x40000000, 35530960};
#define TARGET_NAME L"SkyrimVR"
#define TARGET_NAME_A "SkyrimVR"
#define PRODUCT_NAME L"Skyrim Together VR"
#define SHORT_NAME L"Skyrim VR"

// clang-format on
