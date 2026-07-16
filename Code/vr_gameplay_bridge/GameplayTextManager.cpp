#include "GameplayTextManager.h"

#include "AvatarManager.h"
#include "DialogueHooks.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace SkyrimTogetherVR::GameplayAdapter
{
namespace
{
constexpr std::size_t kMaximumPendingTexts = 64;
constexpr std::uint64_t kShowSubtitleVrRva = 0x08F9C60;
constexpr std::array<std::uint8_t, 16> kShowSubtitleVrPrologue{
    0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
    0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
};

struct TextKey
{
    std::uint64_t ServerNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
    std::uint64_t EntityId{};
    std::uint32_t EntityGeneration{};
    std::uint64_t TextId{};

    [[nodiscard]] bool operator==(const TextKey&) const noexcept = default;
};

struct TextKeyHash
{
    [[nodiscard]] std::size_t operator()(const TextKey& a_key) const noexcept
    {
        std::size_t value{};
        const auto combine = [&value](const std::uint64_t a_part) {
            value ^= std::hash<std::uint64_t>{}(a_part) + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
        };
        combine(a_key.ServerNonce);
        combine(a_key.ConnectionGeneration);
        combine(a_key.LifecycleEpoch);
        combine(a_key.EntityId);
        combine(a_key.EntityGeneration);
        combine(a_key.TextId);
        return value;
    }
};

struct PendingText
{
    CommandRecord InitialCommand{};
    std::array<std::array<char, kGameplayTextBytesPerChunk>, kMaximumGameplayTextChunks> Chunks{};
    std::array<std::uint16_t, kMaximumGameplayTextChunks> Lengths{};
    std::bitset<kMaximumGameplayTextChunks> Received{};
    std::uint16_t ChunkCount{};
};

std::unordered_map<TextKey, PendingText, TextKeyHash> g_pending;

struct TextTargetKey
{
    std::uint64_t ServerNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
    std::uint64_t EntityId{};
    std::uint32_t EntityGeneration{};
    std::uint64_t TargetHandle{};

    [[nodiscard]] bool operator==(const TextTargetKey&) const noexcept = default;
};

struct TextTargetKeyHash
{
    [[nodiscard]] std::size_t operator()(const TextTargetKey& a_key) const noexcept
    {
        std::size_t value{};
        const auto combine = [&value](const std::uint64_t a_part) {
            value ^= std::hash<std::uint64_t>{}(a_part) + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
        };
        combine(a_key.ServerNonce);
        combine(a_key.ConnectionGeneration);
        combine(a_key.LifecycleEpoch);
        combine(a_key.EntityId);
        combine(a_key.EntityGeneration);
        combine(a_key.TargetHandle);
        return value;
    }
};

std::unordered_map<TextTargetKey, std::uint64_t, TextTargetKeyHash> g_lastAcceptedTextActions;

[[nodiscard]] TextKey MakeKey(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    return {identity.ServerInstanceNonce, identity.ConnectionGeneration, identity.LifecycleEpoch,
            identity.EntityId, identity.EntityGeneration, a_command.Payload.ApplyGameplayTextChunk.TextId};
}

[[nodiscard]] TextTargetKey MakeTargetKey(const CommandRecord& a_command) noexcept
{
    const auto& identity = a_command.Header.Identity;
    return {identity.ServerInstanceNonce, identity.ConnectionGeneration, identity.LifecycleEpoch,
            identity.EntityId, identity.EntityGeneration,
            a_command.Payload.ApplyGameplayTextChunk.TargetHandle.Value};
}

[[nodiscard]] bool HasMatchingTextMetadata(
    const CommandRecord& a_first,
    const CommandRecord& a_next) noexcept
{
    const auto& firstIdentity = a_first.Header.Identity;
    const auto& nextIdentity = a_next.Header.Identity;
    const auto& first = a_first.Payload.ApplyGameplayTextChunk;
    const auto& next = a_next.Payload.ApplyGameplayTextChunk;
    return firstIdentity.ActionId == nextIdentity.ActionId &&
           first.TargetHandle.Value == next.TargetHandle.Value &&
           first.TargetLocalFormId == next.TargetLocalFormId && first.Domain == next.Domain &&
           first.Action == next.Action && first.TextId == next.TextId &&
           first.AuxiliaryLocalFormId == next.AuxiliaryLocalFormId &&
           first.ChunkCount == next.ChunkCount;
}

[[nodiscard]] CommandStatus ResolveActorForText(
    const CommandRecord& a_command,
    RE::NiPointer<RE::Actor>& ar_actor) noexcept
{
    CommandRecord actionCommand{};
    actionCommand.Header = a_command.Header;
    actionCommand.Header.Kind = static_cast<std::uint16_t>(CommandKind::ApplyGameplayAction);
    const auto& source = a_command.Payload.ApplyGameplayTextChunk;
    auto& target = actionCommand.Payload.ApplyGameplayAction;
    target.TargetHandle = source.TargetHandle;
    target.TargetLocalFormId = source.TargetLocalFormId;
    target.Domain = source.Domain;
    target.Action = source.Action;
    return AvatarManager::Get().ResolveGameplayActor(actionCommand, ar_actor);
}

[[nodiscard]] CommandStatus PlayAnimationAndWait(
    RE::TESObjectREFR& a_reference,
    const std::string_view a_animation,
    const std::string_view a_event) noexcept
{
    auto* skyrimVm = RE::SkyrimVM::GetSingleton();
    auto* vm = skyrimVm ? skyrimVm->impl.get() : nullptr;
    auto* handles = vm ? vm->GetObjectHandlePolicy() : nullptr;
    if (!handles)
        return CommandStatus::Inactive;

    const auto handle = handles->GetHandleForObject(a_reference.GetFormType(), &a_reference);
    if (handle == handles->EmptyHandle())
        return CommandStatus::EngineRejected;

    const std::string animation{a_animation};
    const std::string event{a_event};
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
    return vm->DispatchMethodCall(
               handle,
               RE::BSFixedString("ObjectReference"),
               RE::BSFixedString("PlayAnimationAndWait"),
               RE::MakeFunctionArguments(
                   RE::BSFixedString(animation.c_str()),
                   RE::BSFixedString(event.c_str())),
               callback) ?
               CommandStatus::Success : CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ShowSubtitle(
    RE::Actor& a_speaker,
    const std::string& a_text,
    const std::uint32_t a_topicFormId) noexcept
{
    if (a_text.empty() || a_text.size() > 512 || a_text.find('\0') != std::string::npos)
        return CommandStatus::Malformed;
    if (a_topicFormId != 0 && !RE::TESForm::LookupByID<RE::TESTopic>(a_topicFormId))
        return CommandStatus::MissingForm;

    auto* manager = RE::SubtitleManager::GetSingleton();
    if (!manager)
        return CommandStatus::Inactive;

    // VR Address Library ID 51753 resolves to RVA 0x08F9C60 in SkyrimVR
    // 1.4.15. Its decrypted body validates the exact four-argument ABI and
    // appends a 0x20-byte SubtitleInfo under SubtitleManager's lock. The
    // similarly named generated ID 52626 row is a hash-table helper and must
    // never be used for this call.
    using ShowSubtitle = void(RE::SubtitleManager*, RE::TESObjectREFR*, const char*, bool);
    static REL::Relocation<ShowSubtitle> showSubtitle{REL::ID(51753)};
    if (showSubtitle.offset() != kShowSubtitleVrRva ||
        std::memcmp(reinterpret_cast<const void*>(showSubtitle.address()),
                    kShowSubtitleVrPrologue.data(), kShowSubtitleVrPrologue.size()) != 0)
        return CommandStatus::Unsupported;
    showSubtitle(manager, &a_speaker, a_text.c_str(), false);
    return CommandStatus::Success;
}

[[nodiscard]] bool IsSafeVoiceResourcePath(const std::string_view a_path) noexcept
{
    if (a_path.empty() || a_path.size() > 512 || a_path.front() == '/' || a_path.front() == '\\' ||
        a_path.find('\0') != std::string_view::npos || a_path.find(':') != std::string_view::npos)
        return false;

    std::size_t segmentStart{};
    while (segmentStart < a_path.size()) {
        const auto segmentEnd = a_path.find_first_of("/\\", segmentStart);
        const auto segment = a_path.substr(
            segmentStart,
            segmentEnd == std::string_view::npos ? std::string_view::npos : segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (segmentEnd == std::string_view::npos)
            break;
        segmentStart = segmentEnd + 1;
    }

    const auto hasExtension = [&a_path](const std::string_view a_extension) {
        if (a_path.size() < a_extension.size())
            return false;
        const auto suffix = a_path.substr(a_path.size() - a_extension.size());
        return std::equal(suffix.begin(), suffix.end(), a_extension.begin(), [](const char a_lhs, const char a_rhs) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(a_lhs))) == a_rhs;
        });
    };
    return hasExtension(".fuz") || hasExtension(".xwm") || hasExtension(".wav");
}

[[nodiscard]] CommandStatus PlayDialogueVoice(RE::Actor& a_actor, const std::string& a_resourcePath) noexcept
{
    if (!IsSafeVoiceResourcePath(a_resourcePath))
        return CommandStatus::Malformed;
    return DialogueHooks::PlayRemoteVoice(a_actor, a_resourcePath.c_str()) ?
               CommandStatus::Success : CommandStatus::EngineRejected;
}

[[nodiscard]] CommandStatus ApplyCompletedText(const CommandRecord& a_command, const std::string& a_text) noexcept
{
    const auto& payload = a_command.Payload.ApplyGameplayTextChunk;
    const auto action = static_cast<GameplayAction>(payload.Action);
    if (action == GameplayAction::Dialogue && payload.TargetHandle.Value == 0) {
        RE::SendHUDMessage::ShowHUDMessage(a_text.c_str(), nullptr, false);
        return CommandStatus::Success;
    }

    if (action == GameplayAction::Subtitle) {
        RE::NiPointer<RE::Actor> actor;
        const auto resolved = ResolveActorForText(a_command, actor);
        return resolved == CommandStatus::Success ?
                   ShowSubtitle(*actor, a_text, payload.AuxiliaryLocalFormId) : resolved;
    }

    if (action == GameplayAction::ScriptAnimation) {
        auto* reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(payload.TargetLocalFormId);
        if (!reference)
            return CommandStatus::MissingForm;
        const auto separator = a_text.find('\n');
        const std::string_view animation = separator == std::string::npos ? std::string_view{} :
                                          std::string_view(a_text).substr(0, separator);
        const std::string_view event = separator == std::string::npos ? std::string_view(a_text) :
                                      std::string_view(a_text).substr(separator + 1);
        if (animation.size() > 127 || event.empty() || event.size() > 127 ||
            animation.find('\0') != std::string_view::npos || event.find('\0') != std::string_view::npos ||
            (separator != std::string::npos && a_text.find('\n', separator + 1) != std::string::npos))
            return CommandStatus::Malformed;
        if (!animation.empty())
            return PlayAnimationAndWait(*reference, animation, event);
        return reference->NotifyAnimationGraph(RE::BSFixedString(event.data())) ?
                   CommandStatus::Success : CommandStatus::EngineRejected;
    }

    RE::NiPointer<RE::Actor> actor;
    const auto resolved = ResolveActorForText(a_command, actor);
    if (resolved != CommandStatus::Success)
        return resolved;

    if (action == GameplayAction::AnimationEvent) {
        if (!IsSupportedLegacyAnimationEvent(a_text))
            return CommandStatus::Unsupported;
        return actor->NotifyAnimationGraph(RE::BSFixedString(a_text.c_str())) ?
                   CommandStatus::Success : CommandStatus::EngineRejected;
    }
    if (action == GameplayAction::Dialogue) {
        actor->StopCurrentDialogue();
        // The generated 37542 alias resolves to an unrelated PlayerCharacter
        // helper. DialogueHooks owns the semantically verified VR routine at
        // RVA 0x5F0E20 and suppresses the local capture hook during replay.
        return PlayDialogueVoice(*actor, a_text);
    }
    if (action == GameplayAction::SetName) {
        auto* npc = actor->GetActorBase();
        if (!npc || !npc->IsDynamicForm() || a_text.empty() || a_text.size() > 127)
            return CommandStatus::Malformed;
        npc->SetFullName(a_text.c_str());
        return CommandStatus::Success;
    }
    return CommandStatus::Unsupported;
}
} // namespace

CommandStatus GameplayTextManager::Execute(const CommandRecord& a_command) noexcept
{
    try {
        if (static_cast<CommandKind>(a_command.Header.Kind) != CommandKind::ApplyGameplayTextChunk ||
            a_command.Header.PayloadSize != kFixedPayloadBytes || a_command.Header.Flags != 0 ||
            a_command.Header.Identity.SequenceId != 0 || a_command.Header.Identity.ActionId == 0)
            return CommandStatus::Malformed;

        const auto& payload = a_command.Payload.ApplyGameplayTextChunk;
        const auto action = static_cast<GameplayAction>(payload.Action);
        if (payload.TextId == 0 || payload.ChunkCount == 0 || payload.ChunkCount > kMaximumGameplayTextChunks ||
            payload.ChunkIndex >= payload.ChunkCount || payload.ByteCount > kGameplayTextBytesPerChunk ||
            payload.Reserved0 != 0 || (action != GameplayAction::Subtitle && payload.AuxiliaryLocalFormId != 0) ||
            !IsActionInDomain(static_cast<GameplayDomain>(payload.Domain), action) ||
            !std::all_of(payload.Utf8Bytes + payload.ByteCount,
                         payload.Utf8Bytes + kGameplayTextBytesPerChunk,
                         [](const char a_value) { return a_value == '\0'; }))
            return CommandStatus::Malformed;

        const auto key = MakeKey(a_command);
        auto found = g_pending.find(key);
        if (found == g_pending.end()) {
            if (g_pending.size() >= kMaximumPendingTexts)
                return CommandStatus::QueueOverflow;
            const auto targetKey = MakeTargetKey(a_command);
            if (const auto accepted = g_lastAcceptedTextActions.find(targetKey);
                accepted != g_lastAcceptedTextActions.end() &&
                a_command.Header.Identity.ActionId <= accepted->second)
                return CommandStatus::StaleEntity;
            found = g_pending.emplace(key, PendingText{}).first;
            found->second.ChunkCount = payload.ChunkCount;
            found->second.InitialCommand = a_command;
        }
        auto& pending = found->second;
        if (pending.ChunkCount != payload.ChunkCount ||
            !HasMatchingTextMetadata(pending.InitialCommand, a_command)) {
            const auto previousActionId = pending.InitialCommand.Header.Identity.ActionId;
            if (a_command.Header.Identity.ActionId <= previousActionId)
                return CommandStatus::StaleEntity;

            // A producer can retry a partially queued semantic text using the
            // same TextId and a new command ActionId. Replace the incomplete
            // transaction before accepting this retry's first observed chunk.
            pending = {};
            pending.ChunkCount = payload.ChunkCount;
            pending.InitialCommand = a_command;
        }
        if (pending.Received.test(payload.ChunkIndex))
            return CommandStatus::StaleEntity;

        std::copy_n(payload.Utf8Bytes, payload.ByteCount, pending.Chunks[payload.ChunkIndex].begin());
        pending.Lengths[payload.ChunkIndex] = payload.ByteCount;
        pending.Received.set(payload.ChunkIndex);
        if (pending.Received.count() != pending.ChunkCount)
            return CommandStatus::Success;

        std::string text;
        for (std::uint16_t index = 0; index < pending.ChunkCount; ++index)
            text.append(pending.Chunks[index].data(), pending.Lengths[index]);
        const auto command = pending.InitialCommand;
        g_pending.erase(found);
        const auto targetKey = MakeTargetKey(command);
        auto& lastAccepted = g_lastAcceptedTextActions[targetKey];
        if (command.Header.Identity.ActionId <= lastAccepted)
            return CommandStatus::StaleEntity;
        lastAccepted = command.Header.Identity.ActionId;
        return ApplyCompletedText(command, text);
    } catch (...) {
        return CommandStatus::EngineRejected;
    }
}

void GameplayTextManager::Reset() noexcept
{
    g_pending.clear();
    g_lastAcceptedTextActions.clear();
}
} // namespace SkyrimTogetherVR::GameplayAdapter
