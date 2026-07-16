#include "DialogueHooks.h"

#include "AvatarManager.h"
#include "LocalGameplayCapture.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::DialogueHooks
{
namespace
{
constexpr std::uint64_t kSpeakSoundVrRva = 0x05F0E20;
constexpr std::array<std::uint8_t, 16> kSpeakSoundVrPrologue{
    0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20, 0x48,
    0x89, 0x50, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54,
};

// VR callers consume the return value from XMM0 as a duration. The fourteen
// argument layout is independently visible at each direct callsite.
using SpeakSound = float (*)(RE::Actor*, const char*, std::uint32_t*, std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, bool,
                             std::uint64_t, bool, bool, bool);

SpeakSound g_targetSpeakSound{};
SpeakSound g_originalSpeakSound{};
void* g_hookTarget{};
std::atomic_bool g_installAttempted{};
thread_local std::uint32_t g_remoteReplayDepth{};

class ScopedRemoteReplay final
{
public:
    ScopedRemoteReplay() noexcept { ++g_remoteReplayDepth; }
    ~ScopedRemoteReplay() noexcept
    {
        if (g_remoteReplayDepth != 0)
            --g_remoteReplayDepth;
    }
};

float HookSpeakSound(
    RE::Actor* a_actor,
    const char* a_resourcePath,
    std::uint32_t* a_handle,
    const std::uint32_t a_arg4,
    const std::uint32_t a_priority,
    const std::uint32_t a_arg6,
    const std::uint64_t a_arg7,
    const std::uint64_t a_arg8,
    const std::uint64_t a_arg9,
    const bool a_arg10,
    const std::uint64_t a_arg11,
    const bool a_arg12,
    const bool a_arg13,
    const bool a_arg14)
{
    const auto result = g_originalSpeakSound ?
        g_originalSpeakSound(
            a_actor, a_resourcePath, a_handle, a_arg4, a_priority, a_arg6, a_arg7, a_arg8,
            a_arg9, a_arg10, a_arg11, a_arg12, a_arg13, a_arg14) :
        0.0F;
    if (std::isfinite(result) && result > 0.0F && g_remoteReplayDepth == 0 && a_actor && a_resourcePath &&
        !AvatarManager::Get().IsManagedRemoteActor(a_actor))
        LocalGameplayCapture::CaptureDialogueVoice(a_actor->GetFormID(), a_resourcePath);
    return result;
}
} // namespace

bool Install() noexcept
{
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return g_originalSpeakSound != nullptr;

    REL::Relocation<SpeakSound> target{REL::Offset(kSpeakSoundVrRva)};
    if (target.offset() != kSpeakSoundVrRva ||
        std::memcmp(reinterpret_cast<const void*>(target.address()),
                    kSpeakSoundVrPrologue.data(), kSpeakSoundVrPrologue.size()) != 0) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound VR signature mismatch at RVA 0x{:X}",
                         target.offset());
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
    g_targetSpeakSound = target.get();

    const auto initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for SpeakSound ({})",
                         static_cast<int>(initialize));
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    g_hookTarget = reinterpret_cast<void*>(target.address());
    const auto create = MH_CreateHook(
        g_hookTarget, reinterpret_cast<void*>(&HookSpeakSound),
        reinterpret_cast<void**>(&g_originalSpeakSound));
    if (create != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound hook creation failed ({})",
                         static_cast<int>(create));
        g_hookTarget = nullptr;
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    const auto enable = MH_EnableHook(g_hookTarget);
    if (enable != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound hook enable failed ({})",
                         static_cast<int>(enable));
        MH_RemoveHook(g_hookTarget);
        g_hookTarget = nullptr;
        g_originalSpeakSound = nullptr;
        g_targetSpeakSound = nullptr;
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed exact Actor::SpeakSound hook at VR RVA 0x{:X}",
                    kSpeakSoundVrRva);
    return true;
}

void Uninstall() noexcept
{
    if (g_hookTarget) {
        MH_DisableHook(g_hookTarget);
        MH_RemoveHook(g_hookTarget);
    }
    g_hookTarget = nullptr;
    g_originalSpeakSound = nullptr;
    g_targetSpeakSound = nullptr;
    g_installAttempted.store(false, std::memory_order_release);
}

bool PlayRemoteVoice(RE::Actor& a_actor, const char* a_resourcePath) noexcept
{
    if (!a_resourcePath || a_resourcePath[0] == '\0')
        return false;
    auto* speak = g_originalSpeakSound ? g_originalSpeakSound : g_targetSpeakSound;
    if (!speak)
        return false;

    ScopedRemoteReplay replay;
    std::uint32_t handle[3]{std::numeric_limits<std::uint32_t>::max(), 0, 0};
    const auto result = speak(&a_actor, a_resourcePath, handle, 0, 0x32, 0, 0, 0, 0, false, 0,
                              false, true, true);
    return std::isfinite(result) && result > 0.0F;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueHooks
