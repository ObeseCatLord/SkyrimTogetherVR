#include "pch.h"

#include "RemoteSaveExclusion.h"

#include <RE/T/TESForm.h>
#include <RE/T/TESObjectREFR.h>
#include <REL/Relocation.h>

#include <atomic>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::RemoteSaveExclusion
{
namespace
{
using SetTemporary = void(RE::TESObjectREFR*);

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
std::atomic_bool g_invalidTargetLogged{};
std::atomic<std::uint64_t> g_markFailures{};

[[nodiscard]] bool IsSpanWithinSegment(
    const std::uintptr_t a_segmentAddress,
    const std::uintptr_t a_segmentSize,
    const std::uintptr_t a_spanAddress,
    const std::uintptr_t a_spanSize) noexcept
{
    if (a_spanSize == 0 || a_spanAddress < a_segmentAddress ||
        a_segmentAddress > std::numeric_limits<std::uintptr_t>::max() - a_segmentSize)
        return false;

    const auto offset = a_spanAddress - a_segmentAddress;
    return offset <= a_segmentSize && a_spanSize <= a_segmentSize - offset;
}

[[nodiscard]] bool IsVerifiedExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithinSegment(
            static_cast<std::uintptr_t>(text.address()),
            static_cast<std::uintptr_t>(text.size()),
            a_address,
            static_cast<std::uintptr_t>(kSetTemporaryVrPrologue.size())))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    if (!IsSpanWithinSegment(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress),
            static_cast<std::uintptr_t>(memory.RegionSize),
            a_address,
            static_cast<std::uintptr_t>(kSetTemporaryVrPrologue.size())))
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0 &&
           std::memcmp(reinterpret_cast<const void*>(a_address), kSetTemporaryVrPrologue.data(), kSetTemporaryVrPrologue.size()) == 0;
}

void RecordInvalidTargetOnce(const char* a_reason) noexcept
{
    if (g_invalidTargetLogged.exchange(true, std::memory_order_relaxed))
        return;

    try
    {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: TESObjectREFR temporary target validation failed: {}",
            a_reason ? a_reason : "unspecified failure");
    }
    catch (...)
    {
    }
}

void RecordMarkFailure(const char* a_reason) noexcept
{
    const auto count = g_markFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!ShouldLogValidationFailure(count))
        return;

    try
    {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: remote actor save exclusion failed (count={}): {}",
            count,
            a_reason ? a_reason : "unspecified failure");
    }
    catch (...)
    {
    }
}
} // namespace

bool ValidateTarget() noexcept
{
    try
    {
        if (!HasPinnedTargetConfiguration() || !REL::Module::IsVR() ||
            REL::Module::get().version() != kExpectedSkyrimVrRuntime)
        {
            RecordInvalidTargetOnce("requires exact Skyrim VR 1.4.15.0");
            return false;
        }

        const REL::Relocation<std::uintptr_t> target{REL::Offset(kSetTemporaryVrRva)};
        const auto moduleBase = REL::Module::get().base();
        const bool valid =
            moduleBase != 0 &&
            moduleBase <= std::numeric_limits<std::uintptr_t>::max() - kSetTemporaryVrRva &&
            target.offset() == kSetTemporaryVrRva &&
            target.address() == moduleBase + kSetTemporaryVrRva &&
            IsVerifiedExecutableTarget(target.address());
        if (!valid)
            RecordInvalidTargetOnce("RVA, module base, text span, page, or prologue mismatch");
        return valid;
    }
    catch (...)
    {
        RecordInvalidTargetOnce("resolution threw");
        return false;
    }
}

bool MarkTemporary(RE::TESObjectREFR& a_reference) noexcept
{
    try
    {
        if (!ValidateTarget())
            return false;

        static REL::Relocation<SetTemporary> setTemporary{REL::Offset(kSetTemporaryVrRva)};
        setTemporary(&a_reference);

        // GetFormFlags and RecordFlags::kTemporary are CommonLib's typed form
        // API. This verifies the engine mutation without a layout write.
        const auto formFlags = a_reference.GetFormFlags();
        if (HasTemporaryFormFlag(formFlags) &&
            (formFlags & static_cast<std::uint32_t>(RE::TESForm::RecordFlags::kTemporary)) != 0)
            return true;

        RecordMarkFailure("temporary form flag was absent after the engine operation");
        return false;
    }
    catch (...)
    {
        RecordMarkFailure("engine operation threw");
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::RemoteSaveExclusion
