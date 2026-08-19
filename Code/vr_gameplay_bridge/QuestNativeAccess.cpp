#include "pch.h"

#include "QuestNativeAccess.h"
#include "VrNoThrow.h"

#include <RE/T/TESQuest.h>
#include <REL/Relocation.h>

#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
{
namespace
{
constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
std::atomic_bool g_invalidTargetLogged{};

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

[[nodiscard]] bool IsVerifiedSetStageTarget(const std::uintptr_t a_address) noexcept
{
    if (!HasPinnedTargetConfiguration() || !REL::Module::IsVR() ||
        REL::Module::get().version() != kExpectedSkyrimVrRuntime)
        return false;

    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithinSegment(text.address(), text.size(), a_address, kSetStageVrPrologue.size()))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithinSegment(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress), memory.RegionSize,
            a_address, kSetStageVrPrologue.size()))
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0 &&
           std::memcmp(
               reinterpret_cast<const void*>(a_address),
               kSetStageVrPrologue.data(),
               kSetStageVrPrologue.size()) == 0;
}
} // namespace

bool ValidateTarget() noexcept
{
    try
    {
        const REL::Relocation<std::uintptr_t> target{REL::Offset(kSetStageVrRva)};
        const bool valid = target.offset() == kSetStageVrRva && IsVerifiedSetStageTarget(target.address());
        if (!valid && !g_invalidTargetLogged.exchange(true, std::memory_order_relaxed))
            NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: exact TESQuest::SetStage target validation failed"); });
        return valid;
    }
    catch (...)
    {
        if (!g_invalidTargetLogged.exchange(true, std::memory_order_relaxed))
            NoThrow::BestEffort([] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: TESQuest::SetStage target resolution threw"); });
        return false;
    }
}

bool SetStage(RE::TESQuest& a_quest, const std::uint16_t a_stage) noexcept
{
    // Skyrim's x64 __fastcall ABI passes TESQuest* in RCX and the uint16_t
    // stage in RDX.
    // The bridge DLL is VR-only. Resolve the exact pinned RVA and reject any
    // executable whose runtime, text span, protection, or entry bytes differ.
    using SetStageFn = bool(__fastcall*)(RE::TESQuest*, std::uint16_t);
    return NoThrow::FailClosed<bool>([&] {
        static REL::Relocation<SetStageFn> setStage{REL::Offset(kSetStageVrRva)};
        return ValidateTarget() && setStage(std::addressof(a_quest), a_stage);
    }, false);
}

bool IsStageDone(const RE::TESQuest& a_quest, const std::uint16_t a_stage) noexcept
{
    if (!a_quest.executedStages)
        return false;

    for (const auto& stage : *a_quest.executedStages)
    {
        if (IsStageRecordDone(stage.data.index, stage.data.flags.underlying(), a_stage))
            return true;
    }
    return false;
}

void SetActive(RE::TESQuest& a_quest, const bool a_active) noexcept
{
    if (a_active)
        a_quest.data.flags.set(RE::QuestFlag::kActive);
    else
        a_quest.data.flags.reset(RE::QuestFlag::kActive);
    a_quest.AddChange(RE::TESQuest::ChangeFlags::kQuestFlags);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
