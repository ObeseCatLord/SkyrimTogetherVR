// Copyright (C) 2021 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.
#pragma once

#include <array>
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
    std::array<uint8_t, 32> exeSha256;
};

// clang-format off

static constexpr TargetConfig CurrentTarget{
    nullptr,
    L"Skyrim VR",
    611670,
    0x40000000,
    35530960,
    {0x69, 0x61, 0xef, 0xb4, 0xf4, 0x77, 0x5a, 0x30, 0x7b, 0x0f, 0xc9, 0xa3, 0xd6, 0x37, 0x54, 0x2c,
     0x1e, 0x09, 0x0b, 0xe2, 0x07, 0xd3, 0xb0, 0x94, 0x67, 0xea, 0xb2, 0x16, 0xb7, 0xf8, 0x79, 0x71}};
#define TARGET_NAME L"SkyrimVR"
#define TARGET_NAME_A "SkyrimVR"
#define PRODUCT_NAME L"Skyrim Together VR"
#define SHORT_NAME L"Skyrim VR"

// clang-format on
