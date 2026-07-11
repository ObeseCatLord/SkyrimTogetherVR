#pragma once

#include <cstdint>

// Utility to import the 3rd-party Script Extender made by Ian Patterson et al.
// Required for non-Creation Kit mods such as SkyUI
enum class ScriptExtenderLoadResult : uint8_t
{
    kModuleLoaded,
    kModuleMissing,
    kVersionUnsupported,
    kModuleLoadFailed,
};

ScriptExtenderLoadResult LoadScriptExender();

// Check whether the SKSEVR module was loaded. Operational initialization still
// requires runtime validation.
bool IsScriptExtenderLoaded();
