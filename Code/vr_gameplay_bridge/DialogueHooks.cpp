#include "DialogueHooks.h"

#include "AvatarManager.h"
#include "BridgeBatchPolicy.h"
#include "BridgeEndpoint.h"
#include "LocalGameplayCapture.h"
#include "VrNoThrow.h"
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
constexpr std::size_t kMaximumDialogueChoices = 256;
constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};

// VR callers consume the return value from XMM0 as a duration. The fourteen
// argument layout is independently visible at each direct callsite.
using SpeakSound = float (*)(RE::Actor*, const char*, std::uint32_t*, std::uint32_t, std::uint32_t,
                             std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, bool,
                             std::uint64_t, bool, bool, bool);
// Address Library ID 51753 resolves to this exact VR body. Its decrypted
// function stores a SubtitleInfo from these four arguments under the manager lock.
using ShowSubtitle = void (*)(RE::SubtitleManager*, RE::TESObjectREFR*, const char*, bool);
// The verified x64 body receives RCX=MenuTopicManager* and EDX=int32 index.
using PlayDialogueOption = bool (*)(RE::MenuTopicManager*, std::int32_t);

SpeakSound g_targetSpeakSound{};
SpeakSound g_originalSpeakSound{};
ShowSubtitle g_originalShowSubtitle{};
PlayDialogueOption g_originalPlayDialogueOption{};
void* g_speakSoundHookTarget{};
void* g_showSubtitleHookTarget{};
void* g_playDialogueOptionHookTarget{};
std::atomic_bool g_installAttempted{};
std::atomic_bool g_missingSpeakSoundTrampolineLogged{};
std::atomic_bool g_missingPlayDialogueOptionTrampolineLogged{};
enum class PresentationDiagnostic : std::size_t
{
    UnmanagedNative,
    ManagedSuppression,
    ReplayAttempt,
    ReplaySuccess,
    ReplayRejected,
    NativeFallback,
    Count,
};

struct PresentationDiagnostics
{
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(PresentationDiagnostic::Count)> Counts{};
};

PresentationDiagnostics g_speechDiagnostics{};
PresentationDiagnostics g_subtitleDiagnostics{};
std::atomic<std::uint64_t> g_nativeSpeechResultCount{};
std::atomic<std::uint64_t> g_suppressedManagedRemoteSpeechCount{};
std::atomic<std::uint64_t> g_suppressedManagedRemoteSubtitleCount{};
DialogueHookPolicy::HookAttachment g_speakSoundAttachment{};
DialogueHookPolicy::HookAttachment g_showSubtitleAttachment{};
DialogueHookPolicy::HookAttachment g_playDialogueOptionAttachment{};
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

[[nodiscard]] bool IsExpectedVrRuntime() noexcept
{
    return REL::Module::IsVR() && REL::Module::get().version() == kExpectedSkyrimVrRuntime;
}

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    if (!IsExpectedVrRuntime())
        return false;

    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (a_address < text.address() || a_address - text.address() >= text.size())
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    constexpr DWORD kExecutableProtection =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutableProtection) != 0;
}

template <std::size_t N>
[[nodiscard]] bool IsVerifiedExecutableTarget(
    const std::uintptr_t a_address,
    const std::array<std::uint8_t, N>& a_prologue) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    return text.size() >= a_prologue.size() && a_address >= text.address() &&
           a_address - text.address() <= text.size() - a_prologue.size() &&
           IsExecutableTarget(a_address) &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_prologue.data(), a_prologue.size()) == 0;
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
                NoThrow::BestEffort([&] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: {} disable failed ({}); preserving trampoline", aName, static_cast<int>(status)); });
            return IsSafeDisableStatus(status);
        },
        [aName, aTarget]() noexcept {
            const auto status = MH_RemoveHook(aTarget);
            if (!IsSafeRemoveStatus(status))
                NoThrow::BestEffort([&] { SKSE::log::critical("SkyrimTogetherVRGameplayBridge: {} removal failed ({}); preserving hook state", aName, static_cast<int>(status)); });
            return IsSafeRemoveStatus(status);
        });
}

void ClearDetachedHookState() noexcept
{
    g_speakSoundHookTarget = nullptr;
    g_showSubtitleHookTarget = nullptr;
    g_playDialogueOptionHookTarget = nullptr;
    g_originalSpeakSound = nullptr;
    g_originalShowSubtitle = nullptr;
    g_originalPlayDialogueOption = nullptr;
    g_targetSpeakSound = nullptr;
    g_installRetainedDegraded = false;
    g_missingSpeakSoundTrampolineLogged.store(false, std::memory_order_relaxed);
    g_missingPlayDialogueOptionTrampolineLogged.store(false, std::memory_order_relaxed);
    for (auto& count : g_speechDiagnostics.Counts)
        count.store(0, std::memory_order_relaxed);
    for (auto& count : g_subtitleDiagnostics.Counts)
        count.store(0, std::memory_order_relaxed);
    g_nativeSpeechResultCount.store(0, std::memory_order_relaxed);
    g_suppressedManagedRemoteSpeechCount.store(0, std::memory_order_relaxed);
    g_suppressedManagedRemoteSubtitleCount.store(0, std::memory_order_relaxed);
}

[[nodiscard]] bool RollbackFailedInstall(const char* aStage) noexcept
{
    const bool choiceDetached = DetachHook(
        "MenuTopicManager::PlayDialogueOption", g_playDialogueOptionHookTarget,
        g_playDialogueOptionAttachment);
    const bool subtitleDetached = DetachHook(
        "ShowSubtitle", g_showSubtitleHookTarget, g_showSubtitleAttachment);
    const bool speechDetached = DetachHook(
        "SpeakSound", g_speakSoundHookTarget, g_speakSoundAttachment);
    if (choiceDetached && subtitleDetached && speechDetached)
    {
        ClearDetachedHookState();
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    g_installRetainedDegraded = true;
    NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("dialogue hook rollback could not prove detachment"); });
    NoThrow::BestEffort([&] { SKSE::log::critical(
        "SkyrimTogetherVRGameplayBridge: {} rollback could not prove all dialogue detours detached; retaining the plugin and callable trampolines in degraded mode",
        aStage);
    });
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

void RecordManagedRemoteDialogueSuppression(
    std::atomic<std::uint64_t>& ar_count,
    const char* a_eventName) noexcept
{
    const auto total = ar_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (BridgeBatchPolicy::ShouldLogAggregate(total)) {
        NoThrow::BestEffort([&] { SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: suppressed managed remote {} before native presentation "
            "(aggregate total={})",
            a_eventName, total);
        });
    }
}

[[nodiscard]] constexpr const char* PresentationDiagnosticName(const PresentationDiagnostic a_diagnostic) noexcept
{
    switch (a_diagnostic) {
    case PresentationDiagnostic::UnmanagedNative:
        return "unmanaged native playback";
    case PresentationDiagnostic::ManagedSuppression:
        return "managed suppression";
    case PresentationDiagnostic::ReplayAttempt:
        return "replay attempt";
    case PresentationDiagnostic::ReplaySuccess:
        return "replay success";
    case PresentationDiagnostic::ReplayRejected:
        return "replay rejection";
    case PresentationDiagnostic::NativeFallback:
        return "native fallback";
    case PresentationDiagnostic::Count:
        break;
    }
    return "unknown";
}

void RecordPresentationDiagnostic(
    PresentationDiagnostics& ar_diagnostics,
    const PresentationDiagnostic a_diagnostic,
    const char* a_presentationName) noexcept
{
    auto& count = ar_diagnostics.Counts[static_cast<std::size_t>(a_diagnostic)];
    const auto total = count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (BridgeBatchPolicy::ShouldLogAggregate(total)) {
        NoThrow::BestEffort([&] { SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: dialogue {} {} (aggregate total={})",
            a_presentationName, PresentationDiagnosticName(a_diagnostic), total);
        });
    }
}

void RecordNativeSpeechResult(const float a_result, const bool aHandlePresent) noexcept
{
    const auto total = g_nativeSpeechResultCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!BridgeBatchPolicy::ShouldLogAggregate(total))
        return;

    NoThrow::BestEffort([&] {
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: native speech scheduling result={} accepted={} "
            "handlePresent={} (aggregate total={})",
            a_result, std::isfinite(a_result) && a_result > 0.0F, aHandlePresent, total);
    });
}

[[nodiscard]] bool IsManagedRemotePresentationRelayAvailable(const bool a_hasReplayTrampoline) noexcept
{
    try {
        auto& endpoint = BridgeEndpoint::Get();
        const auto* mapping = endpoint.Mapping();
        if (!a_hasReplayTrampoline || !mapping || !endpoint.IsOperational() ||
            static_cast<EndpointState>(mapping->Header.State.load(std::memory_order_acquire)) != EndpointState::Ready)
            return false;

        SessionIdentitySnapshot session{};
        if (!TrySnapshotSessionIdentity(mapping->Header, session) || session.ServerInstanceNonce == 0 ||
            session.ConnectionGeneration == 0 || mapping->Header.LifecycleEpoch.load(std::memory_order_acquire) == 0)
            return false;

        return static_cast<EndpointState>(mapping->Header.State.load(std::memory_order_acquire)) == EndpointState::Ready &&
               HasCapability(mapping->Header.ActiveCapabilities.load(std::memory_order_acquire),
                             CapabilityForDomain(GameplayDomain::Dialogue));
    } catch (...) {
        return false;
    }
}

void CaptureSubtitle(RE::TESObjectREFR* a_speaker, const char* a_text) noexcept
{
    try {
        auto* actor = a_speaker ? a_speaker->As<RE::Actor>() : nullptr;
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto speakerFormId = actor ? actor->GetFormID() : 0;
        if (!a_text || !DialogueHookPolicy::ShouldCaptureLocalNpcSpeaker(
                           actor != nullptr, player != nullptr, actor == player,
                           actor && AvatarManager::Get().IsManagedRemoteActor(actor),
                           speakerFormId != 0,
                           speakerFormId != 0 && LocalGameplayCapture::IsNpcObserved(speakerFormId)))
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
        std::array<EventRecord, kMaximumGameplayTextChunks> records{};
        for (std::uint16_t index = 0; index < chunkCount; ++index) {
            auto& record = records[index];
            record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayTextChunk);
            record.Header.PayloadSize = kFixedPayloadBytes;
            record.Header.Identity = identity;
            record.Header.Identity.ActionId = actionId;
            auto& payload = record.Payload.LocalGameplayTextChunk;
            payload.TargetHandle = kLocalPlayerHandle;
            payload.TargetLocalFormId = speakerFormId;
            payload.Domain = static_cast<std::uint16_t>(GameplayDomain::Dialogue);
            payload.Action = static_cast<std::uint16_t>(GameplayAction::Subtitle);
            payload.TextId = textId;
            payload.ChunkIndex = index;
            payload.ChunkCount = chunkCount;
            // Skyrim's subtitle transport has no topic contract. Retaining
            // a locally resolved topic would leak a non-portable form ID.
            payload.AuxiliaryLocalFormId = DialogueHookPolicy::SkyrimSubtitleTopicFormId();
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
    try {
    const bool hasSpeaker = a_actor != nullptr;
    const bool managedRemoteSpeaker = hasSpeaker && AvatarManager::Get().IsManagedRemoteActor(a_actor);
    const auto disposition = DialogueHookPolicy::DecideSpeakerPresentation(
        hasSpeaker, managedRemoteSpeaker, g_remoteReplayDepth != 0,
        managedRemoteSpeaker && IsManagedRemotePresentationRelayAvailable(g_originalSpeakSound != nullptr));
    if (disposition.SuppressManagedRemote) {
        RecordManagedRemoteDialogueSuppression(g_suppressedManagedRemoteSpeechCount, "SpeakSound");
        RecordPresentationDiagnostic(
            g_speechDiagnostics, PresentationDiagnostic::ManagedSuppression, "speech");
        return 0.0F;
    }
    if (disposition.FallbackToNative) {
        RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::NativeFallback, "speech");
    } else if (hasSpeaker && !managedRemoteSpeaker && g_remoteReplayDepth == 0) {
        RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::UnmanagedNative, "speech");
    }

    // Install verifies and publishes this trampoline before it enables the
    // detour. The desktop hook emits a valid local NPC voice before the
    // engine call; no poll captures voices, so this cannot double-emit.
    const auto original = g_originalSpeakSound;
    if (!original) {
        if (!g_missingSpeakSoundTrampolineLogged.exchange(true, std::memory_order_relaxed)) {
            NoThrow::BestEffort([] { SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled SpeakSound detour has no trampoline; "
                "local dialogue capture is disabled until the hook is removed");
            });
        }
        return 0.0F;
    }
    const auto result = DialogueHookPolicy::CaptureBeforeNativeIf(
        disposition.CaptureLocal && a_resourcePath != nullptr,
        [&]() noexcept {
            static_cast<void>(LocalGameplayCapture::CaptureDialogueVoice(
                a_actor->GetFormID(), a_resourcePath));
        },
        [&]() {
            return original(
                a_actor, a_resourcePath, a_handle, a_arg4, a_priority, a_arg6, a_arg7, a_arg8,
                a_arg9, a_arg10, a_arg11, a_arg12, a_arg13, a_arg14);
        });
    RecordNativeSpeechResult(result, a_handle != nullptr);
    return result;
    } catch (...) {
        return 0.0F;
    }
}

void HookShowSubtitle(
    RE::SubtitleManager* a_manager,
    RE::TESObjectREFR* a_speaker,
    const char* a_text,
    const bool a_forceDisplay)
{
    try {
    auto* actor = a_speaker ? a_speaker->As<RE::Actor>() : nullptr;
    const bool managedRemoteSpeaker = actor && AvatarManager::Get().IsManagedRemoteActor(actor);
    const auto disposition = DialogueHookPolicy::DecideSpeakerPresentation(
        actor != nullptr, managedRemoteSpeaker, g_remoteSubtitleDepth != 0,
        managedRemoteSpeaker && IsManagedRemotePresentationRelayAvailable(g_originalShowSubtitle != nullptr));
    if (disposition.SuppressManagedRemote) {
        RecordManagedRemoteDialogueSuppression(g_suppressedManagedRemoteSubtitleCount, "ShowSubtitle");
        RecordPresentationDiagnostic(
            g_subtitleDiagnostics, PresentationDiagnostic::ManagedSuppression, "subtitle");
        return;
    }
    if (disposition.FallbackToNative) {
        RecordPresentationDiagnostic(g_subtitleDiagnostics, PresentationDiagnostic::NativeFallback, "subtitle");
    } else if (actor && !managedRemoteSpeaker && g_remoteSubtitleDepth == 0) {
        RecordPresentationDiagnostic(g_subtitleDiagnostics, PresentationDiagnostic::UnmanagedNative, "subtitle");
    }

    if (!g_originalShowSubtitle)
        return;
    // As on desktop, capture precedes native presentation. CaptureSubtitle
    // independently rejects player, remote, invalid, and unobserved actors.
    static_cast<void>(DialogueHookPolicy::CaptureBeforeNativeIf(
        disposition.CaptureLocal,
        [&]() noexcept { CaptureSubtitle(a_speaker, a_text); },
        [&]() {
            g_originalShowSubtitle(a_manager, a_speaker, a_text, a_forceDisplay);
            return true;
        }));
    } catch (...) {
        // The native body is never retried after a capture exception.
    }
}

[[nodiscard]] const RE::MenuTopicManager::Dialogue* ResolveLocalDialogueChoice(
    RE::MenuTopicManager* a_manager,
    const std::int32_t a_index) noexcept
{
    try {
    if (!a_manager || a_manager != RE::MenuTopicManager::GetSingleton() || !a_manager->menuOpen ||
        a_index < 0 || static_cast<std::size_t>(a_index) >= kMaximumDialogueChoices ||
        !a_manager->dialogueList)
        return nullptr;

    const auto targetIndex = static_cast<std::size_t>(a_index);
    std::size_t currentIndex{};
    for (auto iterator = a_manager->dialogueList->begin();
         iterator != a_manager->dialogueList->end() && currentIndex <= targetIndex &&
         currentIndex < kMaximumDialogueChoices;
         ++iterator, ++currentIndex)
    {
        if (currentIndex == targetIndex)
            return *iterator;
    }
    return nullptr;
    } catch (...) {
        return nullptr;
    }
}

[[nodiscard]] bool CopyDialogueChoiceText(
    const RE::MenuTopicManager::Dialogue& a_dialogue,
    std::array<char, kMaximumSubtitleBytes + 1>& ar_text) noexcept
{
    const char* source = a_dialogue.topicText.c_str();
    if (!source)
        return false;

    std::size_t length{};
    while (length < kMaximumSubtitleBytes && source[length] != '\0') {
        ar_text[length] = source[length];
        ++length;
    }
    if (length == 0 || source[length] != '\0')
        return false;
    ar_text[length] = '\0';
    return true;
}

bool HookPlayDialogueOption(
    RE::MenuTopicManager* a_manager,
    const std::int32_t a_index) noexcept
{
    try {
    const auto original = g_originalPlayDialogueOption;
    if (!original)
    {
        if (!g_missingPlayDialogueOptionTrampolineLogged.exchange(true, std::memory_order_relaxed))
        {
            NoThrow::BestEffort([] { SKSE::log::critical(
                "SkyrimTogetherVRGameplayBridge: enabled MenuTopicManager::PlayDialogueOption detour has no "
                "trampoline; exact dialogue-choice capture is disabled until the hook is removed");
            });
        }
        return false;
    }

    const bool remoteReplay = g_remoteReplayDepth != 0;
    const auto* dialogue = remoteReplay ? nullptr : ResolveLocalDialogueChoice(a_manager, a_index);
    std::array<char, kMaximumSubtitleBytes + 1> dialogueText{};
    const bool validSelection = dialogue && CopyDialogueChoiceText(*dialogue, dialogueText);

    // Engine acceptance is authoritative. Resolve and copy the typed list
    // entry before the call, but never publish unless this exact body returns
    // true after accepting the requested index.
    const bool accepted = original(a_manager, a_index);
    if (DialogueHookPolicy::ShouldCaptureExactDialogueChoice(
            accepted, remoteReplay, validSelection, true))
    {
        // The verified body selects this exact typed list entry. Keep only
        // its address as the polling identity; CaptureExactDialogueChoice
        // does not dereference it after the engine call.
        NoThrow::BestEffort([&] {
            static_cast<void>(LocalGameplayCapture::CaptureExactDialogueChoice(
                dialogue, dialogueText.data()));
        });
    }
    return accepted;
    } catch (...) {
        return false;
    }
}
} // namespace

bool Install() noexcept
{
    bool expected = false;
    if (!g_installAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return g_installRetainedDegraded ||
               (g_speakSoundAttachment.Enabled && g_showSubtitleAttachment.Enabled &&
                g_playDialogueOptionAttachment.Enabled && g_originalSpeakSound != nullptr &&
                g_originalShowSubtitle != nullptr && g_originalPlayDialogueOption != nullptr);

    try {
    REL::Relocation<SpeakSound> speakTarget{REL::Offset(kSpeakSoundVrRva)};
    REL::Relocation<ShowSubtitle> subtitleTarget{REL::ID(51753)};
    // Do not replace this verified RVA with the disproven generated desktop
    // alias 35269. It names a different VR function.
    REL::Relocation<PlayDialogueOption> playDialogueOptionTarget{
        REL::Offset(DialogueHookPolicy::kPlayDialogueOptionVrRva)};
    const auto moduleBase = REL::Module::get().base();
    if (!IsExpectedVrRuntime() ||
        !DialogueHookPolicy::IsPinnedPlayDialogueOptionTarget(
            DialogueHookPolicy::kPlayDialogueOptionVrRva) ||
        speakTarget.offset() != kSpeakSoundVrRva ||
        subtitleTarget.offset() != kShowSubtitleVrRva ||
        playDialogueOptionTarget.offset() != DialogueHookPolicy::kPlayDialogueOptionVrRva ||
        speakTarget.address() != moduleBase + kSpeakSoundVrRva ||
        subtitleTarget.address() != moduleBase + kShowSubtitleVrRva ||
        playDialogueOptionTarget.address() != moduleBase + DialogueHookPolicy::kPlayDialogueOptionVrRva ||
        !IsVerifiedExecutableTarget(speakTarget.address(), kSpeakSoundVrPrologue) ||
        !IsVerifiedExecutableTarget(subtitleTarget.address(), kShowSubtitleVrPrologue) ||
        !IsVerifiedExecutableTarget(playDialogueOptionTarget.address(),
                                    DialogueHookPolicy::kPlayDialogueOptionVrPrologue)) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: dialogue hook target validation failed "
            "(SpeakSound RVA=0x{:X}, ShowSubtitle RVA=0x{:X}, PlayDialogueOption RVA=0x{:X})",
            speakTarget.offset(), subtitleTarget.offset(), playDialogueOptionTarget.offset());
        g_installAttempted.store(false, std::memory_order_release);
        return false;
    }

    g_targetSpeakSound = speakTarget.get();
    SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: validated exact dialogue hook entries; creating MinHook trampolines");

    const auto initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: MinHook initialization failed for dialogue hooks ({})",
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

    g_playDialogueOptionHookTarget = reinterpret_cast<void*>(playDialogueOptionTarget.address());
    void* playDialogueOptionTrampoline{};
    const auto createPlayDialogueOption = MH_CreateHook(
        g_playDialogueOptionHookTarget, reinterpret_cast<void*>(&HookPlayDialogueOption),
        &playDialogueOptionTrampoline);
    if (createPlayDialogueOption != MH_OK) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: MenuTopicManager::PlayDialogueOption hook creation failed ({})",
            static_cast<int>(createPlayDialogueOption));
        return RollbackFailedInstall("MenuTopicManager::PlayDialogueOption hook creation");
    }
    g_playDialogueOptionAttachment.Created = true;
    if (!DialogueHookPolicy::CanEnableSpeakSoundHook(
            reinterpret_cast<std::uintptr_t>(g_playDialogueOptionHookTarget),
            reinterpret_cast<std::uintptr_t>(playDialogueOptionTrampoline))) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: MenuTopicManager::PlayDialogueOption hook refused because MinHook "
            "returned an invalid trampoline");
        return RollbackFailedInstall("MenuTopicManager::PlayDialogueOption trampoline validation");
    }
    g_originalPlayDialogueOption = reinterpret_cast<PlayDialogueOption>(playDialogueOptionTrampoline);

    // Mark the attachment before enabling. A failed enable does not prove
    // MinHook left target bytes unchanged, so rollback must still attempt a
    // disable and retain any callable trampoline if it cannot prove removal.
    g_playDialogueOptionAttachment.Enabled = true;
    const auto enablePlayDialogueOption = MH_EnableHook(g_playDialogueOptionHookTarget);
    if (enablePlayDialogueOption != MH_OK) {
        SKSE::log::error(
            "SkyrimTogetherVRGameplayBridge: MenuTopicManager::PlayDialogueOption hook enable failed ({})",
            static_cast<int>(enablePlayDialogueOption));
        return RollbackFailedInstall("MenuTopicManager::PlayDialogueOption hook enable");
    }

    g_showSubtitleAttachment.Enabled = true;
    const auto enableSubtitle = MH_EnableHook(g_showSubtitleHookTarget);
    if (enableSubtitle != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: ShowSubtitle hook enable failed ({})",
                         static_cast<int>(enableSubtitle));
        return RollbackFailedInstall("ShowSubtitle hook enable");
    }

    g_speakSoundAttachment.Enabled = true;
    const auto enableSpeak = MH_EnableHook(g_speakSoundHookTarget);
    if (enableSpeak != MH_OK) {
        SKSE::log::error("SkyrimTogetherVRGameplayBridge: SpeakSound hook enable failed ({})",
                         static_cast<int>(enableSpeak));
        return RollbackFailedInstall("SpeakSound hook enable");
    }

    SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: installed exact dialogue hooks at VR RVAs 0x{:X}, 0x{:X}, and 0x{:X}",
        kSpeakSoundVrRva, kShowSubtitleVrRva, DialogueHookPolicy::kPlayDialogueOptionVrRva);
    return true;
    } catch (...) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault("dialogue hook installation threw"); });
        NoThrow::BestEffort([] {
            SKSE::log::critical("SkyrimTogetherVRGameplayBridge: dialogue hook installation rejected an exception");
        });
        return RollbackFailedInstall("exception");
    }
}

bool Uninstall() noexcept
{
    if (g_speakSoundHookTarget || g_showSubtitleHookTarget || g_playDialogueOptionHookTarget)
        NoThrow::BestEffort([] { SKSE::log::info("SkyrimTogetherVRGameplayBridge: removing dialogue hooks"); });
    const bool choiceDetached = DetachHook(
        "MenuTopicManager::PlayDialogueOption", g_playDialogueOptionHookTarget,
        g_playDialogueOptionAttachment);
    const bool subtitleDetached = DetachHook(
        "ShowSubtitle", g_showSubtitleHookTarget, g_showSubtitleAttachment);
    const bool speechDetached = DetachHook(
        "SpeakSound", g_speakSoundHookTarget, g_speakSoundAttachment);
    if (!choiceDetached || !subtitleDetached || !speechDetached) {
        g_installRetainedDegraded = true;
        NoThrow::BestEffort([] { SKSE::log::critical(
            "SkyrimTogetherVRGameplayBridge: dialogue hook uninstall incomplete; preserving callable trampolines");
        });
        return false;
    }
    ClearDetachedHookState();
    g_installAttempted.store(false, std::memory_order_release);
    return true;
}

bool PlayRemoteVoice(RE::Actor& a_actor, const char* a_resourcePath) noexcept
{
    RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::ReplayAttempt, "speech");
    if (!a_resourcePath || a_resourcePath[0] == '\0') {
        RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::ReplayRejected, "speech");
        return false;
    }
    auto* speak = g_originalSpeakSound;
    if (!speak) {
        RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::ReplayRejected, "speech");
        return false;
    }

    try {
        ScopedRemoteReplay replay;
        std::uint32_t handle[3]{std::numeric_limits<std::uint32_t>::max(), 0, 0};
        const auto result = speak(&a_actor, a_resourcePath, handle, 0, 0x32, 0, 0, 0, 0, false, 0,
                                  false, true, true);
        if (std::isfinite(result) && result > 0.0F) {
            RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::ReplaySuccess, "speech");
            return true;
        }
        RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::ReplayRejected, "speech");
        return false;
    } catch (...) {
        RecordPresentationDiagnostic(g_speechDiagnostics, PresentationDiagnostic::ReplayRejected, "speech");
        return false;
    }
}

bool ShowRemoteSubtitle(RE::Actor& a_speaker, const char* a_text) noexcept
{
    RecordPresentationDiagnostic(g_subtitleDiagnostics, PresentationDiagnostic::ReplayAttempt, "subtitle");
    auto* manager = RE::SubtitleManager::GetSingleton();
    if (!manager || !a_text || !g_originalShowSubtitle) {
        RecordPresentationDiagnostic(g_subtitleDiagnostics, PresentationDiagnostic::ReplayRejected, "subtitle");
        return false;
    }

    try {
        ScopedRemoteSubtitle replay;
        g_originalShowSubtitle(manager, &a_speaker, a_text, false);
        RecordPresentationDiagnostic(g_subtitleDiagnostics, PresentationDiagnostic::ReplaySuccess, "subtitle");
        return true;
    } catch (...) {
        RecordPresentationDiagnostic(g_subtitleDiagnostics, PresentationDiagnostic::ReplayRejected, "subtitle");
        return false;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueHooks
