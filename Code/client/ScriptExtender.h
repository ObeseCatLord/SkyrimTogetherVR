#pragma once

#include <chrono>
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

// The immersive launcher must attempt SKSEVR bootstrap before entering the
// manually mapped Skyrim VR executable.
bool WasScriptExtenderLoadAttempted() noexcept;
ScriptExtenderLoadResult GetScriptExtenderLoadResult() noexcept;

// Check whether the SKSEVR module was loaded. Operational initialization still
// requires runtime validation.
bool IsScriptExtenderLoaded();

// Wait briefly for the Steam bootstrap shim to load the SKSEVR core at the
// mapped game's VCRUNTIME startup boundary.
bool WaitForScriptExtenderLoaded(std::chrono::milliseconds aTimeout);
