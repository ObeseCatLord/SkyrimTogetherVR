#include "VerifiedVrActorAction.h"

#include <atomic>
#include <cstring>

namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrActorAction
{
namespace
{
using ActionStep = std::uint8_t(__fastcall*)(void*, RE::TESActionData*) noexcept;
using ApplyAnimationVariables = void*(__fastcall*)(void*, RE::TESActionData*, std::uint8_t) noexcept;
using TesActionDataCtor = RE::TESActionData* (__fastcall*)(RE::TESActionData*) noexcept;

constexpr std::uintptr_t kPerformActionRva = 0x0643F20;
constexpr std::uintptr_t kPerformComplexActionRva = 0x0644160;
constexpr std::uintptr_t kApplyAnimationVariablesRva = 0x0646160;
constexpr std::uintptr_t kTesActionDataCtorRva = 0x01FE070;
constexpr std::uintptr_t kTesActionDataVtableRva = 0x15BF5D8;
constexpr std::uintptr_t kActorMediatorVtableRva = 0x16D1CC8;
constexpr std::uintptr_t kActorMediatorSingletonRva = 0x2FEBCB8;
constexpr std::uintptr_t kAnimationVariableContextRva = 0x2FEBCB0;
constexpr std::uintptr_t kPerformComplexActionApplySequenceOffset = 0x51;

constexpr std::array<std::uint8_t, 16> kPerformActionPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0xDA,
};
constexpr std::array<std::uint8_t, 16> kPerformComplexActionPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
};
constexpr std::array<std::uint8_t, 16> kApplyAnimationVariablesPrologue{
    0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
    0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
};
constexpr std::array<std::uint8_t, 16> kTesActionDataCtorPrologue{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57,
    0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24,
};
constexpr std::array<std::uint8_t, 19> kPerformComplexActionApplySequence{
    0x48, 0x8B, 0x0D, 0xF8, 0x7A, 0x9A, 0x02,
    0x44, 0x0F, 0xB6, 0xC7,
    0x48, 0x8B, 0xD3,
    0xE8, 0x9C, 0x1F, 0x00, 0x00,
};

std::atomic<PerformAction> g_performAction{};
std::atomic<ActionStep> g_performComplexAction{};
std::atomic<ApplyAnimationVariables> g_applyAnimationVariables{};
std::atomic<TesActionDataCtor> g_tesActionDataCtor{};
std::atomic<bool> g_ready{};

[[nodiscard]] bool IsReadableMemory(const void* a_candidate, const std::size_t a_size) noexcept
{
    if (!a_candidate || a_size == 0)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(a_candidate, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const auto candidate = reinterpret_cast<std::uintptr_t>(a_candidate);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return candidate <= regionEnd && regionEnd - candidate >= a_size;
}

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (a_address < text.address() || a_address >= text.address() + text.size())
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & executable) != 0;
}

template <std::size_t N>
[[nodiscard]] bool HasExpectedBytes(const std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_bytes) noexcept
{
    return IsExecutableTarget(a_address) &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_bytes.data(), a_bytes.size()) == 0;
}

[[nodiscard]] bool HasExpectedComplexActionApplyCall(
    const std::uintptr_t a_base,
    const std::uintptr_t a_complexActionAddress,
    const std::uintptr_t a_applyVariablesAddress) noexcept
{
    const auto sequenceAddress = a_complexActionAddress + kPerformComplexActionApplySequenceOffset;
    if (!HasExpectedBytes(sequenceAddress, kPerformComplexActionApplySequence))
        return false;

    std::int32_t contextDisplacement{};
    std::int32_t callDisplacement{};
    std::memcpy(&contextDisplacement, reinterpret_cast<const void*>(sequenceAddress + 3), sizeof(contextDisplacement));
    std::memcpy(&callDisplacement, reinterpret_cast<const void*>(sequenceAddress + 15), sizeof(callDisplacement));
    const auto contextAddress = static_cast<std::uintptr_t>(
        static_cast<std::intptr_t>(sequenceAddress + 7) + contextDisplacement);
    const auto callTarget = static_cast<std::uintptr_t>(
        static_cast<std::intptr_t>(sequenceAddress + kPerformComplexActionApplySequence.size()) + callDisplacement);
    return contextAddress == a_base + kAnimationVariableContextRva && callTarget == a_applyVariablesAddress;
}

[[nodiscard]] bool HasExpectedVtables() noexcept
{
    const REL::Relocation<std::uintptr_t> actionDataVtable{RE::VTABLE_TESActionData[0]};
    const REL::Relocation<std::uintptr_t> actorMediatorVtable{RE::VTABLE_ActorMediator[0]};
    if (actionDataVtable.offset() != kTesActionDataVtableRva ||
        actorMediatorVtable.offset() != kActorMediatorVtableRva ||
        !IsReadableMemory(reinterpret_cast<const void*>(actionDataVtable.address()), sizeof(std::uintptr_t)) ||
        !IsReadableMemory(reinterpret_cast<const void*>(actorMediatorVtable.address()), sizeof(std::uintptr_t)))
        return false;

    const auto actionDataDestructor = *reinterpret_cast<const std::uintptr_t*>(actionDataVtable.address());
    const auto actorMediatorDestructor = *reinterpret_cast<const std::uintptr_t*>(actorMediatorVtable.address());
    return IsExecutableTarget(actionDataDestructor) && IsExecutableTarget(actorMediatorDestructor);
}

[[nodiscard]] RE::TESActionData* AsActionData(std::array<std::byte, sizeof(RE::TESActionData)>& a_storage) noexcept
{
    return reinterpret_cast<RE::TESActionData*>(a_storage.data());
}

void ReleaseActionData(RE::TESActionData& a_data) noexcept
{
    // Mirrors Skyrim Together's stack TESActionData destructor. The VR complex
    // path can release output fields before returning; these operations are
    // idempotent for empty BSFixedStrings and ref handles.
    a_data.animEvent = nullptr;
    a_data.targetAnimEvent = nullptr;
    a_data.source.reset();
    a_data.target.reset();
    a_data.action = nullptr;
    a_data.sequence = nullptr;
    a_data.animObjIdle = nullptr;
}
} // namespace

bool Initialize() noexcept
{
    if (g_ready.load(std::memory_order_acquire))
        return true;

    try {
        const auto base = REL::Module::get().base();
        const auto performActionAddress = base + kPerformActionRva;
        const auto performComplexActionAddress = base + kPerformComplexActionRva;
        const auto applyAnimationVariablesAddress = base + kApplyAnimationVariablesRva;
        const auto ctorAddress = base + kTesActionDataCtorRva;
        const auto singletonStorageAddress = base + kActorMediatorSingletonRva;
        const REL::Relocation<TesActionDataCtor> ctor{REL::RelocationID(15916, 41558, 15916)};

        if (!HasExpectedBytes(performActionAddress, kPerformActionPrologue) ||
            !HasExpectedBytes(performComplexActionAddress, kPerformComplexActionPrologue) ||
            !HasExpectedBytes(applyAnimationVariablesAddress, kApplyAnimationVariablesPrologue) ||
            !HasExpectedComplexActionApplyCall(base, performComplexActionAddress, applyAnimationVariablesAddress) ||
            ctor.address() != ctorAddress || !HasExpectedBytes(ctorAddress, kTesActionDataCtorPrologue) ||
            !IsReadableMemory(reinterpret_cast<const void*>(singletonStorageAddress), sizeof(void*)) ||
            !IsReadableMemory(reinterpret_cast<const void*>(base + kAnimationVariableContextRva), sizeof(void*)) ||
            !HasExpectedVtables())
            return false;

        g_performAction.store(reinterpret_cast<PerformAction>(performActionAddress), std::memory_order_release);
        g_performComplexAction.store(reinterpret_cast<ActionStep>(performComplexActionAddress), std::memory_order_release);
        g_applyAnimationVariables.store(
            reinterpret_cast<ApplyAnimationVariables>(applyAnimationVariablesAddress), std::memory_order_release);
        g_tesActionDataCtor.store(ctor.get(), std::memory_order_release);
        g_ready.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        Reset();
        return false;
    }
}

void Reset() noexcept
{
    g_ready.store(false, std::memory_order_release);
    g_tesActionDataCtor.store(nullptr, std::memory_order_release);
    g_applyAnimationVariables.store(nullptr, std::memory_order_release);
    g_performComplexAction.store(nullptr, std::memory_order_release);
    g_performAction.store(nullptr, std::memory_order_release);
}

bool IsReady() noexcept
{
    return g_ready.load(std::memory_order_acquire) &&
           g_performAction.load(std::memory_order_acquire) != nullptr &&
           g_performComplexAction.load(std::memory_order_acquire) != nullptr &&
           g_applyAnimationVariables.load(std::memory_order_acquire) != nullptr &&
           g_tesActionDataCtor.load(std::memory_order_acquire) != nullptr;
}

PerformAction GetPerformAction() noexcept
{
    return IsReady() ? g_performAction.load(std::memory_order_acquire) : nullptr;
}

bool IsActorMediator(const void* a_candidate) noexcept
{
    if (!IsReadableMemory(a_candidate, sizeof(std::uintptr_t)))
        return false;

    const REL::Relocation<std::uintptr_t> expectedVtable{RE::VTABLE_ActorMediator[0]};
    return expectedVtable.offset() == kActorMediatorVtableRva &&
           *static_cast<const std::uintptr_t*>(a_candidate) == expectedVtable.address();
}

bool IsTesActionData(const RE::TESActionData* a_data) noexcept
{
    if (!IsReadableMemory(a_data, sizeof(RE::TESActionData)))
        return false;

    const REL::Relocation<std::uintptr_t> expectedVtable{RE::VTABLE_TESActionData[0]};
    if (expectedVtable.offset() != kTesActionDataVtableRva ||
        *reinterpret_cast<const std::uintptr_t*>(a_data) != expectedVtable.address())
        return false;

    auto* source = a_data->source.get();
    const auto action = a_data->action;
    if (!source || !action || !IsReadableMemory(source, sizeof(std::uintptr_t)) ||
        !IsReadableMemory(action, sizeof(std::uintptr_t)) ||
        RE::TESForm::LookupByID<RE::Actor>(source->GetFormID()) != source ||
        RE::TESForm::LookupByID<RE::BGSAction>(action->GetFormID()) != action)
        return false;

    auto* target = a_data->target.get();
    if (target && (!IsReadableMemory(target, sizeof(std::uintptr_t)) ||
                   RE::TESForm::LookupByID<RE::TESObjectREFR>(target->GetFormID()) != target))
        return false;

    const auto idle = a_data->animObjIdle;
    return !idle || (IsReadableMemory(idle, sizeof(std::uintptr_t)) &&
                     RE::TESForm::LookupByID<RE::TESIdleForm>(idle->GetFormID()) == idle);
}

void* GetActorMediator() noexcept
{
    if (!IsReady())
        return nullptr;

    const auto storage = reinterpret_cast<void* const*>(REL::Module::get().base() + kActorMediatorSingletonRva);
    if (!IsReadableMemory(storage, sizeof(*storage)))
        return nullptr;

    auto* mediator = *storage;
    return IsActorMediator(mediator) ? mediator : nullptr;
}

std::uint8_t ForceAction(void* a_mediator, RE::TESActionData* a_data) noexcept
{
    const auto step = g_performComplexAction.load(std::memory_order_acquire);
    const auto applyVariables = g_applyAnimationVariables.load(std::memory_order_acquire);
    const auto contextStorage = reinterpret_cast<void* const*>(REL::Module::get().base() + kAnimationVariableContextRva);
    if (!step || !applyVariables || !IsReadableMemory(contextStorage, sizeof(*contextStorage)) || !*contextStorage ||
        !IsReadableMemory(*contextStorage, sizeof(void*)) || !IsActorMediator(a_mediator) || !IsTesActionData(a_data))
        return 0;

    // 0x140644160 is the target used by VR's normal PerformAction complex
    // branch. Its unconditional tail call at 0x1406441B1 reaches the verified
    // variable application routine, matching desktop ForceAction's sequence.
    return step(a_mediator, a_data);
}

ReplayActionData::~ReplayActionData() noexcept
{
    if (_constructed)
        ReleaseActionData(*AsActionData(_storage));
}

RE::TESActionData* ReplayActionData::Construct() noexcept
{
    if (_constructed || !IsReady())
        return nullptr;

    auto* data = AsActionData(_storage);
    const auto ctor = g_tesActionDataCtor.load(std::memory_order_acquire);
    const REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_TESActionData[0]};
    if (!ctor || vtable.offset() != kTesActionDataVtableRva)
        return nullptr;

    std::memset(data, 0, sizeof(*data));
    if (ctor(data) != data)
        return nullptr;

    *reinterpret_cast<std::uintptr_t*>(data) = vtable.address();
    _constructed = true;
    return data;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::VerifiedVrActorAction
