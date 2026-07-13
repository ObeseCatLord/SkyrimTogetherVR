#pragma once

// The legacy SKSEVR headers use the Windows min macro internally. Keep this
// exception local to their translation units; the rest of the project retains
// NOMINMAX from the top-level xmake configuration.
#ifdef NOMINMAX
#undef NOMINMAX
#endif

#include <common/IPrefix.h>
