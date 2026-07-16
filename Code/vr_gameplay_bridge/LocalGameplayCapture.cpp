#include "LocalGameplayCapture.h"

#include "BridgeEndpoint.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture
{
namespace
{
constexpr std::size_t kActorValueCount = 3;
constexpr std::size_t kSkillCount = 18;
constexpr std::size_t kHeadPartCount = static_cast<std::size_t>(RE::BGSHeadPart::HeadPartType::kTotal);
constexpr std::size_t kTintTypeCount = static_cast<std::size_t>(RE::TintMask::Type::kTotal);
constexpr std::size_t kEquipmentSlotCount = 2;
constexpr std::size_t kMaximumWornEquipmentEntries = 64;
// Bound reconciliation without truncating ordinary heavily modded players.
// Live container events remain the primary delta path; this snapshot repairs
// missed events and initial state within one quarter of the event ring.
constexpr std::size_t kMaximumInventoryEntries = 512;
constexpr std::size_t kMaximumNpcActors = 8;
constexpr std::size_t kMaximumNpcCandidates = 128;
constexpr std::size_t kMaximumNpcItems = 64;
constexpr std::size_t kMaximumNpcFactions = 64;
constexpr std::size_t kMaximumObservedNpcs = 64;
constexpr std::size_t kMaximumObjectSnapshotEntries = 512;
constexpr std::size_t kMaximumObjectSnapshots = 512;
constexpr std::size_t kMaximumPlayerNameBytes = 128;
constexpr std::size_t kMaximumPlayerDialogueBytes = 512;
constexpr std::int32_t kMaximumInventoryDelta = 10'000;
constexpr auto kSnapshotInterval = std::chrono::milliseconds{100};
constexpr auto kNpcDiscoveryInterval = std::chrono::milliseconds{1000};
constexpr auto kNpcObservationInterval = std::chrono::milliseconds{100};
constexpr auto kWeatherObservationInterval = std::chrono::milliseconds{1000};
constexpr auto kEquipmentRefreshInterval = std::chrono::seconds{5};
constexpr auto kPackageRefreshInterval = std::chrono::seconds{5};
constexpr auto kQuestSuppressionLifetime = std::chrono::seconds{2};
constexpr auto kExperienceSuppressionLifetime = std::chrono::seconds{2};
constexpr std::size_t kMaximumQuestSuppressions = 32;
constexpr std::uint32_t kMapWeatherFormId = 0x000A6858;
constexpr std::uint32_t kMaximumQuestType = 11;
constexpr std::array<std::uint32_t, 4> kNonSyncableQuestIds{
    0x0002BA16,
    0x020071D0,
    0x0003AC44,
    0x000F2593,
};

constexpr std::array<RE::ActorValue, kActorValueCount> kCapturedActorValues{
    RE::ActorValue::kHealth,
    RE::ActorValue::kMagicka,
    RE::ActorValue::kStamina,
};

struct WornEquipmentEntry
{
    std::uint32_t FormId{};
    std::int32_t Count{};
    bool Worn{};
    bool WornLeft{};
    bool Weapon{};
    bool Ammo{};

    [[nodiscard]] bool operator==(const WornEquipmentEntry&) const noexcept = default;
};

struct SelectedMagicEquipment
{
    std::uint32_t LeftSpellFormId{};
    std::uint32_t RightSpellFormId{};
    std::uint32_t PowerOrShoutFormId{};

    [[nodiscard]] bool operator==(const SelectedMagicEquipment&) const noexcept = default;
};

struct Snapshot
{
    bool Valid{};
    std::uint32_t RaceFormId{};
    std::int32_t Sex{};
    bool SexCaptured{};
    std::uint32_t WeightBits{};
    bool WeightCaptured{};
    std::uint64_t NameHash{};
    std::array<std::uint32_t, kHeadPartCount> HeadParts{};
    std::array<std::uint32_t, kTintTypeCount> TintAlphaBits{};
    std::array<std::uint32_t, kTintTypeCount> TintColors{};
    std::array<bool, kTintTypeCount> TintCaptured{};
    std::array<std::uint32_t, kActorValueCount> ActorValueBits{};
    std::array<std::uint32_t, kActorValueCount> ActorMaximumBits{};
    std::array<bool, kActorValueCount> ActorValueCaptured{};
    std::array<bool, kActorValueCount> ActorMaximumCaptured{};
    std::array<std::uint32_t, kSkillCount> SkillXpBits{};
    bool SkillsValid{};
    std::uint16_t Level{1};
    bool LevelCaptured{};
    bool Essential{};
    bool EssentialCaptured{};
    bool Dead{};
    bool DeadCaptured{};
    bool WeaponDrawn{};
    bool WeaponDrawnCaptured{};
    std::uint32_t MountFormId{};
    std::uint32_t PackageFormId{};
    std::array<std::uint32_t, kEquipmentSlotCount> EquippedForms{};
    std::vector<WornEquipmentEntry> WornEquipment{};
    SelectedMagicEquipment MagicEquipment{};
    bool EquipmentCaptured{};
    std::map<std::uint32_t, std::int32_t> Inventory;
};

std::recursive_mutex g_captureLock;
std::mutex g_textPublishLock;
Snapshot g_snapshot;
std::atomic_bool g_armed{};
std::atomic<std::uint64_t> g_nextActionId{};
std::atomic<std::uint64_t> g_nextTextId{};
std::atomic<std::uint64_t> g_nextQuestSuppressionToken{};
std::atomic<std::uint64_t> g_nextLockSuppressionToken{};
bool g_initialized{};
bool g_periodicCaptureActive{};
bool g_periodicCaptureFailed{};
bool g_scriptSinksRegistered{};
bool g_animationSinkRegistered{};
RE::PlayerCharacter* g_animationSinkPlayer{};
std::uint32_t g_lastObjectCellFormId{};
std::chrono::steady_clock::time_point g_lastSnapshotAt{};
std::chrono::steady_clock::time_point g_lastNpcDiscoveryAt{};
std::chrono::steady_clock::time_point g_lastNpcObservationAt{};
std::chrono::steady_clock::time_point g_lastWeatherObservationAt{};
std::chrono::steady_clock::time_point g_lastEquipmentPublishedAt{};
std::chrono::steady_clock::time_point g_lastPackagePublishedAt{};
std::uint32_t g_lastNpcDiscoveryCellFormId{};
std::size_t g_npcDiscoveryOffset{};
std::size_t g_npcObservationOffset{};
std::unordered_set<std::uint32_t> g_observedNpcReferences{};
bool g_hasWeatherSnapshot{};
std::uint32_t g_lastWeatherFormId{};

struct WaypointSnapshot
{
    bool Valid{};
    std::uint32_t WorldspaceFormId{};
    RE::NiPoint3 Position{};
};

WaypointSnapshot g_waypointSnapshot{};
const RE::MenuTopicManager::Dialogue* g_lastSelectedDialogue{};
std::string g_lastSelectedDialogueText{};

enum class QuestSuppressionKind : std::uint8_t
{
    StartStop,
    Stage,
};

struct QuestSuppression
{
    QuestSuppressionKind Kind{};
    std::uint32_t QuestFormId{};
    std::uint16_t Stage{};
    bool Started{};
    std::uint64_t Token{};
    std::chrono::steady_clock::time_point ExpiresAt{};
};

std::mutex g_questSuppressionLock;
std::array<QuestSuppression, kMaximumQuestSuppressions> g_questSuppressions{};

struct LockSuppression
{
    std::uint32_t ReferenceFormId{};
    std::uint8_t LockLevel{};
    bool Locked{};
    std::uint64_t Token{};
    std::chrono::steady_clock::time_point ExpiresAt{};
};

std::mutex g_lockSuppressionLock;
std::array<LockSuppression, kMaximumQuestSuppressions> g_lockSuppressions{};
std::array<std::chrono::steady_clock::time_point, kSkillCount> g_experienceSuppressions{};

[[nodiscard]] bool IsFinite(const float a_value) noexcept
{
    return std::isfinite(a_value);
}

[[nodiscard]] bool IsValidFormId(const RE::FormID a_formId) noexcept
{
    return a_formId != 0 && a_formId != std::numeric_limits<RE::FormID>::max() && RE::TESForm::LookupByID(a_formId) != nullptr;
}

[[nodiscard]] bool IsBoundedInventoryDelta(const std::int32_t a_delta) noexcept
{
    return a_delta != 0 && a_delta >= -kMaximumInventoryDelta && a_delta <= kMaximumInventoryDelta;
}

[[nodiscard]] bool IsSyncableQuest(const RE::TESQuest& a_quest) noexcept
{
    const auto formId = a_quest.GetFormID();
    const auto type = static_cast<std::uint32_t>(a_quest.GetType());
    return IsValidFormId(formId) && type <= kMaximumQuestType && a_quest.executedStages &&
           !a_quest.executedStages->empty() &&
           std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), formId) == kNonSyncableQuestIds.end();
}

[[nodiscard]] QuestSuppressionToken ArmQuestSuppression(
    const QuestSuppressionKind a_kind,
    const std::uint32_t a_questFormId,
    const std::uint16_t a_stage,
    const bool a_started) noexcept
{
    if (a_questFormId == 0)
        return 0;

    const auto now = std::chrono::steady_clock::now();
    const auto token = g_nextQuestSuppressionToken.fetch_add(1, std::memory_order_relaxed) + 1;
    if (token == 0)
        return 0;

    const std::scoped_lock lock{g_questSuppressionLock};
    QuestSuppression* slot{};
    for (auto& suppression : g_questSuppressions) {
        if (suppression.Token != 0 && suppression.ExpiresAt <= now)
            suppression = {};
        if (!slot && suppression.Token == 0)
            slot = &suppression;
    }
    if (!slot)
        return 0;

    *slot = {
        .Kind = a_kind,
        .QuestFormId = a_questFormId,
        .Stage = a_stage,
        .Started = a_started,
        .Token = token,
        .ExpiresAt = now + kQuestSuppressionLifetime,
    };
    return token;
}

[[nodiscard]] bool ConsumeQuestSuppression(
    const QuestSuppressionKind a_kind,
    const std::uint32_t a_questFormId,
    const std::uint16_t a_stage,
    const bool a_started) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    const std::scoped_lock lock{g_questSuppressionLock};
    for (auto& suppression : g_questSuppressions) {
        if (suppression.Token != 0 && suppression.ExpiresAt <= now) {
            suppression = {};
            continue;
        }
        if (suppression.Token != 0 && suppression.Kind == a_kind && suppression.QuestFormId == a_questFormId &&
            suppression.Stage == a_stage && suppression.Started == a_started) {
            suppression = {};
            return true;
        }
    }
    return false;
}

void ClearQuestSuppressions() noexcept
{
    const std::scoped_lock lock{g_questSuppressionLock};
    g_questSuppressions = {};
}

[[nodiscard]] bool ConsumeLockSuppression(
    const std::uint32_t a_referenceFormId, const bool a_locked, const std::uint8_t a_lockLevel) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    const std::scoped_lock lock{g_lockSuppressionLock};
    for (auto& suppression : g_lockSuppressions) {
        if (suppression.Token != 0 && suppression.ExpiresAt <= now) {
            suppression = {};
            continue;
        }
        if (suppression.Token != 0 && suppression.ReferenceFormId == a_referenceFormId &&
            suppression.Locked == a_locked && suppression.LockLevel == a_lockLevel) {
            suppression = {};
            return true;
        }
    }
    return false;
}

void ClearLockSuppressions() noexcept
{
    const std::scoped_lock lock{g_lockSuppressionLock};
    g_lockSuppressions = {};
}

[[nodiscard]] std::uint64_t HashBoundedUtf8(const char* a_text) noexcept
{
    if (!a_text)
        return 0;

    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t index = 0; index < kMaximumPlayerNameBytes && a_text[index] != '\0'; ++index) {
        hash ^= static_cast<std::uint8_t>(a_text[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] bool CanPublish(const GameplayDomain a_domain) noexcept
{
    if (!g_armed.load(std::memory_order_acquire))
        return false;
    auto& endpoint = BridgeEndpoint::Get();
    return endpoint.IsOperational() &&
           HasCapability(endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire), CapabilityForDomain(a_domain));
}

void RecordPeriodicPublication(const bool a_accepted) noexcept
{
    if (g_periodicCaptureActive && !g_snapshot.Valid && !a_accepted)
        g_periodicCaptureFailed = true;
}

[[nodiscard]] bool PreparePlayerPayload(GameplayActionPayload& a_payload, const RE::PlayerCharacter& a_player) noexcept
{
    const auto formId = a_player.GetFormID();
    if (!IsValidFormId(formId)) {
        RecordPeriodicPublication(false);
        return false;
    }

    a_payload.TargetHandle = kLocalPlayerHandle;
    a_payload.TargetLocalFormId = formId;
    return true;
}

[[nodiscard]] bool Publish(
    const GameplayDomain a_domain,
    const GameplayAction a_action,
    const GameplayActionPayload& a_payload) noexcept
{
    const std::scoped_lock lock{g_captureLock};
    const bool objectSnapshot = a_domain == GameplayDomain::Object &&
        a_action >= GameplayAction::ObjectSnapshotBegin && a_action <= GameplayAction::ObjectSnapshotEnd;
    if (!IsActionInDomain(a_domain, a_action) || !CanPublish(a_domain) ||
        ((!objectSnapshot && a_payload.TargetHandle.Value != kLocalPlayerHandle.Value) ||
         (objectSnapshot && (a_payload.TargetHandle.Value != 0 || a_payload.TargetLocalFormId == 0))) ||
        a_payload.SecondaryHandle.Value != 0 ||
        !IsFinite(a_payload.ScalarA) || !IsFinite(a_payload.ScalarB) || !IsFinite(a_payload.ScalarC) || !IsFinite(a_payload.ScalarD))
    {
        RecordPeriodicPublication(false);
        return false;
    }

    auto& endpoint = BridgeEndpoint::Get();
    EventRecord record{};
    record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
    record.Header.PayloadSize = kFixedPayloadBytes;
    record.Header.Identity = endpoint.SnapshotIdentity(0);
    record.Header.Identity.ActionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    record.Payload.LocalGameplayAction = a_payload;
    record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(a_domain);
    record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
    const auto accepted = endpoint.TryPushEvent(record);
    RecordPeriodicPublication(accepted);
    return accepted;
}

struct NpcInventoryEntry
{
    std::uint32_t FormId{};
    std::int32_t Count{};
    bool QuestItem{};
};

struct NpcFactionEntry
{
    std::uint32_t FormId{};
    std::int8_t Rank{};
};

struct NpcSnapshot
{
    std::uint32_t ReferenceFormId{};
    std::uint32_t BaseFormId{};
    std::uint32_t CellFormId{};
    std::uint32_t WorldspaceFormId{};
    std::uint32_t PackageFormId{};
    RE::NiPoint3 Position{};
    float ZRotation{};
    std::array<float, kActorValueCount> Values{};
    std::array<float, kActorValueCount> Maximums{};
    std::vector<NpcInventoryEntry> Inventory{};
    std::vector<NpcFactionEntry> Factions{};
    bool Dead{};
    bool WeaponDrawn{};
};

[[nodiscard]] bool CaptureNpcSnapshot(RE::Actor& a_actor, NpcSnapshot& ar_snapshot) noexcept
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (&a_actor == player || !a_actor.Is3DLoaded())
        return false;

    const auto referenceId = a_actor.GetFormID();
    const auto* base = a_actor.GetActorBase();
    const auto* cell = a_actor.GetParentCell();
    const auto* worldspace = a_actor.GetWorldspace();
    if (!base || !cell || !IsValidFormId(referenceId) || !IsValidFormId(base->GetFormID()) ||
        !IsValidFormId(cell->GetFormID()) || (worldspace && !IsValidFormId(worldspace->GetFormID())))
        return false;

    const auto position = a_actor.GetPosition();
    const auto angle = a_actor.GetAngle();
    if (!IsFinite(position.x) || !IsFinite(position.y) || !IsFinite(position.z) || !IsFinite(angle.z))
        return false;

    ar_snapshot = {};
    ar_snapshot.ReferenceFormId = referenceId;
    ar_snapshot.BaseFormId = base->GetFormID();
    ar_snapshot.CellFormId = cell->GetFormID();
    ar_snapshot.WorldspaceFormId = worldspace ? worldspace->GetFormID() : 0;
    if (const auto* package = a_actor.GetCurrentPackage()) {
        if (!IsValidFormId(package->GetFormID()))
            return false;
        ar_snapshot.PackageFormId = package->GetFormID();
    }
    ar_snapshot.Position = position;
    ar_snapshot.ZRotation = angle.z;
    ar_snapshot.Dead = a_actor.IsDead();
    if (const auto* state = a_actor.AsActorState())
        ar_snapshot.WeaponDrawn = state->IsWeaponDrawn();

    for (std::size_t index = 0; index < kCapturedActorValues.size(); ++index) {
        const auto value = a_actor.GetActorValue(kCapturedActorValues[index]);
        const auto maximum = a_actor.GetActorValueMax(kCapturedActorValues[index]);
        if (!IsFinite(value) || !IsFinite(maximum))
            return false;
        ar_snapshot.Values[index] = value;
        ar_snapshot.Maximums[index] = maximum;
    }

    for (const auto& [object, data] : a_actor.GetInventory()) {
        const auto count = data.first;
        const auto& entry = data.second;
        if (!object || !IsValidFormId(object->GetFormID()) || count <= 0 || count > kMaximumInventoryDelta ||
            ar_snapshot.Inventory.size() >= kMaximumNpcItems)
            return false;
        ar_snapshot.Inventory.push_back({object->GetFormID(), count, entry && entry->IsQuestObject()});
    }

    bool factionsComplete = true;
    a_actor.VisitFactions([&ar_snapshot, &factionsComplete](RE::TESFaction* a_faction, const std::int8_t a_rank) {
        if (!a_faction || !IsValidFormId(a_faction->GetFormID()) ||
            ar_snapshot.Factions.size() >= kMaximumNpcFactions) {
            factionsComplete = false;
            return true;
        }
        ar_snapshot.Factions.push_back({a_faction->GetFormID(), a_rank});
        return false;
    });
    return factionsComplete;
}

[[nodiscard]] bool PublishNpcSnapshot(const NpcSnapshot& a_snapshot) noexcept
{
    const std::scoped_lock lock{g_captureLock};
    if (!CanPublish(GameplayDomain::NpcOwnership))
        return false;
    const auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    std::vector<EventRecord> records;
    records.reserve(2 + kCapturedActorValues.size() + a_snapshot.Inventory.size() + a_snapshot.Factions.size());
    auto& endpoint = BridgeEndpoint::Get();
    const auto append = [&](const GameplayAction a_action, const GameplayActionPayload& a_payload) {
        auto& record = records.emplace_back();
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = actionId;
        record.Payload.LocalGameplayAction = a_payload;
        record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::NpcOwnership);
        record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
    };
    GameplayActionPayload begin{};
    begin.TargetHandle = kLocalPlayerHandle;
    begin.TargetLocalFormId = a_snapshot.ReferenceFormId;
    begin.LocalFormIdA = a_snapshot.BaseFormId;
    begin.LocalFormIdB = a_snapshot.CellFormId;
    begin.LocalFormIdC = a_snapshot.WorldspaceFormId;
    begin.LocalFormIdD = a_snapshot.PackageFormId;
    begin.ScalarA = a_snapshot.Position.x;
    begin.ScalarB = a_snapshot.Position.y;
    begin.ScalarC = a_snapshot.Position.z;
    begin.ScalarD = a_snapshot.ZRotation;
    begin.ActionFlags = (a_snapshot.Dead ? kNpcSnapshotDead : 0u) |
                        (a_snapshot.WeaponDrawn ? kNpcSnapshotWeaponDrawn : 0u);
    append(GameplayAction::NpcSnapshotBegin, begin);

    for (std::size_t index = 0; index < kCapturedActorValues.size(); ++index) {
        GameplayActionPayload value{};
        value.TargetHandle = kLocalPlayerHandle;
        value.TargetLocalFormId = a_snapshot.ReferenceFormId;
        value.LocalFormIdA = static_cast<std::uint32_t>(kCapturedActorValues[index]);
        value.ScalarA = a_snapshot.Values[index];
        value.ScalarB = a_snapshot.Maximums[index];
        append(GameplayAction::NpcSnapshotValue, value);
    }
    for (const auto& entry : a_snapshot.Inventory) {
        GameplayActionPayload item{};
        item.TargetHandle = kLocalPlayerHandle;
        item.TargetLocalFormId = a_snapshot.ReferenceFormId;
        item.LocalFormIdA = entry.FormId;
        item.ValueA = entry.Count;
        item.ActionFlags = entry.QuestItem ? kInventoryQuestItem : 0u;
        append(GameplayAction::NpcSnapshotItem, item);
    }
    for (const auto& entry : a_snapshot.Factions) {
        GameplayActionPayload faction{};
        faction.TargetHandle = kLocalPlayerHandle;
        faction.TargetLocalFormId = a_snapshot.ReferenceFormId;
        faction.LocalFormIdA = entry.FormId;
        faction.ValueA = entry.Rank;
        append(GameplayAction::NpcSnapshotFaction, faction);
    }
    GameplayActionPayload end{};
    end.TargetHandle = kLocalPlayerHandle;
    end.TargetLocalFormId = a_snapshot.ReferenceFormId;
    append(GameplayAction::NpcSnapshotEnd, end);
    return endpoint.TryPushEvents(records.data(), records.size());
}

void CaptureNpcDiscovery() noexcept
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* processLists = RE::ProcessLists::GetSingleton();
    const auto cellId = player && player->GetParentCell() ? player->GetParentCell()->GetFormID() : 0;
    const auto now = std::chrono::steady_clock::now();
    if (!player || !processLists || cellId == 0 ||
        (cellId == g_lastNpcDiscoveryCellFormId && g_lastNpcDiscoveryAt != std::chrono::steady_clock::time_point{} &&
         now - g_lastNpcDiscoveryAt < kNpcDiscoveryInterval))
        return;

    if (cellId != g_lastNpcDiscoveryCellFormId)
        g_npcDiscoveryOffset = 0;
    g_lastNpcDiscoveryAt = now;
    g_lastNpcDiscoveryCellFormId = cellId;
    std::vector<RE::Actor*> candidates;
    candidates.reserve(kMaximumNpcCandidates);
    processLists->ForEachHighActor([&candidates](RE::Actor* a_actor) {
        if (a_actor && !g_observedNpcReferences.contains(a_actor->GetFormID()))
            candidates.push_back(a_actor);
        return candidates.size() >= kMaximumNpcCandidates ? RE::BSContainer::ForEachResult::kStop :
                                                            RE::BSContainer::ForEachResult::kContinue;
    });
    if (candidates.empty()) {
        g_npcDiscoveryOffset = 0;
        return;
    }

    const auto start = g_npcDiscoveryOffset % candidates.size();
    const auto candidateCount = std::min(kMaximumNpcActors, candidates.size());
    bool accepted = true;
    for (std::size_t index = 0; index < candidateCount; ++index) {
        auto* actor = candidates[(start + index) % candidates.size()];
        NpcSnapshot snapshot{};
        if (actor && CaptureNpcSnapshot(*actor, snapshot) && !PublishNpcSnapshot(snapshot)) {
            accepted = false;
            break;
        }
    }
    if (accepted)
        g_npcDiscoveryOffset = (start + candidateCount) % candidates.size();
}

void CaptureObservedNpcs() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (g_lastNpcObservationAt != std::chrono::steady_clock::time_point{} && now - g_lastNpcObservationAt < kNpcObservationInterval)
        return;
    g_lastNpcObservationAt = now;
    std::vector<std::uint32_t> references(g_observedNpcReferences.begin(), g_observedNpcReferences.end());
    if (references.empty()) {
        g_npcObservationOffset = 0;
        return;
    }
    std::sort(references.begin(), references.end());
    const auto start = g_npcObservationOffset % references.size();
    const auto referenceCount = std::min(kMaximumNpcActors, references.size());
    bool accepted = true;
    for (std::size_t index = 0; index < referenceCount; ++index) {
        const auto referenceId = references[(start + index) % references.size()];
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(referenceId);
        NpcSnapshot snapshot{};
        if (actor && CaptureNpcSnapshot(*actor, snapshot) && !PublishNpcSnapshot(snapshot)) {
            accepted = false;
            break;
        }
    }
    if (accepted)
        g_npcObservationOffset = (start + referenceCount) % references.size();
}

[[nodiscard]] bool PublishText(
    const GameplayDomain a_domain,
    const GameplayAction a_action,
    const GameplayActionPayload& a_target,
    const char* a_text,
    const std::size_t a_maximumBytes = kMaximumPlayerNameBytes) noexcept
{
    const std::scoped_lock captureLock{g_captureLock};
    if (!a_text || !CanPublish(a_domain)) {
        RecordPeriodicPublication(false);
        return false;
    }
    const std::scoped_lock publishLock{g_textPublishLock};
    const auto maximumBytes = std::min<std::size_t>(
        a_maximumBytes, static_cast<std::size_t>(kGameplayTextBytesPerChunk) * kMaximumGameplayTextChunks);
    std::size_t byteCount{};
    while (byteCount <= maximumBytes && a_text[byteCount] != '\0')
        ++byteCount;
    if (byteCount == 0 || byteCount > maximumBytes) {
        RecordPeriodicPublication(false);
        return false;
    }

    const auto chunkCount = static_cast<std::uint16_t>(
        (byteCount + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
    const auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto textId = g_nextTextId.fetch_add(1, std::memory_order_relaxed) + 1;
    auto& endpoint = BridgeEndpoint::Get();
    std::vector<EventRecord> records;
    records.reserve(chunkCount);
    for (std::uint16_t index = 0; index < chunkCount; ++index) {
        auto& record = records.emplace_back();
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayTextChunk);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = actionId;
        auto& payload = record.Payload.LocalGameplayTextChunk;
        payload.TargetHandle = a_target.TargetHandle;
        payload.TargetLocalFormId = a_target.TargetLocalFormId;
        payload.Domain = static_cast<std::uint16_t>(a_domain);
        payload.Action = static_cast<std::uint16_t>(a_action);
        payload.TextId = textId;
        payload.ChunkIndex = index;
        payload.ChunkCount = chunkCount;
        const auto offset = static_cast<std::size_t>(index) * kGameplayTextBytesPerChunk;
        payload.ByteCount = static_cast<std::uint16_t>(
            std::min<std::size_t>(kGameplayTextBytesPerChunk, byteCount - offset));
        std::memcpy(payload.Utf8Bytes, a_text + offset, payload.ByteCount);
    }
    const auto accepted = endpoint.TryPushEvents(records.data(), records.size());
    RecordPeriodicPublication(accepted);
    return accepted;
}

[[nodiscard]] bool CaptureWornEquipment(
    const RE::PlayerCharacter& a_player, std::vector<WornEquipmentEntry>& ar_entries) noexcept
{
    ar_entries.clear();
    for (const auto& [object, data] : a_player.GetInventory()) {
        const auto count = data.first;
        const auto& entry = data.second;
        if (!entry || !entry->IsWorn())
            continue;
        if (!object || !IsValidFormId(object->GetFormID()) || count <= 0 || count > kMaximumInventoryDelta ||
            ar_entries.size() >= kMaximumWornEquipmentEntries)
            return false;
        const auto formType = object->GetFormType();
        ar_entries.push_back({object->GetFormID(), count, entry->IsWorn(false), entry->IsWorn(true),
                              formType == RE::FormType::Weapon, formType == RE::FormType::Ammo});
    }
    std::sort(ar_entries.begin(), ar_entries.end(), [](const auto& a_left, const auto& a_right) {
        return a_left.FormId < a_right.FormId;
    });
    return true;
}

[[nodiscard]] std::uint32_t GetSelectedMagicFormId(const RE::TESForm* ap_form) noexcept
{
    if (!ap_form)
        return 0;

    const auto formId = ap_form->GetFormID();
    return IsValidFormId(formId) ? formId : 0;
}

[[nodiscard]] SelectedMagicEquipment CaptureSelectedMagicEquipment(const RE::PlayerCharacter& a_player) noexcept
{
    // CommonLib's runtime accessor is the typed VR-safe source for the player
    // selection state; do not infer it from inventory or mutate actor data.
    const auto& runtime = a_player.GetActorRuntimeData();
    return {
        GetSelectedMagicFormId(runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]),
        GetSelectedMagicFormId(runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand]),
        GetSelectedMagicFormId(runtime.selectedPower),
    };
}

[[nodiscard]] bool PublishEquipmentSnapshot(
    const RE::PlayerCharacter& a_player, const std::vector<WornEquipmentEntry>& ac_entries,
    const SelectedMagicEquipment& ac_magicEquipment) noexcept
{
    const std::scoped_lock lock{g_captureLock};
    if (ac_entries.size() > kMaximumWornEquipmentEntries || !CanPublish(GameplayDomain::Equipment)) {
        RecordPeriodicPublication(false);
        return false;
    }

    GameplayActionPayload target{};
    if (!PreparePlayerPayload(target, a_player)) {
        RecordPeriodicPublication(false);
        return false;
    }

    const auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    auto& endpoint = BridgeEndpoint::Get();
    std::vector<EventRecord> records;
    records.reserve(ac_entries.size() + 2);
    const auto append = [&](const GameplayAction a_action, const GameplayActionPayload& ac_payload) {
        auto& record = records.emplace_back();
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = actionId;
        record.Payload.LocalGameplayAction = ac_payload;
        record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::Equipment);
        record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
    };

    auto begin = target;
    // ActionId is the transaction generation; ValueA requires the receiver to
    // accept the snapshot only when every bounded item has arrived. LocalFormId
    // A-C are otherwise unused by this action and carry the atomic magic state.
    begin.LocalFormIdA = ac_magicEquipment.LeftSpellFormId;
    begin.LocalFormIdB = ac_magicEquipment.RightSpellFormId;
    begin.LocalFormIdC = ac_magicEquipment.PowerOrShoutFormId;
    begin.ValueA = static_cast<std::int32_t>(ac_entries.size());
    append(GameplayAction::EquipmentSnapshotBegin, begin);
    for (const auto& entry : ac_entries) {
        auto item = target;
        item.LocalFormIdA = entry.FormId;
        item.ValueA = entry.Count;
        item.ActionFlags = (entry.Worn ? kEquipmentSnapshotWorn : 0u) |
                           (entry.WornLeft ? kEquipmentSnapshotWornLeft : 0u) |
                           (entry.Weapon ? kEquipmentSnapshotWeapon : 0u) |
                           (entry.Ammo ? kEquipmentSnapshotAmmo : 0u);
        append(GameplayAction::EquipmentSnapshotItem, item);
    }
    append(GameplayAction::EquipmentSnapshotEnd, target);
    const auto accepted = endpoint.TryPushEvents(records.data(), records.size());
    RecordPeriodicPublication(accepted);
    return accepted;
}

bool PublishInventoryDelta(
    const RE::PlayerCharacter& a_player, const RE::FormID a_formId, const std::int32_t a_delta,
    const bool a_drop = false) noexcept
{
    if (!IsValidFormId(a_formId) || !IsBoundedInventoryDelta(a_delta))
        return false;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return false;
    payload.LocalFormIdA = a_formId;
    payload.ValueA = a_delta;
    payload.ActionFlags = a_drop ? kInventoryDrop : 0u;
    return Publish(GameplayDomain::Inventory, GameplayAction::InventoryDelta, payload);
}

[[nodiscard]] bool PublishDeathState(const RE::PlayerCharacter& a_player, const bool a_dead) noexcept
{
    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return false;
    payload.ValueA = a_dead ? 1 : 0;
    return Publish(GameplayDomain::ActorState, GameplayAction::SetDeathState, payload);
}

[[nodiscard]] bool PublishDrawState(const RE::PlayerCharacter& a_player, const bool a_drawn) noexcept
{
    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return false;
    payload.ValueA = a_drawn ? 1 : 0;
    return Publish(GameplayDomain::Animation, GameplayAction::DrawWeapon, payload);
}

[[nodiscard]] bool PublishMount(const RE::PlayerCharacter& a_player, const RE::FormID a_mountFormId) noexcept
{
    if (a_mountFormId != 0 && !IsValidFormId(a_mountFormId))
        return false;
    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return false;
    payload.LocalFormIdA = a_mountFormId;
    return Publish(GameplayDomain::ActorState, GameplayAction::Mount, payload);
}

[[nodiscard]] bool PublishActorValue(
    const RE::PlayerCharacter& a_player,
    const GameplayAction a_action,
    const RE::ActorValue a_actorValue,
    const float a_value) noexcept
{
    if (!IsFinite(a_value))
        return false;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return false;
    payload.LocalFormIdA = static_cast<std::uint32_t>(a_actorValue);
    payload.ScalarA = a_value;
    return Publish(GameplayDomain::ActorState, a_action, payload);
}

[[nodiscard]] bool PublishActorMetadata(
    const RE::PlayerCharacter& a_player, const GameplayAction a_action, const std::int32_t a_value) noexcept
{
    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return false;
    payload.ValueA = a_value;
    return Publish(GameplayDomain::ActorState, a_action, payload);
}

[[nodiscard]] std::uint32_t FixedAnimationEventId(const RE::BSFixedString& a_tag) noexcept
{
    const auto* text = a_tag.data();
    if (!text)
        return 0;

    const std::string_view tag{text};
    if (tag == "IdleForceDefaultState")
        return 1;
    if (tag == "IdleReturnToDefault")
        return 2;
    if (tag == "ReturnDefaultState")
        return 3;
    if (tag == "ReturnToDefault")
        return 4;
    if (tag == "ForceFurnExit")
        return 5;
    if (tag == "IdleStop")
        return 6;
    if (tag == "IdleStopInstant")
        return 7;
    if (tag == "GetUpBegin")
        return 8;
    if (tag == "JumpUp")
        return 9;
    if (tag == "JumpDown")
        return 10;
    if (tag == "JumpLand")
        return 11;
    if (tag == "SneakStart")
        return 12;
    if (tag == "SneakStop")
        return 13;
    if (tag == "SprintStart")
        return 14;
    if (tag == "SprintStop")
        return 15;
    if (tag == "Ragdoll")
        return 16;
    if (tag == "GetUpEnd")
        return 17;
    if (tag == "ChairEnter")
        return 18;
    if (tag == "ChairExit")
        return 19;
    if (tag == "HorseEnter")
        return 20;
    if (tag == "HorseExit")
        return 21;
    if (tag == "weaponDraw")
        return 22;
    if (tag == "weaponSheathe")
        return 23;
    return 0;
}

void OnAnimationEvent(const RE::BSAnimationGraphEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || a_event.holder != player)
            return;
        const auto eventId = FixedAnimationEventId(a_event.tag);
        if (eventId == 0)
            return;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, *player))
            return;
        payload.LocalFormIdA = eventId;
        Publish(GameplayDomain::Animation, GameplayAction::AnimationEvent, payload);
    } catch (...) {
    }
}

void OnActivateEvent(const RE::TESActivateEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* activated = a_event.objectActivated.get();
        if (!player || a_event.actionRef.get() != player || !activated || !IsValidFormId(activated->GetFormID()))
            return;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, *player))
            return;
        auto* base = activated->GetBaseObject();
        auto* cell = activated->GetParentCell();
        auto* worldspace = activated->GetWorldspace();
        if (!base || !cell || !IsValidFormId(cell->GetFormID()))
            return;
        payload.LocalFormIdA = activated->GetFormID();
        payload.LocalFormIdB = cell->GetFormID();
        payload.LocalFormIdC = worldspace ? worldspace->GetFormID() : 0;
        payload.ValueA = static_cast<std::int32_t>(base->GetFormType());
        payload.ValueB = static_cast<std::int32_t>(RE::BGSOpenCloseForm::GetOpenState(activated));
        const auto position = activated->GetPosition();
        payload.ScalarA = position.x;
        payload.ScalarB = position.y;
        payload.ScalarC = position.z;
        Publish(GameplayDomain::Object, GameplayAction::Activate, payload);
    } catch (...) {
    }
}

void OnContainerChangedEvent(const RE::TESContainerChangedEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !IsBoundedInventoryDelta(a_event.itemCount) || !IsValidFormId(a_event.baseObj))
            return;

        const auto playerFormId = player->GetFormID();
        if (!IsValidFormId(playerFormId) || (a_event.oldContainer != playerFormId && a_event.newContainer != playerFormId))
            return;

        const auto delta = a_event.newContainer == playerFormId ? a_event.itemCount : -a_event.itemCount;
        PublishInventoryDelta(*player, a_event.baseObj, delta,
                              a_event.oldContainer == playerFormId && a_event.newContainer == 0);
    } catch (...) {
    }
}

void OnEquipEvent(const RE::TESEquipEvent& a_event) noexcept
{
    // Full typed inventory snapshots own local equipment replication. Do not
    // emit a second bare TESEquip event with less authoritative state.
    static_cast<void>(a_event);
}

void OnLockChangedEvent(const RE::TESLockChangedEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* reference = a_event.lockedObject.get();
        if (!player || !reference || !IsValidFormId(reference->GetFormID()))
            return;

        auto* cell = reference->GetParentCell();
        if (!cell || !IsValidFormId(cell->GetFormID()))
            return;

        const bool isLocked = reference->IsLocked();
        std::int32_t lockLevel{};
        if (isLocked) {
            lockLevel = static_cast<std::int32_t>(reference->GetLockLevel());
            if (lockLevel < static_cast<std::int32_t>(RE::LOCK_LEVEL::kVeryEasy) ||
                lockLevel > static_cast<std::int32_t>(RE::LOCK_LEVEL::kRequiresKey))
                return;
        }
        if (ConsumeLockSuppression(reference->GetFormID(), isLocked, static_cast<std::uint8_t>(lockLevel)))
            return;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, *player))
            return;
        payload.LocalFormIdA = reference->GetFormID();
        payload.LocalFormIdB = cell->GetFormID();
        payload.ValueA = isLocked ? 1 : 0;
        payload.ValueB = lockLevel;
        Publish(GameplayDomain::Object, GameplayAction::SetLockState, payload);
    } catch (...) {
    }
}

void OnDeathEvent(const RE::TESDeathEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && a_event.actorDying.get() == player)
            static_cast<void>(PublishDeathState(*player, a_event.dead));
    } catch (...) {
    }
}

void OnCombatEvent(const RE::TESCombatEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || a_event.actor.get() != player)
            return;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, *player))
            return;
        if (a_event.newState != RE::ACTOR_COMBAT_STATE::kNone) {
            const auto* target = a_event.targetActor.get();
            if (!target || !IsValidFormId(target->GetFormID()) || !RE::TESForm::LookupByID<RE::Actor>(target->GetFormID()))
                return;
            payload.LocalFormIdA = target->GetFormID();
        }
        Publish(GameplayDomain::Combat, GameplayAction::SetCombatTarget, payload);
    } catch (...) {
    }
}

void OnHitEvent(const RE::TESHitEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* target = a_event.target.get();
        if (!player || a_event.cause.get() != player || !target || !IsValidFormId(target->GetFormID()) ||
            !RE::TESForm::LookupByID<RE::Actor>(target->GetFormID()))
            return;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, *player))
            return;
        payload.LocalFormIdA = target->GetFormID();
        if (a_event.source != 0 && RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_event.source))
            payload.LocalFormIdB = a_event.source;
        Publish(GameplayDomain::Combat, GameplayAction::MeleeHit, payload);
    } catch (...) {
    }
}

// TESMagicEffectApplyEvent exposes an MGEF form, while ApplyMagicEffect's
// fixed payload requires a MagicItem and effect-list index. TESPlayerBowShot
// similarly omits the BGSProjectile form required by LaunchProjectile. The
// sinks intentionally validate their local-player ownership but cannot emit a
// fabricated payload under this ABI.
void OnMagicEffectApplyEvent(const RE::TESMagicEffectApplyEvent& a_event) noexcept
{
    const auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event.caster.get() != player || !IsValidFormId(a_event.magicEffect))
        return;
}

void OnPlayerBowShotEvent(const RE::TESPlayerBowShotEvent& a_event) noexcept
{
    if (!IsValidFormId(a_event.weapon) || !IsValidFormId(a_event.ammo) || !IsFinite(a_event.shotPower))
        return;
}

void OnQuestStartStopEvent(const RE::TESQuestStartStopEvent& a_event) noexcept
{
    if (ConsumeQuestSuppression(QuestSuppressionKind::StartStop, a_event.formID, 0, a_event.started) || a_event.failed)
        return;

    auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_event.formID);
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!quest || !player || !IsSyncableQuest(*quest))
        return;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, *player))
        return;
    payload.LocalFormIdA = quest->GetFormID();
    payload.ValueA = quest->GetCurrentStageID();
    payload.ValueB = a_event.started ? 1 : 2;
    payload.ActionFlags = static_cast<std::uint32_t>(quest->GetType());
    Publish(GameplayDomain::Quest, GameplayAction::SetQuestState, payload);
}

void OnQuestStageEvent(const RE::TESQuestStageEvent& a_event) noexcept
{
    if (ConsumeQuestSuppression(QuestSuppressionKind::Stage, a_event.formID, a_event.stage, false))
        return;

    auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_event.formID);
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!quest || !player || !IsSyncableQuest(*quest))
        return;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, *player))
        return;
    payload.LocalFormIdA = quest->GetFormID();
    payload.ValueA = a_event.stage;
    payload.ValueB = 0;
    payload.ActionFlags = static_cast<std::uint32_t>(quest->GetType());
    Publish(GameplayDomain::Quest, GameplayAction::SetQuestStage, payload);
}

template <class Event, void (*Handler)(const Event&) noexcept>
class LocalSink final : public RE::BSTEventSink<Event>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const Event* a_event, RE::BSTEventSource<Event>*) override
    {
        if (a_event) {
            const std::scoped_lock lock{g_captureLock};
            if (g_armed.load(std::memory_order_acquire))
                Handler(*a_event);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

LocalSink<RE::BSAnimationGraphEvent, OnAnimationEvent> g_animationSink;
LocalSink<RE::TESActivateEvent, OnActivateEvent> g_activateSink;
LocalSink<RE::TESContainerChangedEvent, OnContainerChangedEvent> g_containerChangedSink;
LocalSink<RE::TESEquipEvent, OnEquipEvent> g_equipSink;
LocalSink<RE::TESLockChangedEvent, OnLockChangedEvent> g_lockChangedSink;
LocalSink<RE::TESDeathEvent, OnDeathEvent> g_deathSink;
LocalSink<RE::TESCombatEvent, OnCombatEvent> g_combatSink;
LocalSink<RE::TESHitEvent, OnHitEvent> g_hitSink;
LocalSink<RE::TESMagicEffectApplyEvent, OnMagicEffectApplyEvent> g_magicEffectSink;
LocalSink<RE::TESPlayerBowShotEvent, OnPlayerBowShotEvent> g_playerBowShotSink;
LocalSink<RE::TESQuestStartStopEvent, OnQuestStartStopEvent> g_questStartStopSink;
LocalSink<RE::TESQuestStageEvent, OnQuestStageEvent> g_questStageSink;

void RegisterScriptSinks() noexcept
{
    if (g_scriptSinksRegistered)
        return;
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder)
        return;

    holder->AddEventSink(&g_activateSink);
    holder->AddEventSink(&g_containerChangedSink);
    holder->AddEventSink(&g_equipSink);
    holder->AddEventSink(&g_lockChangedSink);
    holder->AddEventSink(&g_deathSink);
    holder->AddEventSink(&g_combatSink);
    holder->AddEventSink(&g_hitSink);
    // MagicHooks owns exact cast, interrupt, desired-target, and AddTarget
    // production. The script events omit fields required by the original wire
    // and would duplicate the exact hook records.
    holder->AddEventSink(&g_playerBowShotSink);
    holder->AddEventSink(&g_questStartStopSink);
    holder->AddEventSink(&g_questStageSink);
    g_scriptSinksRegistered = true;
}

void RegisterAnimationSink(const RE::PlayerCharacter& a_player) noexcept
{
    if (g_animationSinkRegistered && g_animationSinkPlayer == &a_player)
        return;
    if (g_animationSinkRegistered)
        return;
    if (a_player.AddAnimationGraphEventSink(&g_animationSink)) {
        g_animationSinkPlayer = const_cast<RE::PlayerCharacter*>(&a_player);
        g_animationSinkRegistered = true;
    }
}

void CaptureAppearance(RE::PlayerCharacter& a_player, Snapshot& a_current) noexcept
{
    auto* base = a_player.GetActorBase();
    if (!base)
        return;

    if (const auto* race = a_player.GetRace(); race && IsValidFormId(race->GetFormID())) {
        const auto raceFormId = race->GetFormID();
        if (!g_snapshot.Valid || raceFormId != g_snapshot.RaceFormId) {
            GameplayActionPayload payload{};
            if (PreparePlayerPayload(payload, a_player)) {
                payload.LocalFormIdA = raceFormId;
                if (Publish(GameplayDomain::Appearance, GameplayAction::SetRace, payload))
                    a_current.RaceFormId = raceFormId;
            }
        } else {
            a_current.RaceFormId = raceFormId;
        }
    }

    const auto sex = static_cast<std::int32_t>(base->GetSex());
    if (sex >= 0 && sex <= 1 && (!g_snapshot.SexCaptured || sex != g_snapshot.Sex)) {
        GameplayActionPayload payload{};
        if (PreparePlayerPayload(payload, a_player)) {
            payload.ValueA = sex;
            if (Publish(GameplayDomain::Appearance, GameplayAction::SetSex, payload)) {
                a_current.Sex = sex;
                a_current.SexCaptured = true;
            }
        }
    } else if (sex >= 0 && sex <= 1) {
        a_current.Sex = sex;
    }

    if (IsFinite(base->weight) && base->weight >= 0.0F && base->weight <= 100.0F) {
        const auto weightBits = std::bit_cast<std::uint32_t>(base->weight);
        if (!g_snapshot.WeightCaptured || weightBits != g_snapshot.WeightBits) {
            GameplayActionPayload payload{};
            if (PreparePlayerPayload(payload, a_player)) {
                payload.ScalarA = base->weight;
                if (Publish(GameplayDomain::Appearance, GameplayAction::SetWeight, payload)) {
                    a_current.WeightBits = weightBits;
                    a_current.WeightCaptured = true;
                }
            }
        } else {
            a_current.WeightBits = weightBits;
        }
    }

    const auto* displayName = a_player.GetDisplayFullName();
    const auto nameHash = HashBoundedUtf8(displayName);
    if ((!g_snapshot.Valid || nameHash != g_snapshot.NameHash) && displayName) {
        GameplayActionPayload target{};
        if (PreparePlayerPayload(target, a_player))
            if (PublishText(GameplayDomain::Appearance, GameplayAction::SetName, target, displayName))
                a_current.NameHash = nameHash;
    } else if (displayName) {
        a_current.NameHash = nameHash;
    }

    for (std::size_t index = 0; index < kHeadPartCount; ++index) {
        const auto type = static_cast<RE::BGSHeadPart::HeadPartType>(index);
        const auto* headPart = base->GetCurrentHeadPartByType(type);
        if (!headPart || !IsValidFormId(headPart->GetFormID()))
            continue;
        const auto headPartFormId = headPart->GetFormID();
        if (!g_snapshot.Valid || headPartFormId != g_snapshot.HeadParts[index]) {
            GameplayActionPayload payload{};
            if (PreparePlayerPayload(payload, a_player)) {
                payload.LocalFormIdA = headPartFormId;
                payload.ValueA = static_cast<std::int32_t>(index);
                if (Publish(GameplayDomain::Appearance, GameplayAction::SetHeadPart, payload))
                    a_current.HeadParts[index] = headPartFormId;
            }
        } else {
            a_current.HeadParts[index] = headPartFormId;
        }
    }

    constexpr auto skinTone = static_cast<std::size_t>(RE::TintMask::Type::kSkinTone);
    static_assert(skinTone == kSupportedSkinTintType);
    for (std::size_t index = skinTone; index <= skinTone; ++index) {
        const auto count = a_player.GetNumTints(static_cast<std::uint32_t>(index));
        const auto* tint = count != 0 ? a_player.GetTintMask(static_cast<std::uint32_t>(index), 0) : nullptr;
        if (!tint || !IsFinite(tint->alpha) || tint->alpha < 0.0F || tint->alpha > 1.0F)
            continue;
        const auto alphaBits = std::bit_cast<std::uint32_t>(tint->alpha);
        const auto color = (static_cast<std::uint32_t>(tint->color.red) << 16) |
                           (static_cast<std::uint32_t>(tint->color.green) << 8) |
                           static_cast<std::uint32_t>(tint->color.blue);
        if (!g_snapshot.TintCaptured[index] || alphaBits != g_snapshot.TintAlphaBits[index] || color != g_snapshot.TintColors[index]) {
            GameplayActionPayload payload{};
            if (PreparePlayerPayload(payload, a_player)) {
                payload.LocalFormIdB = color;
                payload.ValueA = static_cast<std::int32_t>(index);
                payload.ScalarA = tint->alpha;
                if (Publish(GameplayDomain::Appearance, GameplayAction::SetTint, payload)) {
                    a_current.TintAlphaBits[index] = alphaBits;
                    a_current.TintColors[index] = color;
                    a_current.TintCaptured[index] = true;
                }
            }
        } else {
            a_current.TintAlphaBits[index] = alphaBits;
            a_current.TintColors[index] = color;
        }
    }
}

void CaptureActorState(RE::PlayerCharacter& a_player, Snapshot& a_current) noexcept
{
    for (std::size_t index = 0; index < kCapturedActorValues.size(); ++index) {
        const auto actorValue = kCapturedActorValues[index];
        const auto value = a_player.GetActorValue(actorValue);
        if (IsFinite(value)) {
            const auto valueBits = std::bit_cast<std::uint32_t>(value);
            if (!g_snapshot.ActorValueCaptured[index]) {
                if (PublishActorValue(a_player, GameplayAction::SetActorValue, actorValue, value))
                {
                    a_current.ActorValueBits[index] = valueBits;
                    a_current.ActorValueCaptured[index] = true;
                }
            } else if (valueBits != g_snapshot.ActorValueBits[index]) {
                bool accepted{};
                if (actorValue == RE::ActorValue::kHealth) {
                    const auto previous = std::bit_cast<float>(g_snapshot.ActorValueBits[index]);
                    const auto delta = value - previous;
                    if (IsFinite(delta) && delta != 0.0F)
                        accepted = PublishActorValue(a_player, GameplayAction::ModifyActorValue, actorValue, delta);
                } else {
                    accepted = PublishActorValue(a_player, GameplayAction::SetActorValue, actorValue, value);
                }
                if (accepted)
                {
                    a_current.ActorValueBits[index] = valueBits;
                    a_current.ActorValueCaptured[index] = true;
                }
            } else {
                a_current.ActorValueBits[index] = valueBits;
            }
        }

        const auto maximum = a_player.GetActorValueMax(actorValue);
        if (IsFinite(maximum)) {
            const auto maximumBits = std::bit_cast<std::uint32_t>(maximum);
            if (!g_snapshot.ActorMaximumCaptured[index] || maximumBits != g_snapshot.ActorMaximumBits[index]) {
                if (PublishActorValue(a_player, GameplayAction::SetActorMaximum, actorValue, maximum))
                {
                    a_current.ActorMaximumBits[index] = maximumBits;
                    a_current.ActorMaximumCaptured[index] = true;
                }
            } else {
                a_current.ActorMaximumBits[index] = maximumBits;
            }
        }
    }

    const auto level = a_player.GetLevel();
    if (level != 0 && (!g_snapshot.LevelCaptured || level != g_snapshot.Level)) {
        if (PublishActorMetadata(a_player, GameplayAction::SetLevel, level))
        {
            a_current.Level = level;
            a_current.LevelCaptured = true;
        }
    } else if (level != 0) {
        a_current.Level = level;
    }

    const auto essential = a_player.IsEssential();
    if (!g_snapshot.EssentialCaptured || essential != g_snapshot.Essential) {
        if (PublishActorMetadata(a_player, GameplayAction::SetEssential, essential ? 1 : 0))
        {
            a_current.Essential = essential;
            a_current.EssentialCaptured = true;
        }
    } else {
        a_current.Essential = essential;
    }

    const auto dead = a_player.IsDead();
    if (!g_snapshot.DeadCaptured || dead != g_snapshot.Dead) {
        if (PublishDeathState(a_player, dead))
        {
            a_current.Dead = dead;
            a_current.DeadCaptured = true;
        }
    } else {
        a_current.Dead = dead;
    }

    const auto* state = a_player.AsActorState();
    const auto weaponDrawn = state && state->IsWeaponDrawn();
    if (!g_snapshot.WeaponDrawnCaptured || weaponDrawn != g_snapshot.WeaponDrawn) {
        if (PublishDrawState(a_player, weaponDrawn))
        {
            a_current.WeaponDrawn = weaponDrawn;
            a_current.WeaponDrawnCaptured = true;
        }
    } else {
        a_current.WeaponDrawn = weaponDrawn;
    }

    RE::NiPointer<RE::Actor> mount;
    auto mountFormId = RE::FormID{};
    if (a_player.GetMount(mount) && mount && IsValidFormId(mount->GetFormID()))
        mountFormId = mount->GetFormID();
    // Zero is an internal cancellation signal for a pending local mount. The
    // mapped client never serializes it because the original wire protocol has
    // no dismount message.
    if ((!g_snapshot.Valid && mountFormId != 0) ||
        (g_snapshot.Valid && g_snapshot.MountFormId != mountFormId)) {
        if (PublishMount(a_player, mountFormId))
            a_current.MountFormId = mountFormId;
    } else {
        a_current.MountFormId = mountFormId;
    }

    auto packageFormId = RE::FormID{};
    if (const auto* package = a_player.GetCurrentPackage(); package && IsValidFormId(package->GetFormID()))
        packageFormId = package->GetFormID();
    const auto now = std::chrono::steady_clock::now();
    if (packageFormId != 0 &&
        (!g_snapshot.Valid || packageFormId != g_snapshot.PackageFormId ||
         g_lastPackagePublishedAt == std::chrono::steady_clock::time_point{} ||
         now - g_lastPackagePublishedAt >= kPackageRefreshInterval)) {
        GameplayActionPayload payload{};
        if (PreparePlayerPayload(payload, a_player)) {
            payload.LocalFormIdA = packageFormId;
            if (Publish(GameplayDomain::Dialogue, GameplayAction::Package, payload)) {
                a_current.PackageFormId = packageFormId;
                g_lastPackagePublishedAt = now;
            }
        }
    } else {
        a_current.PackageFormId = packageFormId;
    }
}

void CaptureExperience(RE::PlayerCharacter& a_player, Snapshot& a_current) noexcept
{
    const auto* skills = a_player.GetInfoRuntimeData().skills;
    if (!skills || !skills->data)
        return;

    const auto now = std::chrono::steady_clock::now();
    std::array<std::uint32_t, kSkillCount> skillXpBits{};
    for (std::size_t index = 0; index < kSkillCount; ++index) {
        const auto xp = skills->data->skills[index].xp;
        if (!IsFinite(xp) || xp < 0.0F)
            return;
        skillXpBits[index] = std::bit_cast<std::uint32_t>(xp);
    }

    if (!g_snapshot.Valid || !g_snapshot.SkillsValid) {
        a_current.SkillsValid = true;
        a_current.SkillXpBits = skillXpBits;
        return;
    }

    for (std::size_t index = 0; index < kSkillCount; ++index) {
        if (skillXpBits[index] == g_snapshot.SkillXpBits[index])
            continue;

        auto& suppression = g_experienceSuppressions[index];
        if (suppression != std::chrono::steady_clock::time_point{} && suppression >= now) {
            suppression = {};
            a_current.SkillXpBits[index] = skillXpBits[index];
            continue;
        }
        suppression = {};

        const auto actorValue = GameplayBridge::kFirstSkillActorValue + static_cast<std::uint32_t>(index);
        if (!GameplayBridge::IsCombatSkillActorValue(actorValue))
            continue;
        const auto previous = std::bit_cast<float>(g_snapshot.SkillXpBits[index]);
        const auto current = std::bit_cast<float>(skillXpBits[index]);
        const auto delta = current - previous;
        if (!IsFinite(delta) || delta <= 0.0F || delta > GameplayBridge::kMaximumSyncedExperience)
            continue;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, a_player))
            continue;
        payload.LocalFormIdA = actorValue;
        payload.ScalarA = delta;
        if (Publish(GameplayDomain::ActorState, GameplayAction::SyncExperience, payload))
            a_current.SkillXpBits[index] = skillXpBits[index];
    }
}

void CaptureEquipment(RE::PlayerCharacter& a_player, Snapshot& a_current) noexcept
{
    std::vector<WornEquipmentEntry> wornEquipment;
    if (!CaptureWornEquipment(a_player, wornEquipment))
        return;
    const auto magicEquipment = CaptureSelectedMagicEquipment(a_player);
    const auto now = std::chrono::steady_clock::now();
    if ((!g_snapshot.EquipmentCaptured || wornEquipment != g_snapshot.WornEquipment ||
         magicEquipment != g_snapshot.MagicEquipment ||
         g_lastEquipmentPublishedAt == std::chrono::steady_clock::time_point{} ||
         now - g_lastEquipmentPublishedAt >= kEquipmentRefreshInterval) &&
        PublishEquipmentSnapshot(a_player, wornEquipment, magicEquipment)) {
        a_current.WornEquipment = std::move(wornEquipment);
        a_current.MagicEquipment = magicEquipment;
        a_current.EquipmentCaptured = true;
        g_lastEquipmentPublishedAt = now;
    } else if (wornEquipment == g_snapshot.WornEquipment && magicEquipment == g_snapshot.MagicEquipment) {
        a_current.WornEquipment = std::move(wornEquipment);
        a_current.MagicEquipment = magicEquipment;
    }
}

void CaptureInventory(RE::PlayerCharacter& a_player, Snapshot& a_current) noexcept
{
    a_current.Inventory.clear();
    const auto counts = a_player.GetInventoryCounts();
    for (const auto& [object, count] : counts) {
        if (a_current.Inventory.size() >= kMaximumInventoryEntries)
            break;
        if (!object || !IsValidFormId(object->GetFormID()) || count == 0 ||
            count < -kMaximumInventoryDelta || count > kMaximumInventoryDelta)
            continue;
        a_current.Inventory.emplace(object->GetFormID(), count);
    }

    // The event ring has no multi-event transaction. Advance each item in the
    // baseline only after its delta has been accepted, so a full ring retries
    // only the missing deltas on the next owner-thread capture. Do not grow a
    // partially committed baseline beyond the current snapshot's 512-entry cap.
    auto publishedInventory = g_snapshot.Valid ? g_snapshot.Inventory : decltype(g_snapshot.Inventory){};
    for (const auto& [formId, previousCount] : g_snapshot.Inventory) {
        const auto current = a_current.Inventory.find(formId);
        const auto currentCount = current != a_current.Inventory.end() ? current->second : 0;
        if (currentCount == previousCount)
            continue;
        if (!PublishInventoryDelta(a_player, formId, currentCount - previousCount))
            continue;
        if (currentCount == 0)
            publishedInventory.erase(formId);
        else
            publishedInventory.insert_or_assign(formId, currentCount);
    }
    for (const auto& [formId, currentCount] : a_current.Inventory) {
        if (!g_snapshot.Inventory.contains(formId) && publishedInventory.size() < kMaximumInventoryEntries &&
            PublishInventoryDelta(a_player, formId, currentCount))
            publishedInventory.insert_or_assign(formId, currentCount);
    }
    a_current.Inventory = std::move(publishedInventory);
}

void CaptureWorldObjects(RE::PlayerCharacter& a_player) noexcept
{
    auto* cell = a_player.GetParentCell();
    if (!cell || !IsValidFormId(cell->GetFormID()) || cell->GetFormID() == g_lastObjectCellFormId ||
        !CanPublish(GameplayDomain::Object))
        return;

    const auto* loaded = cell->GetRuntimeData().loadedData;
    const auto encounterZoneId = loaded && loaded->encounterZone ? loaded->encounterZone->GetFormID() : 0;
    const bool playerHome = encounterZoneId == 0x000F90B1 && cell->GetFormID() != 0x000EEC55;
    const auto* worldspace = cell->GetRuntimeData().worldSpace;
    const auto worldspaceId = worldspace ? worldspace->GetFormID() : 0;
    bool complete = true;
    std::size_t emittedEntries{};
    std::size_t emittedObjects{};
    auto& endpoint = BridgeEndpoint::Get();
    std::vector<EventRecord> records;
    records.reserve(2 * kMaximumObjectSnapshots + kMaximumObjectSnapshotEntries);
    const auto append = [&](const GameplayAction a_action, const GameplayActionPayload& a_payload) {
        auto& record = records.emplace_back();
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
        record.Payload.LocalGameplayAction = a_payload;
        record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::Object);
        record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
    };

    struct InventoryItemSnapshot
    {
        std::uint32_t FormId{};
        std::int32_t Count{};
        bool QuestItem{};
    };

    cell->ForEachReference([&](RE::TESObjectREFR* a_reference) {
        if (!complete || !a_reference || !IsValidFormId(a_reference->GetFormID()) ||
            a_reference->GetFormID() == 0x00039CF1 || a_reference->GetFormID() == 0x0003EF03)
            return complete ? RE::BSContainer::ForEachResult::kContinue : RE::BSContainer::ForEachResult::kStop;

        const auto* base = a_reference->GetBaseObject();
        if (!base || (base->GetFormType() != RE::FormType::Container && base->GetFormType() != RE::FormType::Door))
            return RE::BSContainer::ForEachResult::kContinue;

        // Never publish a destructive container assignment unless its entire
        // bounded inventory can be represented in this cell transaction.
        std::vector<InventoryItemSnapshot> inventory;
        if (base->GetFormType() == RE::FormType::Container) {
            for (const auto& [object, data] : a_reference->GetInventory()) {
                const auto count = data.first;
                const auto& entry = data.second;
                if (!object || !IsValidFormId(object->GetFormID()) || count <= 0 ||
                    count > kMaximumInventoryDelta)
                    continue;
                if (emittedEntries + inventory.size() >= kMaximumObjectSnapshotEntries) {
                    inventory.clear();
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                inventory.push_back({object->GetFormID(), count, entry && entry->IsQuestObject()});
            }
        }

        if (emittedObjects >= kMaximumObjectSnapshots)
            return RE::BSContainer::ForEachResult::kContinue;

        GameplayActionPayload payload{};
        payload.TargetLocalFormId = a_reference->GetFormID();
        payload.LocalFormIdA = cell->GetFormID();
        payload.LocalFormIdB = worldspaceId;
        payload.ValueA = base->GetFormType() == RE::FormType::Door ?
            static_cast<std::int32_t>(RE::BGSOpenCloseForm::GetOpenState(a_reference)) : 0;
        payload.ValueB = a_reference->IsLocked() ? static_cast<std::int32_t>(a_reference->GetLockLevel()) : -1;
        const auto position = a_reference->GetPosition();
        payload.ScalarA = position.x;
        payload.ScalarB = position.y;
        payload.ScalarC = position.z;
        payload.ActionFlags = (base->GetFormType() == RE::FormType::Container ? kObjectSnapshotContainer : 0u) |
                              (playerHome ? kObjectSnapshotPlayerHome : 0u);
        append(GameplayAction::ObjectSnapshotBegin, payload);

        if (base->GetFormType() == RE::FormType::Container) {
            for (const auto& entry : inventory) {
                GameplayActionPayload item{};
                item.TargetLocalFormId = a_reference->GetFormID();
                item.LocalFormIdA = entry.FormId;
                item.ValueA = entry.Count;
                item.ActionFlags = entry.QuestItem ? kInventoryQuestItem : 0u;
                append(GameplayAction::ObjectSnapshotItem, item);
                ++emittedEntries;
            }
        }

        GameplayActionPayload end{};
        end.TargetLocalFormId = a_reference->GetFormID();
        append(GameplayAction::ObjectSnapshotEnd, end);
        ++emittedObjects;
        return RE::BSContainer::ForEachResult::kContinue;
    });

    if (complete && (records.empty() || endpoint.TryPushEvents(records.data(), records.size())))
        g_lastObjectCellFormId = cell->GetFormID();
}

void CaptureWeather(const RE::PlayerCharacter& a_player) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (g_lastWeatherObservationAt != std::chrono::steady_clock::time_point{} &&
        now - g_lastWeatherObservationAt < kWeatherObservationInterval)
        return;
    g_lastWeatherObservationAt = now;

    const auto* sky = RE::Sky::GetSingleton();
    const auto* weather = sky ? sky->currentWeather : nullptr;
    const auto formId = weather ? weather->GetFormID() : 0;
    if (formId == kMapWeatherFormId) {
        g_hasWeatherSnapshot = false;
        g_lastWeatherFormId = 0;
        return;
    }
    if (!IsValidFormId(formId) || (g_hasWeatherSnapshot && g_lastWeatherFormId == formId))
        return;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return;
    payload.LocalFormIdA = formId;
    if (Publish(GameplayDomain::WorldState, GameplayAction::SetWeather, payload)) {
        g_hasWeatherSnapshot = true;
        g_lastWeatherFormId = formId;
    }
}

[[nodiscard]] bool WaypointMatches(const WaypointSnapshot& a_snapshot, const RE::TESWorldSpace& a_worldspace,
                                    const RE::NiPoint3& a_position) noexcept
{
    return a_snapshot.Valid && a_snapshot.WorldspaceFormId == a_worldspace.GetFormID() &&
           a_snapshot.Position.x == a_position.x && a_snapshot.Position.y == a_position.y &&
           a_snapshot.Position.z == a_position.z;
}

void CaptureWaypoint(const RE::PlayerCharacter& a_player) noexcept
{
    // GetVRInfoRuntimeData is the CommonLib VR-only accessor. Its custom map
    // marker is a typed ObjectRefHandle, avoiding any raw PlayerCharacter
    // layout access while retaining handle lifetime validation.
    const auto* runtimeData = a_player.GetVRInfoRuntimeData();
    if (!runtimeData)
        return;

    const auto marker = runtimeData->playerMapMarker.get();
    if (!marker) {
        if (!g_waypointSnapshot.Valid)
            return;
        GameplayActionPayload payload{};
        if (PreparePlayerPayload(payload, a_player) &&
            Publish(GameplayDomain::Party, GameplayAction::RemoveWaypoint, payload))
            g_waypointSnapshot = {};
        return;
    }

    auto* worldspace = marker->GetWorldspace();
    const auto position = marker->GetPosition();
    if (!worldspace || !IsValidFormId(worldspace->GetFormID()) || !IsFinite(position.x) || !IsFinite(position.y) ||
        !IsFinite(position.z) || WaypointMatches(g_waypointSnapshot, *worldspace, position))
        return;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player))
        return;
    payload.LocalFormIdA = worldspace->GetFormID();
    payload.ScalarA = position.x;
    payload.ScalarB = position.y;
    payload.ScalarC = position.z;
    if (Publish(GameplayDomain::Party, GameplayAction::SetWaypoint, payload)) {
        g_waypointSnapshot = {
            .Valid = true,
            .WorldspaceFormId = worldspace->GetFormID(),
            .Position = position,
        };
    }
}

void CapturePlayerDialogue(const RE::PlayerCharacter& a_player) noexcept
{
    const auto* manager = RE::MenuTopicManager::GetSingleton();
    const auto* selected = manager && manager->menuOpen ? manager->lastSelectedDialogue : nullptr;
    if (!selected) {
        g_lastSelectedDialogue = nullptr;
        g_lastSelectedDialogueText.clear();
        return;
    }

    const char* text = selected->topicText.c_str();
    if (!text || text[0] == '\0')
        return;
    const std::string_view selectedText{text};
    if (selected == g_lastSelectedDialogue && selectedText == g_lastSelectedDialogueText)
        return;

    GameplayActionPayload payload{};
    if (!PreparePlayerPayload(payload, a_player) ||
        !PublishText(GameplayDomain::Dialogue, GameplayAction::Dialogue, payload, text,
                     kMaximumPlayerDialogueBytes))
        return;
    g_lastSelectedDialogue = selected;
    g_lastSelectedDialogueText.assign(selectedText);
}

void InitializeUnlocked() noexcept
{
    RegisterScriptSinks();
    g_initialized = true;
}

void ResetCaptureBaselinesUnlocked() noexcept
{
    g_snapshot = {};
    g_lastSnapshotAt = {};
    g_lastObjectCellFormId = 0;
    g_lastNpcDiscoveryAt = {};
    g_lastNpcObservationAt = {};
    g_lastNpcDiscoveryCellFormId = 0;
    g_npcDiscoveryOffset = 0;
    g_npcObservationOffset = 0;
    g_observedNpcReferences.clear();
    g_lastWeatherObservationAt = {};
    g_lastEquipmentPublishedAt = {};
    g_lastPackagePublishedAt = {};
    g_experienceSuppressions.fill(std::chrono::steady_clock::time_point{});
    g_hasWeatherSnapshot = false;
    g_lastWeatherFormId = 0;
    g_waypointSnapshot = {};
    g_lastSelectedDialogue = nullptr;
    g_lastSelectedDialogueText.clear();
    g_periodicCaptureActive = false;
    g_periodicCaptureFailed = false;
    ClearQuestSuppressions();
    ClearLockSuppressions();
    g_nextQuestSuppressionToken.store(0, std::memory_order_release);
    g_nextLockSuppressionToken.store(0, std::memory_order_release);
    g_nextTextId.store(0, std::memory_order_release);
}
} // namespace

QuestSuppressionToken ArmQuestStartStopSuppression(
    const std::uint32_t a_questLocalFormId,
    const bool a_started) noexcept
{
    try {
        return ArmQuestSuppression(QuestSuppressionKind::StartStop, a_questLocalFormId, 0, a_started);
    } catch (...) {
        return 0;
    }
}

QuestSuppressionToken ArmQuestStageSuppression(
    const std::uint32_t a_questLocalFormId,
    const std::uint16_t a_stage) noexcept
{
    try {
        return ArmQuestSuppression(QuestSuppressionKind::Stage, a_questLocalFormId, a_stage, false);
    } catch (...) {
        return 0;
    }
}

void CancelQuestSuppression(const QuestSuppressionToken a_token) noexcept
{
    if (a_token == 0)
        return;
    try {
        const std::scoped_lock lock{g_questSuppressionLock};
        for (auto& suppression : g_questSuppressions) {
            if (suppression.Token == a_token) {
                suppression = {};
                return;
            }
        }
    } catch (...) {
    }
}

LockSuppressionToken ArmLockSuppression(
    const std::uint32_t a_referenceLocalFormId, const bool a_locked, const std::uint8_t a_lockLevel) noexcept
{
    if (a_referenceLocalFormId == 0 || (!a_locked && a_lockLevel != 0) || a_lockLevel > 5)
        return 0;
    try {
        const auto now = std::chrono::steady_clock::now();
        const auto token = g_nextLockSuppressionToken.fetch_add(1, std::memory_order_relaxed) + 1;
        if (token == 0)
            return 0;
        const std::scoped_lock lock{g_lockSuppressionLock};
        LockSuppression* slot{};
        for (auto& suppression : g_lockSuppressions) {
            if (suppression.Token != 0 && suppression.ExpiresAt <= now)
                suppression = {};
            if (!slot && suppression.Token == 0)
                slot = &suppression;
        }
        if (!slot)
            return 0;
        *slot = {
            .ReferenceFormId = a_referenceLocalFormId,
            .LockLevel = a_lockLevel,
            .Locked = a_locked,
            .Token = token,
            .ExpiresAt = now + kQuestSuppressionLifetime,
        };
        return token;
    } catch (...) {
        return 0;
    }
}

void CancelLockSuppression(const LockSuppressionToken a_token) noexcept
{
    if (a_token == 0)
        return;
    try {
        const std::scoped_lock lock{g_lockSuppressionLock};
        for (auto& suppression : g_lockSuppressions) {
            if (suppression.Token == a_token) {
                suppression = {};
                return;
            }
        }
    } catch (...) {
    }
}

void Initialize() noexcept
{
    try {
        const std::scoped_lock lock{g_captureLock};
        InitializeUnlocked();
    } catch (...) {
    }
}

void Arm() noexcept
{
    try {
        g_armed.store(false, std::memory_order_release);
        const std::scoped_lock lock{g_captureLock};
        ResetCaptureBaselinesUnlocked();
        g_armed.store(true, std::memory_order_release);
    } catch (...) {
    }
}

void CapturePeriodic() noexcept
{
    try {
        const std::scoped_lock lock{g_captureLock};
        if (!g_armed.load(std::memory_order_acquire))
            return;
        if (!g_initialized)
            InitializeUnlocked();

        const auto now = std::chrono::steady_clock::now();
        if (g_lastSnapshotAt != std::chrono::steady_clock::time_point{} &&
            now - g_lastSnapshotAt < kSnapshotInterval)
            return;
        g_lastSnapshotAt = now;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return;
        RegisterAnimationSink(*player);
        g_periodicCaptureActive = true;
        g_periodicCaptureFailed = false;

        Snapshot current = g_snapshot;
        CaptureAppearance(*player, current);
        CaptureActorState(*player, current);
        CaptureExperience(*player, current);
        CaptureInventory(*player, current);
        // Inventory deltas must precede the first equipment transaction so
        // the server updates worn state without creating duplicate counts.
        CaptureEquipment(*player, current);
        CaptureWorldObjects(*player);
        CaptureWeather(*player);
        CaptureWaypoint(*player);
        CapturePlayerDialogue(*player);
        CaptureNpcDiscovery();
        CaptureObservedNpcs();
        g_periodicCaptureActive = false;
        current.Valid = true;
        g_snapshot = std::move(current);
    } catch (...) {
        g_periodicCaptureActive = false;
    }
}

bool ArmExperienceSuppression(const std::uint32_t a_actorValue) noexcept
{
    try {
        if (!GameplayBridge::IsCombatSkillActorValue(a_actorValue))
            return false;
        const auto index = static_cast<std::size_t>(a_actorValue - GameplayBridge::kFirstSkillActorValue);
        const std::scoped_lock lock{g_captureLock};
        g_experienceSuppressions[index] = std::chrono::steady_clock::now() + kExperienceSuppressionLifetime;
        return true;
    } catch (...) {
        return false;
    }
}

bool StartNpcObservation(const std::uint32_t a_localReferenceFormId) noexcept
{
    try {
        const std::scoped_lock lock{g_captureLock};
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_localReferenceFormId);
        if (!actor || actor == RE::PlayerCharacter::GetSingleton() ||
            (g_observedNpcReferences.size() >= kMaximumObservedNpcs &&
             !g_observedNpcReferences.contains(a_localReferenceFormId)))
            return false;
        g_observedNpcReferences.insert(a_localReferenceFormId);
        return true;
    } catch (...) {
        return false;
    }
}

void StopNpcObservation(const std::uint32_t a_localReferenceFormId) noexcept
{
    try {
        const std::scoped_lock lock{g_captureLock};
        g_observedNpcReferences.erase(a_localReferenceFormId);
    } catch (...) {
    }
}

bool CaptureDialogueVoice(
    const std::uint32_t a_localActorFormId,
    const char* a_resourcePath) noexcept
{
    try {
        if (!g_armed.load(std::memory_order_acquire))
            return false;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_localActorFormId);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || actor == player || !a_resourcePath || a_resourcePath[0] == '\0')
            return false;

        GameplayActionPayload payload{};
        payload.TargetHandle = kLocalPlayerHandle;
        payload.TargetLocalFormId = a_localActorFormId;
        return PublishText(
            GameplayDomain::Dialogue,
            GameplayAction::Dialogue,
            payload,
            a_resourcePath,
            kMaximumPlayerDialogueBytes);
    } catch (...) {
        return false;
    }
}

void Reset() noexcept
{
    try {
        g_armed.store(false, std::memory_order_release);
        const std::scoped_lock lock{g_captureLock};
        if (g_animationSinkRegistered) {
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player == g_animationSinkPlayer)
                player->RemoveAnimationGraphEventSink(&g_animationSink);
            g_animationSinkRegistered = false;
            g_animationSinkPlayer = nullptr;
        }

        if (g_scriptSinksRegistered) {
            if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->RemoveEventSink(&g_activateSink);
                holder->RemoveEventSink(&g_containerChangedSink);
                holder->RemoveEventSink(&g_equipSink);
                holder->RemoveEventSink(&g_lockChangedSink);
                holder->RemoveEventSink(&g_deathSink);
                holder->RemoveEventSink(&g_combatSink);
                holder->RemoveEventSink(&g_hitSink);
                holder->RemoveEventSink(&g_playerBowShotSink);
                holder->RemoveEventSink(&g_questStartStopSink);
                holder->RemoveEventSink(&g_questStageSink);
            }
            g_scriptSinksRegistered = false;
        }

        g_initialized = false;
        ResetCaptureBaselinesUnlocked();
    } catch (...) {
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture
