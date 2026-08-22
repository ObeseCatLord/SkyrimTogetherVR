#include "DialogueProcessResponseHook.h"

#include "AvatarManager.h"
#include "BridgeBatchPolicy.h"
#include "BridgeEndpoint.h"
#include "VrNoThrow.h"

#include <MinHook.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHook
{
namespace
{
using namespace DialogueProcessResponseHookPolicy;

using DialogueItemStorage = RE::BSTSmartPointer<RE::DialogueItem>;
// This is the verified lowered ABI, not a C++ source-level by-value call.
// RDX is DialogueItemStorage* pointing at caller-allocated storage.
using ProcessResponse = void (*) (
    RE::AIProcess*, DialogueItemStorage*, RE::Actor*, RE::Actor*, bool);
static_assert(sizeof(DialogueItemStorage) == kByValueSmartPointerStorageBytes);

constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};

ProcessResponse g_originalProcessResponse{};
void* g_target{};
void* g_abiShim{};
VrHookDetachPolicy::HookState g_hookState{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_installed{};
std::atomic_bool g_replayDepthOverflowLogged{};
std::atomic<std::uint64_t> g_suppressionCount{};
thread_local std::uint32_t g_authoritativeReplayDepth{};

void LogNoThrow(auto&& a_action) noexcept
{
    try
    {
        a_action();
    }
    catch (...)
    {
    }
}

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kExpectedSkyrimVrRuntime;
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedExecutableTarget(
    const std::uintptr_t a_address,
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (!IsSpanWithin(
            static_cast<std::uintptr_t>(text.address()), static_cast<std::uintptr_t>(text.size()), a_address,
            kProcessResponseVrFunctionSpan) ||
        !IsSpanWithin(
            static_cast<std::uintptr_t>(text.address()), static_cast<std::uintptr_t>(text.size()), a_address,
            static_cast<std::uintptr_t>(a_prologue.size())))
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithin(
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress), static_cast<std::uintptr_t>(memory.RegionSize),
            a_address, kProcessResponseVrFunctionSpan))
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0 &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
}

void RecordSuppression() noexcept
{
    const auto count = g_suppressionCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!BridgeBatchPolicy::ShouldLogAggregate(count))
        return;
    LogNoThrow([&] {
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: suppressed managed remote AIProcess::ProcessResponse "
            "(aggregate total={})",
            count);
    });
}

// The machine shim calls this with the original RCX and R8 values. It never
// materializes the pointer-lowered RDX value as a C++ smart pointer.
extern "C" bool ShouldSuppressProcessResponse(
    RE::AIProcess* const a_process,
    RE::Actor* const a_talkingActor) noexcept
{
    try
    {
        static_cast<void>(a_process);
        const auto disposition = Decide(
            a_talkingActor != nullptr,
            a_talkingActor && AvatarManager::Get().IsManagedRemoteActor(a_talkingActor),
            IsAuthoritativeDialogueReplay());
        if (disposition != Disposition::SuppressAndDestroyDialogueItemStorage)
            return false;
        RecordSuppression();
        return true;
    }
    catch (...)
    {
        // A classification failure must preserve native behavior and its
        // ownership semantics instead of attempting a best-effort suppression.
        return false;
    }
}

// On suppression the original callee will not destroy its by-value argument.
// RDX points at the caller-allocated object, so destroy that object in place;
// never acquire, copy, or reinterpret the storage address as DialogueItem*.
extern "C" void DestroySuppressedDialogueItemStorage(
    RE::AIProcess* const,
    DialogueItemStorage* const a_storage) noexcept
{
    DestroyIndirectStorage(a_storage);
}

class StubWriter final
{
public:
    void Byte(const std::uint8_t a_value) noexcept { _bytes[_size++] = a_value; }

    void Bytes(const std::initializer_list<std::uint8_t> a_values) noexcept
    {
        for (const auto value : a_values)
            Byte(value);
    }

    void Address(const std::uintptr_t a_address) noexcept
    {
        for (std::size_t index = 0; index < sizeof(a_address); ++index)
            Byte(static_cast<std::uint8_t>(a_address >> (index * 8)));
    }

    [[nodiscard]] std::size_t Size() const noexcept { return _size; }
    [[nodiscard]] const std::uint8_t* Data() const noexcept { return _bytes.data(); }
    void PatchByte(const std::size_t a_offset, const std::uint8_t a_value) noexcept { _bytes[a_offset] = a_value; }

private:
    std::array<std::uint8_t, 128> _bytes{};
    std::size_t _size{};
};

[[nodiscard]] void* CreateAbiShim() noexcept
{
    // Entry: RCX=AIProcess*, RDX=pointer to caller-owned, pointer-lowered
    // DialogueItemStorage, R8=talking Actor*, R9=target Actor*, bool=[rsp+0x28].
    // The forwarding path restores RSP then tail-jumps, leaving every input
    // register and the fifth stack argument exactly as the caller supplied.
    StubWriter writer;
    writer.Bytes({0x48, 0x83, 0xEC, static_cast<std::uint8_t>(kShimShadowAndSaveBytes)}); // sub rsp, 68h
    writer.Bytes({0x48, 0x89, 0x4C, 0x24, 0x20}); // mov [rsp+20h], rcx
    writer.Bytes({0x48, 0x89, 0x54, 0x24, 0x28}); // mov [rsp+28h], rdx
    writer.Bytes({0x4C, 0x89, 0x44, 0x24, 0x30}); // mov [rsp+30h], r8
    writer.Bytes({0x4C, 0x89, 0x4C, 0x24, 0x38}); // mov [rsp+38h], r9
    writer.Bytes({0x48, 0x8B, 0x4C, 0x24, 0x20}); // mov rcx, [rsp+20h]
    writer.Bytes({0x48, 0x8B, 0x54, 0x24, 0x30}); // mov rdx, [rsp+30h]
    writer.Bytes({0x48, 0xB8});
    writer.Address(reinterpret_cast<std::uintptr_t>(&ShouldSuppressProcessResponse));
    writer.Bytes({0xFF, 0xD0, 0x84, 0xC0, 0x75, 0x00}); // call rax; test al, al; jnz suppress
    const auto suppressBranchOffset = writer.Size() - 1;
    writer.Bytes({0x48, 0x8B, 0x4C, 0x24, 0x20}); // mov rcx, [rsp+20h]
    writer.Bytes({0x48, 0x8B, 0x54, 0x24, 0x28}); // mov rdx, [rsp+28h]
    writer.Bytes({0x4C, 0x8B, 0x44, 0x24, 0x30}); // mov r8, [rsp+30h]
    writer.Bytes({0x4C, 0x8B, 0x4C, 0x24, 0x38}); // mov r9, [rsp+38h]
    writer.Bytes({0x48, 0x83, 0xC4, static_cast<std::uint8_t>(kShimShadowAndSaveBytes)}); // add rsp, 68h
    writer.Bytes({0x48, 0xA1}); // mov rax, moffs64
    writer.Address(reinterpret_cast<std::uintptr_t>(&g_originalProcessResponse));
    writer.Bytes({0xFF, 0xE0}); // jmp rax
    const auto suppressOffset = writer.Size();
    writer.Bytes({0x48, 0x8B, 0x4C, 0x24, 0x20}); // suppress: mov rcx, [rsp+20h] (destructor ABI arg 1)
    writer.Bytes({0x48, 0x8B, 0x54, 0x24, 0x28}); // mov rdx, [rsp+28h]
    writer.Bytes({0x48, 0xB8});
    writer.Address(reinterpret_cast<std::uintptr_t>(&DestroySuppressedDialogueItemStorage));
    writer.Bytes({0xFF, 0xD0}); // call rax
    writer.Bytes({0x48, 0x83, 0xC4, static_cast<std::uint8_t>(kShimShadowAndSaveBytes), 0xC3}); // add rsp, 68h; ret
    const auto branchDistance = suppressOffset - (suppressBranchOffset + 1);
    if (branchDistance > static_cast<std::size_t>(std::numeric_limits<std::int8_t>::max()))
        return nullptr;
    writer.PatchByte(suppressBranchOffset, static_cast<std::uint8_t>(branchDistance));

    auto* const memory = VirtualAlloc(nullptr, writer.Size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory)
        return nullptr;
    std::memcpy(memory, writer.Data(), writer.Size());
    DWORD oldProtection{};
    if (!VirtualProtect(memory, writer.Size(), PAGE_EXECUTE_READ, &oldProtection) ||
        !FlushInstructionCache(GetCurrentProcess(), memory, writer.Size()))
    {
        static_cast<void>(VirtualFree(memory, 0, MEM_RELEASE));
        return nullptr;
    }
    return memory;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult DisableHook(void*) noexcept
{
    const auto status = MH_DisableHook(g_target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_DISABLED)
        return VrHookDetachPolicy::OperationResult::AlreadyDisabled;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogNoThrow([&] {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse hook disable failed ({})",
            static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] VrHookDetachPolicy::OperationResult RemoveHook(void*) noexcept
{
    const auto status = MH_RemoveHook(g_target);
    if (status == MH_OK)
        return VrHookDetachPolicy::OperationResult::Complete;
    if (status == MH_ERROR_NOT_CREATED)
        return VrHookDetachPolicy::OperationResult::NotCreated;
    LogNoThrow([&] {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse hook removal failed ({})",
            static_cast<int>(status));
    });
    return VrHookDetachPolicy::OperationResult::Failed;
}

[[nodiscard]] bool Detach() noexcept
{
    return VrHookDetachPolicy::Detach(g_hookState, {DisableHook, RemoveHook, nullptr});
}

void ClearDetachedState() noexcept
{
    if (g_abiShim)
        static_cast<void>(VirtualFree(g_abiShim, 0, MEM_RELEASE));
    g_abiShim = nullptr;
    g_target = nullptr;
    g_originalProcessResponse = nullptr;
    g_hookState = {};
    g_suppressionCount.store(0, std::memory_order_relaxed);
    g_replayDepthOverflowLogged.store(false, std::memory_order_relaxed);
    g_installed.store(false, std::memory_order_release);
}

[[nodiscard]] bool RollbackInstall() noexcept
{
    g_installed.store(false, std::memory_order_release);
    if (Detach())
    {
        ClearDetachedState();
        g_installAttempted.store(false, std::memory_order_release);
        return true;
    }

    BridgeEndpoint::Get().Fault("AIProcess::ProcessResponse hook rollback could not prove detachment");
    LogNoThrow([] {
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse rollback could not prove detachment; retaining callable ABI shim and trampoline");
    });
    return false;
}
} // namespace

ScopedAuthoritativeDialogueReplay::ScopedAuthoritativeDialogueReplay() noexcept
{
    if (g_authoritativeReplayDepth == std::numeric_limits<std::uint32_t>::max())
    {
        if (!g_replayDepthOverflowLogged.exchange(true, std::memory_order_relaxed))
        {
            LogNoThrow([] {
                SKSE::log::critical(
                    "SkyrimTogetherVRGameplayBridge: authoritative dialogue replay depth overflowed; preserving suppression");
            });
        }
        return;
    }
    ++g_authoritativeReplayDepth;
    _entered = true;
}

ScopedAuthoritativeDialogueReplay::~ScopedAuthoritativeDialogueReplay() noexcept
{
    if (_entered && g_authoritativeReplayDepth != 0)
        --g_authoritativeReplayDepth;
}

bool IsAuthoritativeDialogueReplay() noexcept
{
    return g_authoritativeReplayDepth != 0;
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;

    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    try
    {
        REL::Relocation<ProcessResponse> target{REL::Offset(kProcessResponseVrRva)};
        const auto moduleBase = REL::Module::get().base();
        if (!IsExpectedVrRuntime() || !IsPinnedTarget(kProcessResponseVrRva) || moduleBase == 0 ||
            moduleBase > std::numeric_limits<std::uintptr_t>::max() - kProcessResponseVrRva ||
            target.offset() != kProcessResponseVrRva || target.address() != moduleBase + kProcessResponseVrRva ||
            !IsVerifiedExecutableTarget(target.address(), kProcessResponseVrPrologue))
        {
            LogNoThrow([] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: exact AIProcess::ProcessResponse target validation failed "
                    "(required VR RVA=0x66DD50, span=0x27A)");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        const auto initialize = MH_Initialize();
        if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: MinHook initialization failed for AIProcess::ProcessResponse ({})",
                    static_cast<int>(initialize));
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_abiShim = CreateAbiShim();
        if (!g_abiShim)
        {
            LogNoThrow([] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: could not allocate AIProcess::ProcessResponse ABI shim");
            });
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }

        g_target = reinterpret_cast<void*>(target.address());
        void* trampoline{};
        const auto create = MH_CreateHook(g_target, g_abiShim, &trampoline);
        if (create != MH_OK)
        {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse hook creation failed ({})",
                    static_cast<int>(create));
            });
            ClearDetachedState();
            g_installAttempted.store(false, std::memory_order_release);
            return false;
        }
        g_hookState.Created = true;
        g_originalProcessResponse = reinterpret_cast<ProcessResponse>(trampoline);
        if (!g_originalProcessResponse || trampoline == g_target)
        {
            LogNoThrow([] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse hook returned an invalid trampoline");
            });
            static_cast<void>(RollbackInstall());
            return false;
        }

        // Preserve state before enabling because a failing MinHook call does
        // not prove its target bytes were untouched.
        g_hookState.Enabled = true;
        const auto enable = MH_EnableHook(g_target);
        if (enable != MH_OK)
        {
            LogNoThrow([&] {
                SKSE::log::error(
                    "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse hook enable failed ({})",
                    static_cast<int>(enable));
            });
            static_cast<void>(RollbackInstall());
            return false;
        }

        g_installed.store(true, std::memory_order_release);
        LogNoThrow([] {
            SKSE::log::info(
                "SkyrimTogetherVRGameplayBridge: installed exact AIProcess::ProcessResponse suppression at VR RVA 0x66DD50");
        });
        return true;
    }
    catch (...)
    {
        BridgeEndpoint::Get().Fault("AIProcess::ProcessResponse hook installation threw");
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse installation rejected an exception");
        });
        static_cast<void>(RollbackInstall());
        return false;
    }
}

bool Uninstall() noexcept
{
    if (!Detach())
    {
        g_installed.store(false, std::memory_order_release);
        BridgeEndpoint::Get().Fault("AIProcess::ProcessResponse hook uninstall could not prove detachment");
        LogNoThrow([] {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: AIProcess::ProcessResponse uninstall incomplete; retaining callable ABI shim and trampoline");
        });
        return false;
    }
    ClearDetachedState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHook
