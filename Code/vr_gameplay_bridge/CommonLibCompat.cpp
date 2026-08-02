#include "pch.h"

#include <RE/E/ExtraDataList.h>

#if !defined(EXCLUSIVE_SKYRIM_VR)
#    error "CommonLibCompat.cpp requires EXCLUSIVE_SKYRIM_VR"
#endif

static_assert(sizeof(RE::ExtraDataList) == 0x18);

namespace RE
{
ExtraDataList::ExtraDataList() = default;
ExtraDataList::~ExtraDataList() = default;
}
