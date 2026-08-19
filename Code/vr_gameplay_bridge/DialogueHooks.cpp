#include "DialogueHooks.h"

#include "AvatarManager.h"
#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "vr_dialogue_hook_policy.h"

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
constexpr std::uint64_t kShowSubtitleVrRva = 0x08F9C60;
constexpr std::array<std::uint8_t, 16> kShowSubtitleVrPrologue{
    0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
    0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
};
constexpr std::size_t kMaximumSubtitleBytes = 512;

// VR callers consume the return value from XMM0 as a duration. The fourteen
// argument layout is independently visible at each direct callsite.
using SpeakSound = float (*)(RE::Actor*, const char*, std::uint32_t*, std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, bool,
                             std::uint64_t, bool, bool, bool);
// Address Library ID 51753 resolves to this exact VR body. Its decrypted
// function stores a SubtitleInfo from these four arguments under the manager lock.
using ShowSubtitle = void (*)(RE::SubtitleManager*, RE::TESObjectREFR*, const char*, bool);

SpeakSound g_targetSpeakSound{};
SpeakSound g_originalSpeakSound{};
ShowSubtitle g_originalShowSubtitle{};
void* g_speakSoundHookTarget{};
void* g_showSubtitleHookTarget{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_missingSpeakSoundTrampolineLogged{};
DialogueHookPolicy::HookAttachment g_speakSoundAttachment{};
DialogueHookPolicy::HookAttachment g_showSubtitleAttachment{};
bool g_installRetainedDegraded{};
thread_local std::uint32_t g_remoteReplayDepth{};
thread_local std::uint32_t g_remoteSubtitleDepth{};
std::atomic<std::uint64_t> g_nextSubtitleActionId{};
std::atomic<std::uint64_t> g_nextSubtitleTextId{};

[[nodiscard]] bool IsSafeDisableStatus(const MH_STATUS aStatus) noexcept
{
    return aStatus == MH_OK || aStatus == MH_ERROR_DISABLED || aStatus == MH_ERROR_NOT_CREATED;
}

[[nodiscard]] bool IsSafeRemoveStatus(const MH_STATUS aStatus) noexcept
{
    return aStatus == MH_OK || aStatus == MH_ERROR_NOT_CREATED;
}

[[nodiscard]] bool DetachHook(
    const char* aName,
    void* aTarget,
    DialogueHookPolicy::HookAttachment& arAttachment) noexcept
{
    return DialogueHookPolicy::TryDetachHook(
        arAttachment,
        [aName, aTarget]() noexcept {
            const auto status = MH_DisableHook(aTarget);
            if (!IsSafeDisableStatus(status))
                SKSE::log::critical("SkyrimTogetherVRGameplayBridge: {} disable failed ({}); preserving trampoline", aName, static_cast<int>(status));
            return IsSafeDisableStatus(status);
        },
        [aName, aTarget]() noexcept {
            const auto status = MH_RemoveHook(aTarget);
            if (!IsSafeRemoveStatus(status))
                SKSE::log::critical("SkyrimTogetherVRGameplayBridge: {} removal failed ({}); preserving hook state", aName, static_cast<int>(status));
            return IsSafeRemoveStatus(status);
        });
}

void ClearDetachedHookState() noexcept
{
    g_speakSoundHookTarget = nullptr;
    g_showSubtitleHookTarget = nullptr;
    g_originalSpeakSound = nullptr;
    g_originalShowSubtitle = nullptr;
    g_targetSpeakSound = nullptr;
    g_installRetainedDegraded = false;
    g_missingSpeakSoundTrampolineLogged.store(false, std::memory_order_relaxed);
}

[[nodiscard]] bool RollbackFailedInstall(const char* aStage) noexcept
{
    const bool subtitleDetached = DetachHook(
        "ShowSubtitle", g_showSubtitleHookTarget, g_showSubtitleAttachment);
    const bool speechDetached = DetachHook(
        "SpeakSound", g_speakSoundHookTarget, g_speakSoundAttachment);
    if (subtitleDetached && speechDetached)
    {
        ClearDetachedHookState();
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    g_installRetainedDegraded = true;
    BridgeEndpoint::Get().Fault("dialogue hook rollback could not prove detachment");
    SKSE::log::critical(
        "SkyrimTogetherVRGameplayBridge: {} rollback could not prove all dialogue detours detached; retaining the plugin and callable trampolines in degraded mode",
        aStage);
    return true;
}

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

class ScopedRemoteSubtitle final
{
public:
    ScopedRemoteSubtitle() noexcept { ++g_remoteSubtitleDepth; }
    ~ScopedRemoteSubtitle() noexcept
    {
        if (g_remoteSubtitleDepth != 0)
            --g_remoteSubtitleDepth;
    }
};

[[nodiscard]] bool IsValidUtf8(const char* a_text, const std::size_t a_length) noexcept
{
    const auto isContinuation = [a_text, a_length](const std::size_t a_index) noexcept {
        return a_index < a_length &&
               (static_cast<std::uint8_t>(a_text[a_index]) & 0xC0u) == 0x80u;
    };
    for (std::size_t index = 0; index < a_length;) {
        const auto byte = static_cast<std::uint8_t>(a_text[index]);
        if (byte <= 0x7Fu) {
            ++index;
        } else if (byte >= 0xC2u && byte <= 0xDFu && isContinuation(index + 1)) {
            index += 2;
        } else if (byte == 0xE0u && index + 2 < a_length &&
                   static_cast<std::uint8_t>(a_text[index + 1]) >= 0xA0u &&
                   static_cast<std::uint8_t>(a_text[index + 1]) <= 0xBFu && isContinuation(index + 2)) {
            index += 3;
        } else if ((byte >= 0xE1u && byte <= 0xECu || byte >= 0xEEu && byte <= 0xEFu) &&
                   isContinuation(index + 1) && isContinuation(index + 2)) {
            index += 3;
        } else if (byte == 0xEDu && index + 2 < a_length &&
                   static_cast<std::uint8_t>(a_text[index + 1]) >= 0x80u &&
                   static_cast<std::uint8_t>(a_text[index + 1]) <= 0x9Fu && isContinuation(index + 2)) {
            index += 3;
        } else if (byte == 0xF0u && index + 3 < a_length &&
                   static_cast<std::uint8_t>(a_text[index + 1]) >= 0x90u &&
                   static_cast<std::uint8_t>(a_text[index + 1]) <= 0xBFu &&
                   isContinuation(index + 2) && isContinuation(index + 3)) {
            index += 4;
        } else if (byte >= 0xF1u && byte <= 0xF3u && isContinuation(index + 1) &&
                   isContinuation(index + 2) && isContinuation(index + 3)) {
            index += 4;
        } else if (byte == 0xF4u && index + 3 < a_length &&
                   static_cast<std::uint8_t>(a_text[index + 1]) >= 0x80u &&
                   static_cast<std::uint8_t>(a_text[index + 1]) <= 0x8Fu &&
                   isContinuation(index + 2) && isContinuation(index + 3)) {
            index += 4;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint64_t NextNonzero(std::atomic<std::uint64_t>& ar_counter) noexcept
{
    std::uint64_t value{};
    do {
        value = ar_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    } while (value == 0);
    return value;
}

[[nodiscard]] std::uint32_t GetCurrentSubtitleTopicFormId(RE::TESObjectREFR& a_speaker) noexcept
{
    auto* topicManager = RE::MenuTopicManager::GetSingleton();
    if (!topicManager || !topicManager->IsCurrentSpeaker(a_speaker.GetHandle()) ||
        !topicManager->currentTopicInfo || !topicManager->currentTopicInfo->parentTopic)
        return 0;
    return topicManager->currentTopicInfo->parentTopic->GetFormID();
}

void CaptureSubtitle(RE::TESObjectREFR* a_speaker, const char* a_text) noexcept
{
    try {
        auto* actor = a_speaker ? a_speaker->As<RE::Actor>() : nullptr;
        if (!actor || !a_text || AvatarManager::Get().IsManagedRemoteActor(actor))
            return;

        std::size_t byteCount{};
        while (byteCount <= kMaximumSubtitleBytes && a_text[byteCount] != '\0')
            ++byteCount;
        if (byteCount == 0 || byteCount > kMaximumSubtitleBytes || !IsValidUtf8(a_text, byteCount))
            return;

        auto& endpoint = BridgeEndpoint::Get();
        if (!endpoint.IsOperational() || !endpoint.Mapping() ||
            !HasCapability(endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire),
                           CapabilityForDomain(GameplayDomain::Dialogue)))
            return;

        const auto identity = endpoint.SnapshotIdentity(0);
        if (identity.ServerInstanceNonce == 0 || identity.ConnectionGeneration == 0 ||
            identity.LifecycleEpoch == 0)
            return;

        const auto chunkCount = static_cast<std::uint16_t>(
            (byteCount + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
        if (chunkCount == 0 || chunkCount > kMaximumGameplayTextChunks)
            return;

        const auto actionId = NextNonzero(g_nextSubtitleActionId);
        const auto textId = NextNonzero(g_nextSubtitleTextId);
        const auto topicFormId = GetCurrentSubtitleTopicFormId(*a_speaker);
        std::array<EventRecord, kMaximumGameplayTextChunks> records{};
        for (std::uint16_t index = 0; index < chunkCount; ++index) {
            auto& record = records[index];
            record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayTextChunk);
            record.Header.PayloadSize = kFixedPayloadBytes;
            record.Header.Identity = identity;
            record.Header.Identity.ActionId = actionId;
            auto& payload = record.Payload.LocalGameplayTextChunk;
            payload.TargetHandle = kLocalPlayerHandle;
            payload.TargetLocalFormId = actor->GetFormID();
            payload.Domain = static_cast<std::uint16_t>(GameplayDomain::Dialogue);
            payload.Action = static_cast<std::uint16_t>(GameplayAction::Subtitle);
            payload.TextId = textId;
            payload.ChunkIndex = index;
            payload.ChunkCount = chunkCount;
            payload.AuxiliaryLocalFormId = topicFormId;
            const auto offset = static_cast<std::size_t>(index) * kGameplayTextBytesPerChunk;
            payload.ByteCount = static_cast<std::uint16_t>(
                std::min<std::size_t>(kGameplayTextBytesPerChunk, byteCount - offset));
            std::memcpy(payload.Utf8Bytes, a_text + offset, payload.ByteCount);
        }
        static_cast<void>(endpoint.TryPushEvents(records.data(), chunkCount));
    } catch (...) {
    }
}

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
    // Install verifies and publishes this trampoline before it enables the
    // detour. Local speech is always delegated to the engine before capture.
    const auto original = g_originalSpeakSound;
    if (!original) {
        if (!g_missingSpeakSoundTrampolineLogged.exchange(true, std::memory_order_relaxed)) {
            SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled SpeakSound detour has no trampoline; "
                "local dialogue capture is disabled until the hook is removed");
        }
        return 0.0F;
    }
    const auto result = original(
        a_actor, a_resourcePath, a_handle, a_arg4, a_priority, a_arg6, a_arg7, a_arg8,
        a_arg9, a_arg10, a_arg11, a_arg12, a_arg13, a_arg14);
    if (std::isfinite(result) && result > 0.0F && g_remoteReplayDepth == 0 && a_actor && a_resourcePath &&
        !AvatarManager::Get().IsManagedRemoteActor(a_actor))
        LocalGameplayCapture::CaptureDialogueVoice(a_actor->GetFormID(), a_resourcePath);
    return result;
}

void HookShowSubtitle(
    RE::SubtitleManager* a_manager,
    RE::TESObjectREFR* a_speaker,
    const char* a_text,
    const bool a_forceDisplay)
{
    if (!g_originalShowSubtitle)
        return;
    g_originalShowSubtitle(a_manager, a_speaker, a_text, a_forceDisplay);
    if (g_remoteSubtitleDepth == 0)
        CaptureSubtitle(a_speaker, a_text);
}
} // namespace

bool Install() noexcept
{
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return g_installRetainedDegraded ||
               (g_speakSoundAttachment.Enabled && g_showSubtitleAttachment.Enabled &&
                g_originalSpeakSound != nullptr && g_originalShowSubtitle != nullptr);

    REL::Relocation<SpeakSound> speakTarget{REL::Offset(kSpeakSoundVrRva)};
    if (speakTarget.offset() != kSpeakSoundVrRva ||
        std::memcmp(reinterpret_cast<const void*>(speakTarget.address()),
                    kSpeakSoundVrPrologue.data(), kSpeakSoundVrPrologue.size()) != 0) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound VR signature mismatch at RVA 0x{:X}",
                         speakTarget.offset());
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    REL::Relocation<ShowSubtitle> subtitleTarget{REL::ID(51753)};
    if (subtitleTarget.offset() != kShowSubtitleVrRva ||
        std::memcmp(reinterpret_cast<const void*>(subtitleTarget.address()),
                    kShowSubtitleVrPrologue.data(), kShowSubtitleVrPrologue.size()) != 0) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: ShowSubtitle VR signature mismatch at RVA 0x{:X}",
                         subtitleTarget.offset());
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
    g_targetSpeakSound = speakTarget.get();
    SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: validated exact SpeakSound entry; creating MinHook trampoline");

    const auto initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for SpeakSound ({})",
                         static_cast<int>(initialize));
        g_targetSpeakSound = nullptr;
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    g_speakSoundHookTarget = reinterpret_cast<void*>(speakTarget.address());
    void* speakSoundTrampoline{};
    const auto createSpeak = MH_CreateHook(
        g_speakSoundHookTarget, reinterpret_cast<void*>(&HookSpeakSound),
        &speakSoundTrampoline);
    if (createSpeak != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound hook creation failed ({})",
                         static_cast<int>(createSpeak));
        g_speakSoundHookTarget = nullptr;
        g_targetSpeakSound = nullptr;
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }
    g_speakSoundAttachment.Created = true;
    if (!DialogueHookPolicy::CanEnableSpeakSoundHook(
            reinterpret_cast<std::uintptr_t>(g_speakSoundHookTarget),
            reinterpret_cast<std::uintptr_t>(speakSoundTrampoline))) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: SpeakSound hook refused because MinHook returned an invalid trampoline");
        return RollbackFailedInstall("SpeakSound trampoline validation");
    }
    g_originalSpeakSound = reinterpret_cast<SpeakSound>(speakSoundTrampoline);

    g_showSubtitleHookTarget = reinterpret_cast<void*>(subtitleTarget.address());
    void* showSubtitleTrampoline{};
    const auto createSubtitle = MH_CreateHook(
        g_showSubtitleHookTarget, reinterpret_cast<void*>(&HookShowSubtitle),
        &showSubtitleTrampoline);
    if (createSubtitle != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: ShowSubtitle hook creation failed ({})",
                         static_cast<int>(createSubtitle));
        return RollbackFailedInstall("ShowSubtitle hook creation");
    }
    g_showSubtitleAttachment.Created = true;
    if (!DialogueHookPolicy::CanEnableSpeakSoundHook(
            reinterpret_cast<std::uintptr_t>(g_showSubtitleHookTarget),
            reinterpret_cast<std::uintptr_t>(showSubtitleTrampoline))) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: ShowSubtitle hook refused because MinHook returned an invalid trampoline");
        return RollbackFailedInstall("ShowSubtitle trampoline validation");
    }
    g_originalShowSubtitle = reinterpret_cast<ShowSubtitle>(showSubtitleTrampoline);

    const auto enableSubtitle = MH_EnableHook(g_showSubtitleHookTarget);
    if (enableSubtitle != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: ShowSubtitle hook enable failed ({})",
                         static_cast<int>(enableSubtitle));
        return RollbackFailedInstall("ShowSubtitle hook enable");
    }
    g_showSubtitleAttachment.Enabled = true;

    const auto enableSpeak = MH_EnableHook(g_speakSoundHookTarget);
    if (enableSpeak != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound hook enable failed ({})",
                         static_cast<int>(enableSpeak));
        return RollbackFailedInstall("SpeakSound hook enable");
    }
    g_speakSoundAttachment.Enabled = true;
    SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: SpeakSound detour enabled with verified trampoline; "
        "local capture remains observational");

    SKSE::log::info("SkyrimTogetherVRGameplayBridge: installed exact dialogue hooks at VR RVAs 0x{:X} and 0x{:X}",
                    kSpeakSoundVrRva, kShowSubtitleVrRva);
    return true;
}

bool Uninstall() noexcept
{
    if (g_speakSoundHookTarget || g_showSubtitleHookTarget)
        SKSE::log::info("SkyrimTogetherVRGameplayBridge: removing dialogue hooks");
    const bool subtitleDetached = DetachHook(
        "ShowSubtitle", g_showSubtitleHookTarget, g_showSubtitleAttachment);
    const bool speechDetached = DetachHook(
        "SpeakSound", g_speakSoundHookTarget, g_speakSoundAttachment);
    if (!subtitleDetached || !speechDetached) {
        g_installRetainedDegraded = true;
        SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: dialogue hook uninstall incomplete; preserving callable trampolines");
        return false;
    }
    ClearDetachedHookState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
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

bool ShowRemoteSubtitle(RE::Actor& a_speaker, const char* a_text) noexcept
{
    auto* manager = RE::SubtitleManager::GetSingleton();
    if (!manager || !a_text || !g_originalShowSubtitle)
        return false;

    ScopedRemoteSubtitle replay;
    g_originalShowSubtitle(manager, &a_speaker, a_text, false);
    return true;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueHooks
