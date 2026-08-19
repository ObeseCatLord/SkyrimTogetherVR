#include "pch.h"

#include "WeatherNativeAccess.h"

#include <REL/Relocation.h>

#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace SkyrimTogetherVR::GameplayAdapter::WeatherNativeAccess
{
namespace
{
constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
std::atomic_bool g_targetValidationFailureLogged{};

[[nodiscard]] bool IsSpanWithin(
    const std::uintptr_t a_start,
    const std::uintptr_t a_size,
    const std::uintptr_t a_spanStart,
    const std::uintptr_t a_spanSize) noexcept
{
    if (a_spanSize == 0 || a_spanStart < a_start ||
        a_start > std::numeric_limits<std::uintptr_t>::max() - a_size)
        return false;

    const auto offset = a_spanStart - a_start;
    return offset <= a_size && a_spanSize <= a_size - offset;
}

void LogTargetValidationFailure(const char* a_reason) noexcept
{
    if (g_targetValidationFailureLogged.exchange(true, std::memory_order_relaxed))
        return;
    try
    {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: exact Sky ForceWeather/ReleaseWeatherOverride target validation failed ({})",
            a_reason ? a_reason : "unspecified failure");
    }
    catch (...)
    {
    }
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedTarget(
    const REL::Relocation<std::uintptr_t>& a_target,
    const std::uintptr_t a_rva,
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto& module = REL::Module::get();
    const auto moduleBase = module.base();
    if (moduleBase == 0 || moduleBase > std::numeric_limits<std::uintptr_t>::max() - a_rva ||
        a_target.offset() != a_rva || a_target.address() != moduleBase + a_rva)
        return false;

    const auto text = module.segment(REL::Segment::textx);
    if (!IsSpanWithin(
            static_cast<std::uintptr_t>(text.address()),
            static_cast<std::uintptr_t>(text.size()),
            a_target.address(),
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_target.address()), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
        memory.AllocationBase != reinterpret_cast<void*>(moduleBase) ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithin(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress),
            static_cast<std::uintptr_t>(memory.RegionSize),
            a_target.address(),
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    constexpr DWORD kReadableExecutableProtection =
        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kReadableExecutableProtection) != 0 &&
           std::memcmp(
               reinterpret_cast<const void*>(a_target.address()),
               a_prologue.data(),
               a_prologue.size()) == 0;
}
} // namespace

bool ValidateTargets() noexcept
{
    try {
        if (!HasPinnedTargetConfiguration() || !REL::Module::IsVR() ||
            REL::Module::get().version() != kExpectedSkyrimVrRuntime) {
            LogTargetValidationFailure("runtime or pinned configuration mismatch");
            return false;
        }

        const REL::Relocation<std::uintptr_t> forceWeather{REL::Offset(kForceWeatherVrRva)};
        const REL::Relocation<std::uintptr_t> releaseWeatherOverride{REL::Offset(kReleaseWeatherOverrideVrRva)};
        if (!IsVerifiedTarget(forceWeather, kForceWeatherVrRva, kForceWeatherVrPrologue) ||
            !IsVerifiedTarget(
                releaseWeatherOverride,
                kReleaseWeatherOverrideVrRva,
                kReleaseWeatherOverrideVrPrologue)) {
            LogTargetValidationFailure("RVA, module base, text, page, or prologue mismatch");
            return false;
        }

        return true;
    } catch (...) {
        LogTargetValidationFailure("target resolution threw");
        return false;
    }
}

bool ForceWeather(RE::Sky& a_sky, RE::TESWeather& a_weather) noexcept
{
    using ForceWeatherFn = void(__fastcall*)(RE::Sky*, RE::TESWeather*, bool);

    try {
        if (!ValidateTargets())
            return false;

        static REL::Relocation<ForceWeatherFn> forceWeather{REL::Offset(kForceWeatherVrRva)};
        forceWeather(std::addressof(a_sky), std::addressof(a_weather), kForceWeatherOverrideArgument);
        return true;
    } catch (...) {
        LogTargetValidationFailure("ForceWeather dispatch threw");
        return false;
    }
}

bool ReleaseWeatherOverride(RE::Sky& a_sky) noexcept
{
    using ReleaseWeatherOverrideFn = void(__fastcall*)(RE::Sky*);

    try {
        if (!ValidateTargets())
            return false;

        static REL::Relocation<ReleaseWeatherOverrideFn> releaseWeatherOverride{
            REL::Offset(kReleaseWeatherOverrideVrRva)};
        releaseWeatherOverride(std::addressof(a_sky));
        return true;
    } catch (...) {
        LogTargetValidationFailure("ReleaseWeatherOverride dispatch threw");
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::WeatherNativeAccess
