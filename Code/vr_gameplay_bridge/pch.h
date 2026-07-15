#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>

// alandtse CommonLib requires its REX wrappers to see the headers before the
// raw Windows API. The adapter still needs HANDLE and file-mapping functions.
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>

#include "vr_common/VRGameplayBridge.h"
