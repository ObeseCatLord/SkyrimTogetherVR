#pragma once

#include <string>

namespace stubs
{
// Returns true if the module name is found in the hard block list.
bool IsDllBlocked(std::wstring_view dllName);

// Returns true if the module is a Skyrim Souls RE related DLL.
bool IsSoulsRE(std::wstring_view dllName);

// Returns true if the module is the HIGGS VR plugin.
bool IsHiggs(std::wstring_view dllName);

// Returns true if the module is the PLANCK VR plugin.
bool IsPlanck(std::wstring_view dllName);

// Global flag indicating whether Skyrim Souls RE is active.
extern bool g_IsSoulsREActive;

// Global flag indicating whether HIGGS is active.
extern bool g_IsHiggsActive;

// Global flag indicating whether PLANCK is active.
extern bool g_IsPlanckActive;
} // namespace stubs
