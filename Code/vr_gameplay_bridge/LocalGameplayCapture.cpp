#include "LocalGameplayCapture.h"

#include "BridgeEndpoint.h"
#include "AnimationGraphDescriptors.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <new>
#include <numeric>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture
{
namespace
{
constexpr std::size_t kActorValueCount = GameplayBridge::kSkyrimActorValueCount;
constexpr std::size_t kSkillCount = 18;
constexpr std::size_t kHeadPartCount = static_cast<std::size_t>(RE::BGSHeadPart::HeadPartType::kTotal);
constexpr std::size_t kTintTypeCount = static_cast<std::size_t>(RE::TintMask::Type::kTotal);
constexpr std::size_t kEquipmentSlotCount = 2;
constexpr std::size_t kMaximumWornEquipmentEntries = 64;
// Bound reconciliation without truncating ordinary heavily modded players.
// Live container events remain the primary delta path; this snapshot repairs
// missed events and initial state within one quarter of the event ring.
constexpr std::size_t kMaximumInventoryEntries = GameplayBridge::kMaximumInventoryTransactionItems;
constexpr std::size_t kMaximumInventoryEffects = GameplayBridge::kMaximumInventoryTransactionEffects;
// These match the assignment receiver's bounded record limits.
constexpr std::size_t kMaximumAssignmentFactionEntries = 511;
// Assignment bootstrap is a paged protocol transaction. Its aggregate is
// deliberately larger than one event ring so it can represent every bounded
// quest, inventory, faction, appearance, and text record before publication.
constexpr std::size_t kMaximumAssignmentBootstrapEvents =
    2 + kActorValueCount + GameplayBridge::kMaximumInventoryTransactionItems * 2 +
    GameplayBridge::kMaximumInventoryTransactionEffects + 1 + VRAssignmentLimits::kMaximumQuestEntries +
    kMaximumAssignmentFactionEntries * 2 + 1 + kFaceMorphCount + kFacePartCount +
    GameplayBridge::kMaximumAppearanceHeadParts + GameplayBridge::kMaximumAppearanceTints +
    GameplayBridge::kMaximumAppearanceTextRecords + 1;
constexpr std::size_t kMaximumNpcActors = 8;
constexpr std::size_t kMaximumNpcCandidates = 128;
constexpr std::size_t kMaximumNpcItems = GameplayBridge::kMaximumNpcSnapshotItems;
constexpr std::size_t kMaximumNpcFactions = GameplayBridge::kMaximumNpcSnapshotFactions;
constexpr std::size_t kMaximumObservedNpcs = 64;
constexpr std::size_t kMaximumContainerInventoryBaselines = 128;
constexpr std::size_t kMaximumPlayerNameBytes = 128;
constexpr std::size_t kMaximumPlayerDialogueBytes = 512;
constexpr std::int32_t kMaximumInventoryDelta = 10'000;
constexpr std::int32_t kMaximumCapturedInventoryCount = 1'000'000;
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

static_assert(static_cast<std::size_t>(RE::ActorValue::kTotal) == kActorValueCount);
static_assert(static_cast<std::uint32_t>(RE::ActorValue::kHealth) ==
              GameplayBridge::kEssentialAssignmentActorValues[0]);
static_assert(static_cast<std::uint32_t>(RE::ActorValue::kMagicka) ==
              GameplayBridge::kEssentialAssignmentActorValues[1]);
static_assert(static_cast<std::uint32_t>(RE::ActorValue::kStamina) ==
              GameplayBridge::kEssentialAssignmentActorValues[2]);
static_assert(kHeadPartCount <= GameplayBridge::kMaximumAppearanceHeadParts);
static_assert(kMaximumAssignmentBootstrapEvents <= VRAssignmentLimits::kMaximumLogicalBootstrapRecords);
static_assert(VRAssignmentLimits::kBootstrapPageRecords <= GameplayBridge::kDefaultEventRingCapacity);
constexpr auto kCapturedActorValues = [] {
    std::array<RE::ActorValue, kActorValueCount> values{};
    for (std::size_t index = 0; index < values.size(); ++index)
        values[index] = static_cast<RE::ActorValue>(index);
    return values;
}();
constexpr std::array<RE::ActorValue, GameplayBridge::kEssentialAssignmentActorValues.size()>
    kAssignmentActorValues{
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

struct CapturedInventoryEffect
{
    std::uint32_t EffectFormId{};
    std::int32_t Area{};
    std::int32_t Duration{};
    float Magnitude{};
    float RawCost{};

    [[nodiscard]] bool operator==(const CapturedInventoryEffect&) const noexcept = default;
};

// A pointer-free copy of one native inventory stack. SourceUniqueId is used
// only while resolving a TESContainerChangedEvent and never crosses the ABI.
struct CapturedInventoryStack
{
    std::uint32_t FormId{};
    std::int32_t Count{};
    std::uint32_t ItemFlags{};
    std::uint32_t ExtraFlags{};
    std::uint32_t EnchantmentFormId{};
    std::uint32_t PoisonFormId{};
    std::uint32_t SoulLevel{};
    std::uint16_t EnchantmentCharge{};
    std::uint32_t PoisonCount{};
    float Charge{};
    float Health{};
    std::uint16_t SourceUniqueId{};
    std::vector<CapturedInventoryEffect> Effects{};

    [[nodiscard]] bool IsSameMetadata(const CapturedInventoryStack& ac_other) const noexcept
    {
        return FormId == ac_other.FormId && ItemFlags == ac_other.ItemFlags &&
               ExtraFlags == ac_other.ExtraFlags && EnchantmentFormId == ac_other.EnchantmentFormId &&
               PoisonFormId == ac_other.PoisonFormId && SoulLevel == ac_other.SoulLevel &&
               EnchantmentCharge == ac_other.EnchantmentCharge && PoisonCount == ac_other.PoisonCount &&
               Charge == ac_other.Charge && Health == ac_other.Health && Effects == ac_other.Effects;
    }
};

struct SelectedMagicEquipment
{
    std::uint32_t LeftSpellFormId{};
    std::uint32_t RightSpellFormId{};
    std::uint32_t PowerOrShoutFormId{};

    [[nodiscard]] bool operator==(const SelectedMagicEquipment&) const noexcept = default;
};

struct CapturedTint
{
    std::uint32_t Color{};
    std::uint32_t AlphaBits{};
    std::uint8_t Type{};
    std::string TexturePath{};

    [[nodiscard]] bool operator==(const CapturedTint&) const noexcept = default;
};

struct Snapshot
{
    bool Valid{};
    bool InventoryCaptured{};
    std::uint32_t RaceFormId{};
    std::int32_t Sex{};
    bool SexCaptured{};
    std::uint32_t WeightBits{};
    bool WeightCaptured{};
    std::uint32_t HairColorFormId{};
    bool HairColorCaptured{};
    std::uint32_t FaceTextureFormId{};
    bool FaceTextureCaptured{};
    std::array<std::uint32_t, kFaceMorphCount> FaceMorphBits{};
    std::array<bool, kFaceMorphCount> FaceMorphCaptured{};
    std::array<std::int32_t, kFacePartCount> FaceParts{};
    std::array<bool, kFacePartCount> FacePartCaptured{};
    bool FaceDataPresent{};
    bool FaceDataPresenceCaptured{};
    std::uint64_t NameHash{};
    std::array<std::uint32_t, kHeadPartCount> HeadParts{};
    std::vector<CapturedTint> Tints{};
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
    std::vector<CapturedInventoryStack> Inventory;
};

struct AssignmentInventorySeed
{
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
    std::vector<CapturedInventoryStack> Inventory{};
    bool Valid{};
};

struct AssignmentBootstrapPublication
{
    std::vector<EventRecord> Records{};
    std::vector<CapturedInventoryStack> Inventory{};
    std::uint64_t ServerInstanceNonce{};
    std::uint64_t ConnectionGeneration{};
    std::uint64_t LifecycleEpoch{};
    std::size_t Count{};
    std::size_t Next{};
    bool Active{};

    void Reset() noexcept
    {
        Records.clear();
        Inventory.clear();
        Count = 0;
        Next = 0;
        ServerInstanceNonce = 0;
        ConnectionGeneration = 0;
        LifecycleEpoch = 0;
        Active = false;
    }
};

std::recursive_mutex g_captureLock;
std::mutex g_textPublishLock;
Snapshot g_snapshot;
AssignmentInventorySeed g_assignmentInventorySeed;
AssignmentBootstrapPublication g_assignmentBootstrapPublication;
std::atomic_bool g_armed{};
std::atomic<std::uint64_t> g_nextActionId{};
std::atomic<std::uint64_t> g_nextNpcSnapshotActionOrdinal{};
std::atomic<std::uint64_t> g_nextTextId{};
std::atomic<std::uint64_t> g_nextQuestSuppressionToken{};
std::atomic<std::uint64_t> g_nextLockSuppressionToken{};
thread_local std::uint32_t g_remoteInventorySuppressionDepth{};
bool g_initialized{};
bool g_periodicCaptureActive{};
bool g_periodicCaptureFailed{};
bool g_scriptSinksRegistered{};
bool g_animationSinkRegistered{};
RE::PlayerCharacter* g_animationSinkPlayer{};
std::uint32_t g_lastObjectCellFormId{};

using ObjectSnapshotItem = CapturedInventoryStack;

struct ObjectSnapshotTransaction
{
    std::uint32_t ReferenceFormId{};
    std::uint32_t CellFormId{};
    std::uint32_t WorldspaceFormId{};
    std::int32_t OpenState{};
    std::int32_t LockLevel{-1};
    RE::NiPoint3 Position{};
    std::uint32_t Flags{};
    std::vector<ObjectSnapshotItem> Inventory{};
    std::uint64_t ActionId{};
    std::size_t ItemIndex{};
    std::size_t EffectIndex{};
    std::size_t EffectCount{};
    std::uint8_t ItemStage{};
    bool BeginPublished{};
};

struct CellObjectSnapshot
{
    std::uint32_t CellFormId{};
    std::vector<ObjectSnapshotTransaction> Objects{};
    std::size_t ObjectIndex{};
    bool Valid{};
};

CellObjectSnapshot g_cellObjectSnapshot{};
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
std::unordered_set<std::uint32_t> g_pendingInventoryReconciliations{};
std::map<std::uint32_t, std::vector<CapturedInventoryStack>> g_containerInventoryBaselines{};
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

[[nodiscard]] bool CaptureInventoryStack(
    RE::TESBoundObject& a_object, const std::int32_t a_count, RE::ExtraDataList* ap_extraList,
    CapturedInventoryStack& ar_stack)
{
    if (a_count <= 0 || a_count > kMaximumCapturedInventoryCount || !IsValidFormId(a_object.GetFormID()))
        return false;

    CapturedInventoryStack stack{};
    stack.FormId = a_object.GetFormID();
    stack.Count = a_count;
    if (a_object.GetFormType() == RE::FormType::Weapon)
        stack.ItemFlags |= kInventoryTransactionWeapon;
    if (a_object.GetFormType() == RE::FormType::Ammo)
        stack.ItemFlags |= kInventoryTransactionAmmo;

    if (ap_extraList) {
        if (ap_extraList->HasQuestObjectAlias())
            stack.ItemFlags |= kInventoryTransactionQuestItem;
        if (ap_extraList->HasType<RE::ExtraWorn>())
            stack.ItemFlags |= kInventoryTransactionWorn;
        if (ap_extraList->HasType<RE::ExtraWornLeft>())
            stack.ItemFlags |= kInventoryTransactionWornLeft;
        if (const auto* extra = ap_extraList->GetByType<RE::ExtraUniqueID>()) {
            if (extra->baseID != stack.FormId || extra->uniqueID == 0)
                return false;
            stack.SourceUniqueId = extra->uniqueID;
        }
        if (const auto* extra = ap_extraList->GetByType<RE::ExtraCharge>()) {
            if (!IsFinite(extra->charge) || extra->charge < 0.0F)
                return false;
            stack.Charge = extra->charge;
        }
        if (const auto* extra = ap_extraList->GetByType<RE::ExtraHealth>()) {
            if (!IsFinite(extra->health) || extra->health < 0.0F)
                return false;
            stack.Health = extra->health;
        }
        if (const auto* extra = ap_extraList->GetByType<RE::ExtraSoul>())
            stack.SoulLevel = static_cast<std::uint32_t>(extra->soul.get());
        if (const auto* extra = ap_extraList->GetByType<RE::ExtraPoison>()) {
            if (!extra->poison || !IsValidFormId(extra->poison->GetFormID()) ||
                extra->count == 0 ||
                extra->count > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
                return false;
            stack.PoisonFormId = extra->poison->GetFormID();
            stack.PoisonCount = extra->count;
        }
        if (const auto* extra = ap_extraList->GetByType<RE::ExtraEnchantment>()) {
            if (!extra->enchantment || !IsValidFormId(extra->enchantment->GetFormID()))
                return false;
            stack.EnchantmentFormId = extra->enchantment->GetFormID();
            stack.EnchantmentCharge = extra->charge;
            if (extra->removeOnUnequip)
                stack.ExtraFlags |= kInventoryTransactionEnchantRemoveUnequip;
            if (a_object.GetFormType() == RE::FormType::Weapon)
                stack.ExtraFlags |= kInventoryTransactionEnchantIsWeapon;
            if ((stack.EnchantmentFormId & 0xFF000000u) == 0xFF000000u) {
                for (const auto* effect : extra->enchantment->effects) {
                    if (!effect || !effect->baseEffect || !IsValidFormId(effect->baseEffect->GetFormID()) ||
                        effect->effectItem.area > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
                        effect->effectItem.duration > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
                        !IsFinite(effect->effectItem.magnitude) || !IsFinite(effect->cost))
                        return false;
                    stack.Effects.push_back({
                        effect->baseEffect->GetFormID(),
                        static_cast<std::int32_t>(effect->effectItem.area),
                        static_cast<std::int32_t>(effect->effectItem.duration),
                        effect->effectItem.magnitude,
                        effect->cost});
                }
            }
        }
    }

    if (stack.SoulLevel > 5 || stack.Effects.size() > kMaximumInventoryEffects)
        return false;
    ar_stack = std::move(stack);
    return true;
}

[[nodiscard]] bool CaptureInventoryStacks(
    RE::TESObjectREFR& a_owner, std::vector<CapturedInventoryStack>& ar_stacks,
    const std::size_t a_maximumStacks = kMaximumInventoryEntries,
    const std::size_t a_maximumEffects = kMaximumInventoryEffects)
{
    ar_stacks.clear();
    ar_stacks.reserve(a_maximumStacks);
    std::size_t effectCount{};
    for (const auto& [object, data] : a_owner.GetInventory()) {
        if (!object || !IsValidFormId(object->GetFormID()) || data.first <= 0 ||
            data.first > kMaximumCapturedInventoryCount)
            continue;

        std::vector<CapturedInventoryStack> objectStacks;
        auto remaining = data.first;
        bool discardObject{};
        const auto append = [&](const std::int32_t a_count, RE::ExtraDataList* ap_extraList) {
            CapturedInventoryStack stack{};
            if (!CaptureInventoryStack(*object, a_count, ap_extraList, stack))
                return true;
            std::sort(stack.Effects.begin(), stack.Effects.end(), [](const auto& a_left, const auto& a_right) {
                if (a_left.EffectFormId != a_right.EffectFormId)
                    return a_left.EffectFormId < a_right.EffectFormId;
                if (a_left.Area != a_right.Area)
                    return a_left.Area < a_right.Area;
                if (a_left.Duration != a_right.Duration)
                    return a_left.Duration < a_right.Duration;
                if (a_left.Magnitude != a_right.Magnitude)
                    return a_left.Magnitude < a_right.Magnitude;
                return a_left.RawCost < a_right.RawCost;
            });
            objectStacks.push_back(std::move(stack));
            return true;
        };
        const auto& entry = data.second;
        if (entry && entry->extraLists) {
            for (auto* extraList : *entry->extraLists) {
                if (!extraList) {
                    discardObject = true;
                    break;
                }
                const auto* extraCount = extraList->GetByType<RE::ExtraCount>();
                if (extraCount && extraCount->count > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
                    discardObject = true;
                    break;
                }
                const auto count = extraCount ? static_cast<std::int32_t>(extraCount->count) : 1;
                if (count <= 0 || count > remaining || !append(count, extraList)) {
                    discardObject = true;
                    break;
                }
                remaining -= count;
            }
        }
        if (discardObject)
            continue;
        if (remaining > 0 && !append(remaining, nullptr))
            continue;
        if (objectStacks.size() > a_maximumStacks - ar_stacks.size())
            return false;
        const auto objectEffects = std::accumulate(
            objectStacks.begin(), objectStacks.end(), std::size_t{},
            [](const std::size_t a_total, const CapturedInventoryStack& a_stack) {
                return a_total + a_stack.Effects.size();
            });
        if (objectEffects > a_maximumEffects - effectCount)
            return false;
        effectCount += objectEffects;
        ar_stacks.insert(ar_stacks.end(), std::make_move_iterator(objectStacks.begin()),
                         std::make_move_iterator(objectStacks.end()));
    }
    std::sort(ar_stacks.begin(), ar_stacks.end(), [](const auto& a_left, const auto& a_right) {
        if (a_left.FormId != a_right.FormId)
            return a_left.FormId < a_right.FormId;
        if (a_left.ItemFlags != a_right.ItemFlags)
            return a_left.ItemFlags < a_right.ItemFlags;
        if (a_left.ExtraFlags != a_right.ExtraFlags)
            return a_left.ExtraFlags < a_right.ExtraFlags;
        if (a_left.EnchantmentFormId != a_right.EnchantmentFormId)
            return a_left.EnchantmentFormId < a_right.EnchantmentFormId;
        if (a_left.PoisonFormId != a_right.PoisonFormId)
            return a_left.PoisonFormId < a_right.PoisonFormId;
        if (a_left.SoulLevel != a_right.SoulLevel)
            return a_left.SoulLevel < a_right.SoulLevel;
        if (a_left.EnchantmentCharge != a_right.EnchantmentCharge)
            return a_left.EnchantmentCharge < a_right.EnchantmentCharge;
        if (a_left.PoisonCount != a_right.PoisonCount)
            return a_left.PoisonCount < a_right.PoisonCount;
        if (a_left.Charge != a_right.Charge)
            return a_left.Charge < a_right.Charge;
        if (a_left.Health != a_right.Health)
            return a_left.Health < a_right.Health;
        if (a_left.Effects != a_right.Effects) {
            return std::lexicographical_compare(
                a_left.Effects.begin(), a_left.Effects.end(), a_right.Effects.begin(), a_right.Effects.end(),
                [](const auto& a_leftEffect, const auto& a_rightEffect) {
                    if (a_leftEffect.EffectFormId != a_rightEffect.EffectFormId)
                        return a_leftEffect.EffectFormId < a_rightEffect.EffectFormId;
                    if (a_leftEffect.Area != a_rightEffect.Area)
                        return a_leftEffect.Area < a_rightEffect.Area;
                    if (a_leftEffect.Duration != a_rightEffect.Duration)
                        return a_leftEffect.Duration < a_rightEffect.Duration;
                    if (a_leftEffect.Magnitude != a_rightEffect.Magnitude)
                        return a_leftEffect.Magnitude < a_rightEffect.Magnitude;
                    return a_leftEffect.RawCost < a_rightEffect.RawCost;
                });
        }
        if (a_left.SourceUniqueId != a_right.SourceUniqueId)
            return a_left.SourceUniqueId < a_right.SourceUniqueId;
        return a_left.Count < a_right.Count;
    });
    return true;
}

[[nodiscard]] constexpr std::uint16_t ToAssignmentInventoryFlags(
    const std::uint32_t a_flags) noexcept
{
    return (a_flags & kInventoryTransactionQuestItem ? kAssignmentBootstrapInventoryQuestItem : 0u) |
           (a_flags & kInventoryTransactionWorn ? kAssignmentBootstrapInventoryWorn : 0u) |
           (a_flags & kInventoryTransactionWornLeft ? kAssignmentBootstrapInventoryWornLeft : 0u) |
           (a_flags & kInventoryTransactionWeapon ? kAssignmentBootstrapInventoryWeapon : 0u) |
           (a_flags & kInventoryTransactionAmmo ? kAssignmentBootstrapInventoryAmmo : 0u);
}

[[nodiscard]] constexpr std::uint16_t ToAssignmentInventoryExtraFlags(
    const std::uint32_t a_flags) noexcept
{
    return (a_flags & kInventoryTransactionEnchantRemoveUnequip ?
                kAssignmentBootstrapEnchantRemoveUnequip : 0u) |
           (a_flags & kInventoryTransactionEnchantIsWeapon ?
                kAssignmentBootstrapEnchantIsWeapon : 0u);
}

[[nodiscard]] bool IsSafeTintTexturePath(const std::string_view a_path) noexcept
{
    if (a_path.empty())
        return true;
    if (a_path.size() > kMaximumAppearanceTexturePathBytes || a_path.front() == '/' ||
        a_path.front() == '\\' || a_path.find(':') != std::string_view::npos ||
        a_path.find('\0') != std::string_view::npos)
        return false;
    std::size_t segmentStart{};
    while (segmentStart <= a_path.size()) {
        const auto segmentEnd = a_path.find_first_of("/\\", segmentStart);
        const auto segment = a_path.substr(
            segmentStart, segmentEnd == std::string_view::npos ? std::string_view::npos : segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (segmentEnd == std::string_view::npos)
            break;
        segmentStart = segmentEnd + 1;
    }
    return true;
}

[[nodiscard]] bool IsBoundedInventoryDelta(const std::int32_t a_delta) noexcept
{
    return a_delta != 0 && a_delta >= -kMaximumInventoryDelta && a_delta <= kMaximumInventoryDelta;
}

[[nodiscard]] bool IsCapturedInventoryDelta(const std::int32_t a_delta) noexcept
{
    return a_delta != 0 && a_delta >= -kMaximumCapturedInventoryCount &&
           a_delta <= kMaximumCapturedInventoryCount;
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
    const bool objectSnapshot = a_domain == GameplayDomain::Object && IsObjectSnapshotAction(a_action);
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

struct NpcFactionEntry
{
    std::uint32_t FormId{};
    std::int8_t Rank{};
};

struct NpcAppearanceSnapshot
{
    std::uint32_t RaceFormId{};
    std::uint32_t HairColorFormId{};
    std::uint32_t FaceTextureFormId{};
    std::int32_t Sex{};
    std::uint16_t Level{};
    float Weight{};
    bool Essential{};
    bool HasFaceData{};
    std::array<float, kFaceMorphCount> FaceMorphs{};
    std::array<std::int32_t, kFacePartCount> FaceParts{};
    std::uint8_t NameLength{};
    std::array<char, kMaximumAppearanceNameBytes> Name{};
    std::uint8_t HeadPartCount{};
    std::array<std::pair<std::uint8_t, std::uint32_t>, kHeadPartCount> HeadParts{};
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
    AnimationGraphProtocol::SnapshotBuffer Animation{};
    std::vector<CapturedInventoryStack> Inventory{};
    std::vector<NpcFactionEntry> Factions{};
    NpcAppearanceSnapshot Appearance{};
    bool Dead{};
    bool WeaponDrawn{};
    bool IsDragon{};
    bool IsMount{};
    bool IsPlayerSummon{};
};

[[nodiscard]] bool CopyBoundedUtf8(
    const char* ap_text, std::array<char, kMaximumAppearanceNameBytes>& ar_output,
    std::uint8_t& ar_length) noexcept
{
    if (!ap_text)
        return false;

    std::size_t length{};
    while (length <= ar_output.size() && ap_text[length] != '\0')
        ++length;
    if (length == 0 || length > ar_output.size())
        return false;

    for (std::size_t index = 0; index < length;) {
        const auto byte = static_cast<std::uint8_t>(ap_text[index]);
        if (byte < 0x80) {
            ++index;
            continue;
        }
        std::size_t continuationCount{};
        std::uint32_t codePoint{};
        if (byte >= 0xC2 && byte <= 0xDF) { continuationCount = 1; codePoint = byte & 0x1Fu; }
        else if (byte >= 0xE0 && byte <= 0xEF) { continuationCount = 2; codePoint = byte & 0x0Fu; }
        else if (byte >= 0xF0 && byte <= 0xF4) { continuationCount = 3; codePoint = byte & 0x07u; }
        else return false;
        if (index + continuationCount >= length)
            return false;
        for (std::size_t continuation = 1; continuation <= continuationCount; ++continuation) {
            const auto continuationByte = static_cast<std::uint8_t>(ap_text[index + continuation]);
            if ((continuationByte & 0xC0u) != 0x80u)
                return false;
            codePoint = (codePoint << 6) | (continuationByte & 0x3Fu);
        }
        if ((continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;
        index += continuationCount + 1;
    }

    std::memcpy(ar_output.data(), ap_text, length);
    ar_length = static_cast<std::uint8_t>(length);
    return true;
}

[[nodiscard]] bool CaptureNpcAppearance(RE::Actor& a_actor, NpcAppearanceSnapshot& ar_appearance) noexcept
{
    auto* base = a_actor.GetActorBase();
    const auto* race = a_actor.GetRace();
    if (!base || !race || !IsValidFormId(race->GetFormID()) ||
        !CopyBoundedUtf8(a_actor.GetDisplayFullName(), ar_appearance.Name, ar_appearance.NameLength))
        return false;

    const auto sex = static_cast<std::int32_t>(base->GetSex());
    const auto level = a_actor.GetLevel();
    if (sex < 0 || sex > 1 || level == 0 || level > std::numeric_limits<std::uint16_t>::max() ||
        !IsFinite(base->weight) || base->weight < 0.0F || base->weight > 100.0F)
        return false;

    ar_appearance.RaceFormId = race->GetFormID();
    ar_appearance.Sex = sex;
    ar_appearance.Level = static_cast<std::uint16_t>(level);
    ar_appearance.Weight = base->weight;
    ar_appearance.Essential = a_actor.IsEssential();
    if (base->headRelatedData && base->headRelatedData->hairColor) {
        if (!IsValidFormId(base->headRelatedData->hairColor->GetFormID()))
            return false;
        ar_appearance.HairColorFormId = base->headRelatedData->hairColor->GetFormID();
    }
    if (base->headRelatedData && base->headRelatedData->faceDetails) {
        if (!IsValidFormId(base->headRelatedData->faceDetails->GetFormID()))
            return false;
        ar_appearance.FaceTextureFormId = base->headRelatedData->faceDetails->GetFormID();
    }

    for (std::size_t slot = 0; slot < kHeadPartCount; ++slot) {
        const auto* headPart = base->GetCurrentHeadPartByType(static_cast<RE::BGSHeadPart::HeadPartType>(slot));
        if (!headPart)
            continue;
        if (!IsValidFormId(headPart->GetFormID()))
            return false;
        ar_appearance.HeadParts[ar_appearance.HeadPartCount++] = {
            static_cast<std::uint8_t>(slot), headPart->GetFormID()};
    }

    if (base->faceData) {
        ar_appearance.HasFaceData = true;
        for (std::size_t index = 0; index < kFaceMorphCount; ++index) {
            const auto morph = base->faceData->morphs[index];
            if (!IsFinite(morph) || std::abs(morph) > kMaximumFaceMorphMagnitude)
                return false;
            ar_appearance.FaceMorphs[index] = morph;
        }
        for (std::size_t index = 0; index < kFacePartCount; ++index) {
            const auto part = base->faceData->parts[index];
            if (part != kFacePartDefault && (part < 0 || part > kMaximumFacePartPreset))
                return false;
            ar_appearance.FaceParts[index] = part;
        }
    }

    // Generic Actor tint storage has no verified CommonLib accessor in this bridge.
    // Leave tints absent rather than consulting desktop layout or save-buffer data.
    return true;
}

[[nodiscard]] std::uint64_t NextNpcSnapshotActionId() noexcept
{
    auto ordinal = g_nextNpcSnapshotActionOrdinal.fetch_add(1, std::memory_order_relaxed) + 1;
    ordinal &= ~GameplayBridge::kNpcSnapshotActionIdMarker;
    if (ordinal == 0)
        ordinal = 1;
    return GameplayBridge::kNpcSnapshotActionIdMarker | ordinal;
}

[[nodiscard]] bool CaptureNpcAnimation(
    RE::Actor& a_actor, AnimationGraphProtocol::SnapshotBuffer& ar_snapshot) noexcept
{
    return AnimationGraphs::Capture(a_actor, ar_snapshot);
}

[[nodiscard]] bool CaptureNpcSnapshot(RE::Actor& a_actor, NpcSnapshot& ar_snapshot) noexcept try
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
    ar_snapshot.IsDragon = a_actor.IsDragon();
    ar_snapshot.IsMount = a_actor.IsAMount();
    if (const auto commandingActor = a_actor.GetCommandingActor(); commandingActor)
        ar_snapshot.IsPlayerSummon = commandingActor->GetFormID() == 0x14;
    const auto* mapping = BridgeEndpoint::Get().Mapping();
    const auto captureAnimationGraph = mapping && HasCapability(
        mapping->Header.ActiveCapabilities.load(std::memory_order_acquire), Capability::LocalAnimationGraphSnapshot);
    if (captureAnimationGraph)
        static_cast<void>(CaptureNpcAnimation(a_actor, ar_snapshot.Animation));
    if (!CaptureNpcAppearance(a_actor, ar_snapshot.Appearance))
        return false;

    ar_snapshot.Factions.reserve(kMaximumNpcFactions);

    for (std::size_t index = 0; index < kCapturedActorValues.size(); ++index) {
        const auto value = a_actor.GetActorValue(kCapturedActorValues[index]);
        const auto maximum = a_actor.GetActorValueMax(kCapturedActorValues[index]);
        if (!IsFinite(value) || !IsFinite(maximum))
            return false;
        ar_snapshot.Values[index] = value;
        ar_snapshot.Maximums[index] = maximum;
    }

    if (!CaptureInventoryStacks(a_actor, ar_snapshot.Inventory, kMaximumNpcItems, kMaximumInventoryEffects))
        return false;

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
    if (!factionsComplete)
        return false;
    std::sort(ar_snapshot.Factions.begin(), ar_snapshot.Factions.end(),
              [](const NpcFactionEntry& acLeft, const NpcFactionEntry& acRight) noexcept {
                  return acLeft.FormId < acRight.FormId;
              });
    return std::adjacent_find(ar_snapshot.Factions.begin(), ar_snapshot.Factions.end(),
                              [](const NpcFactionEntry& acLeft, const NpcFactionEntry& acRight) noexcept {
                                  return acLeft.FormId == acRight.FormId;
                              }) == ar_snapshot.Factions.end();
}
catch (...)
{
    ar_snapshot = {};
    return false;
}

[[nodiscard]] bool PublishNpcSnapshot(const NpcSnapshot& a_snapshot) noexcept
{
    const std::scoped_lock lock{g_captureLock};
    if (!CanPublish(GameplayDomain::NpcOwnership) ||
        a_snapshot.Inventory.size() > kMaximumNpcItems || a_snapshot.Factions.size() > kMaximumNpcFactions)
        return false;
    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational())
        return false;
    const auto activeCapabilities = endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire);
    if (!HasCapability(activeCapabilities, Capability::NpcOwnership) ||
        !HasCapability(activeCapabilities, Capability::InventoryStackTransactions))
        return false;
    const auto includeAnimationGraph = HasCapability(activeCapabilities, Capability::LocalAnimationGraphSnapshot) &&
                                       AnimationGraphProtocol::IsValidCount(AnimationGraphProtocol::ValueType::BooleanBits, a_snapshot.Animation.BooleanCount) &&
                                       AnimationGraphProtocol::IsValidCount(AnimationGraphProtocol::ValueType::Float, a_snapshot.Animation.FloatCount) &&
                                       AnimationGraphProtocol::IsValidCount(AnimationGraphProtocol::ValueType::Integer, a_snapshot.Animation.IntegerCount) &&
                                       std::isfinite(a_snapshot.Animation.Direction);
    const auto actionId = NextNpcSnapshotActionId();
    const auto& appearance = a_snapshot.Appearance;
    const auto nameChunkCount = static_cast<std::size_t>(
        (appearance.NameLength + GameplayBridge::kNpcSnapshotNameChunkBytes - 1) /
        GameplayBridge::kNpcSnapshotNameChunkBytes);
    const auto appearanceRecordCount = 1 + appearance.HeadPartCount +
        (appearance.HasFaceData ? kFaceMorphCount + kFacePartCount : 0) + nameChunkCount;
    std::size_t inventoryEffectCount{};
    for (const auto& entry : a_snapshot.Inventory) {
        if (entry.Count <= 0 || entry.Effects.size() > kMaximumInventoryEffects - inventoryEffectCount)
            return false;
        inventoryEffectCount += entry.Effects.size();
    }
    const auto expectedRecordCount = 2 + appearanceRecordCount + kCapturedActorValues.size() +
                                     (includeAnimationGraph ? 1 + (a_snapshot.Animation.FloatCount + AnimationGraphProtocol::kValuesPerChunk - 1) / AnimationGraphProtocol::kValuesPerChunk + (a_snapshot.Animation.IntegerCount + AnimationGraphProtocol::kValuesPerChunk - 1) / AnimationGraphProtocol::kValuesPerChunk : 0) +
                                     a_snapshot.Inventory.size() * 2 +
                                     inventoryEffectCount + a_snapshot.Factions.size();
    if (expectedRecordCount > GameplayBridge::kMaximumNpcSnapshotRecords)
        return false;

    std::array<EventRecord, GameplayBridge::kMaximumNpcSnapshotRecords> records{};
    std::size_t recordCount{};
    auto identity = endpoint.SnapshotIdentity(0);
    identity.ActionId = actionId;
    const auto append = [&](const GameplayAction a_action, const GameplayActionPayload& a_payload) -> bool {
        if (recordCount >= expectedRecordCount)
            return false;
        auto& record = records[recordCount++];
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = identity;
        record.Payload.LocalGameplayAction = a_payload;
        record.Payload.LocalGameplayAction.Domain = static_cast<std::uint16_t>(GameplayDomain::NpcOwnership);
        record.Payload.LocalGameplayAction.Action = static_cast<std::uint16_t>(a_action);
        return true;
    };
    const auto appendGraph = [&](const AnimationGraphProtocol::ValueType a_type, const std::uint16_t a_start,
                                 const std::uint16_t a_count) -> ActorActionGraphChunkPayload* {
        if (recordCount >= expectedRecordCount)
            return nullptr;
        auto& record = records[recordCount++];
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalActorActionGraphChunk);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = identity;
        auto& payload = record.Payload.LocalActorActionGraphChunk;
        payload.ActorLocalFormId = a_snapshot.ReferenceFormId;
        payload.SnapshotId = actionId;
        payload.DescriptorVersion = AnimationGraphProtocol::kDescriptorVersion;
        payload.ValueType = static_cast<std::uint16_t>(a_type);
        payload.StartIndex = a_start;
        payload.ValueCount = a_count;
        payload.TotalCount = a_type == AnimationGraphProtocol::ValueType::BooleanBits ? a_snapshot.Animation.BooleanCount :
                             a_type == AnimationGraphProtocol::ValueType::Float ? a_snapshot.Animation.FloatCount : a_snapshot.Animation.IntegerCount;
        payload.ChunkFlags = AnimationGraphProtocol::FullSnapshot;
        payload.Direction = a_snapshot.Animation.Direction;
        return &payload;
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
    begin.ValueA = static_cast<std::int32_t>(a_snapshot.Inventory.size());
    begin.ValueB = static_cast<std::int32_t>(a_snapshot.Factions.size());
    begin.ActionFlags = (a_snapshot.Dead ? kNpcSnapshotDead : 0u) |
                        (a_snapshot.WeaponDrawn ? kNpcSnapshotWeaponDrawn : 0u) |
                        (a_snapshot.IsDragon ? kNpcSnapshotIsDragon : 0u) |
                        (a_snapshot.IsMount ? kNpcSnapshotIsMount : 0u) |
                        (a_snapshot.IsPlayerSummon ? kNpcSnapshotIsPlayerSummon : 0u) |
                        (includeAnimationGraph ? kNpcSnapshotHasAnimationGraph : 0u);
    if (!append(GameplayAction::NpcSnapshotBegin, begin))
        return false;

    if (appearance.RaceFormId == 0 || appearance.NameLength == 0 ||
        appearance.HeadPartCount > appearance.HeadParts.size())
        return false;
    GameplayActionPayload appearancePayload{};
    appearancePayload.TargetHandle = kLocalPlayerHandle;
    appearancePayload.TargetLocalFormId = a_snapshot.ReferenceFormId;
    appearancePayload.LocalFormIdA = appearance.RaceFormId;
    appearancePayload.LocalFormIdB = appearance.HairColorFormId;
    appearancePayload.LocalFormIdC = appearance.FaceTextureFormId;
    appearancePayload.LocalFormIdD = appearance.NameLength;
    appearancePayload.ValueA = appearance.Sex;
    appearancePayload.ValueB = appearance.Level;
    appearancePayload.ScalarA = appearance.Weight;
    appearancePayload.ActionFlags =
        (appearance.HasFaceData ? kNpcSnapshotAppearanceHasFaceData : 0u) |
        (appearance.Essential ? kNpcSnapshotAppearanceEssential : 0u) |
        (static_cast<std::uint32_t>(appearance.HeadPartCount) << kNpcSnapshotAppearanceHeadPartCountShift);
    if (!append(GameplayAction::NpcSnapshotAppearance, appearancePayload))
        return false;

    for (std::uint8_t index = 0; index < appearance.HeadPartCount; ++index) {
        GameplayActionPayload headPart{};
        headPart.TargetHandle = kLocalPlayerHandle;
        headPart.TargetLocalFormId = a_snapshot.ReferenceFormId;
        headPart.LocalFormIdA = appearance.HeadParts[index].second;
        headPart.ValueA = appearance.HeadParts[index].first;
        if (!append(GameplayAction::NpcSnapshotHeadPart, headPart))
            return false;
    }
    if (appearance.HasFaceData) {
        for (std::size_t index = 0; index < appearance.FaceMorphs.size(); ++index) {
            GameplayActionPayload morph{};
            morph.TargetHandle = kLocalPlayerHandle;
            morph.TargetLocalFormId = a_snapshot.ReferenceFormId;
            morph.ValueA = static_cast<std::int32_t>(index);
            morph.ScalarA = appearance.FaceMorphs[index];
            if (!append(GameplayAction::NpcSnapshotFaceMorph, morph))
                return false;
        }
        for (std::size_t index = 0; index < appearance.FaceParts.size(); ++index) {
            GameplayActionPayload part{};
            part.TargetHandle = kLocalPlayerHandle;
            part.TargetLocalFormId = a_snapshot.ReferenceFormId;
            part.ValueA = static_cast<std::int32_t>(index);
            part.ValueB = appearance.FaceParts[index];
            if (!append(GameplayAction::NpcSnapshotFacePart, part))
                return false;
        }
    }
    for (std::uint16_t offset = 0; offset < appearance.NameLength;
         offset = static_cast<std::uint16_t>(offset + GameplayBridge::kNpcSnapshotNameChunkBytes)) {
        const auto count = static_cast<std::uint8_t>(std::min<std::size_t>(
            GameplayBridge::kNpcSnapshotNameChunkBytes, appearance.NameLength - offset));
        std::array<std::uint32_t, 6> words{};
        std::memcpy(words.data(), appearance.Name.data() + offset, count);
        GameplayActionPayload name{};
        name.TargetHandle = kLocalPlayerHandle;
        name.TargetLocalFormId = a_snapshot.ReferenceFormId;
        name.LocalFormIdA = words[0];
        name.LocalFormIdB = words[1];
        name.LocalFormIdC = words[2];
        name.LocalFormIdD = words[3];
        name.ValueA = std::bit_cast<std::int32_t>(words[4]);
        name.ValueB = std::bit_cast<std::int32_t>(words[5]);
        name.ActionFlags = static_cast<std::uint32_t>(count) |
                           (static_cast<std::uint32_t>(offset / GameplayBridge::kNpcSnapshotNameChunkBytes)
                            << kNpcSnapshotNameChunkIndexShift);
        if (!append(GameplayAction::NpcSnapshotNameChunk, name))
            return false;
    }

    for (std::size_t index = 0; index < kCapturedActorValues.size(); ++index) {
        GameplayActionPayload value{};
        value.TargetHandle = kLocalPlayerHandle;
        value.TargetLocalFormId = a_snapshot.ReferenceFormId;
        value.LocalFormIdA = static_cast<std::uint32_t>(kCapturedActorValues[index]);
        value.ScalarA = a_snapshot.Values[index];
        value.ScalarB = a_snapshot.Maximums[index];
        if (!append(GameplayAction::NpcSnapshotValue, value))
            return false;
    }

    if (includeAnimationGraph) {
        auto* booleanChunk = appendGraph(AnimationGraphProtocol::ValueType::BooleanBits, 0,
                                         a_snapshot.Animation.BooleanCount);
        if (!booleanChunk)
            return false;
        for (std::size_t index = 0; index < a_snapshot.Animation.BooleanCount; ++index) {
            if (a_snapshot.Animation.Booleans[index])
                booleanChunk->Values[index / 32] |= 1u << (index % 32);
        }
        for (std::uint16_t start = 0; start < a_snapshot.Animation.FloatCount;
             start += AnimationGraphProtocol::kValuesPerChunk) {
            const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(
                AnimationGraphProtocol::kValuesPerChunk, a_snapshot.Animation.FloatCount - start));
            auto* chunk = appendGraph(AnimationGraphProtocol::ValueType::Float, start, count);
            if (!chunk)
                return false;
            for (std::uint16_t index = 0; index < count; ++index)
                chunk->Values[index] = std::bit_cast<std::uint32_t>(a_snapshot.Animation.Floats[start + index]);
        }
        for (std::uint16_t start = 0; start < a_snapshot.Animation.IntegerCount;
             start += AnimationGraphProtocol::kValuesPerChunk) {
            const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(
                AnimationGraphProtocol::kValuesPerChunk, a_snapshot.Animation.IntegerCount - start));
            auto* chunk = appendGraph(AnimationGraphProtocol::ValueType::Integer, start, count);
            if (!chunk)
                return false;
            for (std::uint16_t index = 0; index < count; ++index)
                chunk->Values[index] = std::bit_cast<std::uint32_t>(a_snapshot.Animation.Integers[start + index]);
        }
    }
    for (std::size_t itemIndex = 0; itemIndex < a_snapshot.Inventory.size(); ++itemIndex) {
        const auto& entry = a_snapshot.Inventory[itemIndex];
        GameplayActionPayload item{};
        item.TargetHandle = kLocalPlayerHandle;
        item.TargetLocalFormId = a_snapshot.ReferenceFormId;
        item.LocalFormIdA = entry.FormId;
        item.LocalFormIdB = static_cast<std::uint32_t>(a_snapshot.Inventory.size());
        item.ValueA = entry.Count;
        item.ValueB = static_cast<std::int32_t>(itemIndex);
        item.ActionFlags = entry.ItemFlags;
        if (!append(GameplayAction::NpcSnapshotItem, item))
            return false;
        GameplayActionPayload extra{};
        extra.TargetHandle = kLocalPlayerHandle;
        extra.TargetLocalFormId = a_snapshot.ReferenceFormId;
        extra.LocalFormIdA = entry.EnchantmentFormId;
        extra.LocalFormIdB = entry.PoisonFormId;
        extra.LocalFormIdC = entry.SoulLevel;
        extra.LocalFormIdD = static_cast<std::uint32_t>(entry.Effects.size());
        extra.ValueA = entry.EnchantmentCharge;
        extra.ValueB = static_cast<std::int32_t>(entry.PoisonCount);
        extra.ScalarA = entry.Charge;
        extra.ScalarB = entry.Health;
        extra.ActionFlags = entry.ExtraFlags;
        if (!append(GameplayAction::NpcSnapshotItemExtra, extra))
            return false;
        for (std::size_t effectIndex = 0; effectIndex < entry.Effects.size(); ++effectIndex) {
            const auto& effect = entry.Effects[effectIndex];
            GameplayActionPayload effectPayload{};
            effectPayload.TargetHandle = kLocalPlayerHandle;
            effectPayload.TargetLocalFormId = a_snapshot.ReferenceFormId;
            effectPayload.LocalFormIdA = effect.EffectFormId;
            effectPayload.LocalFormIdB = static_cast<std::uint32_t>(itemIndex);
            effectPayload.LocalFormIdC = static_cast<std::uint32_t>(effectIndex);
            effectPayload.LocalFormIdD = static_cast<std::uint32_t>(entry.Effects.size());
            effectPayload.ValueA = effect.Area;
            effectPayload.ValueB = effect.Duration;
            effectPayload.ScalarA = effect.Magnitude;
            effectPayload.ScalarB = effect.RawCost;
            if (!append(GameplayAction::NpcSnapshotItemEffect, effectPayload))
                return false;
        }
    }
    for (const auto& entry : a_snapshot.Factions) {
        GameplayActionPayload faction{};
        faction.TargetHandle = kLocalPlayerHandle;
        faction.TargetLocalFormId = a_snapshot.ReferenceFormId;
        faction.LocalFormIdA = entry.FormId;
        faction.ValueA = entry.Rank;
        if (!append(GameplayAction::NpcSnapshotFaction, faction))
            return false;
    }
    GameplayActionPayload end{};
    end.TargetHandle = kLocalPlayerHandle;
    end.TargetLocalFormId = a_snapshot.ReferenceFormId;
    return append(GameplayAction::NpcSnapshotEnd, end) && recordCount == expectedRecordCount &&
           endpoint.TryPushEvents(records.data(), recordCount);
}

void CaptureNpcDiscovery() noexcept try
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
catch (...)
{
}

void CaptureObservedNpcs() noexcept try
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
catch (...)
{
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

[[nodiscard]] bool PublishTintSnapshot(
    RE::PlayerCharacter& a_player, const std::vector<CapturedTint>& a_tints)
{
    const std::scoped_lock lock{g_captureLock};
    if (!CanPublish(GameplayDomain::Appearance) ||
        a_tints.size() > kMaximumAppearanceTints || !IsValidFormId(a_player.GetFormID())) {
        RecordPeriodicPublication(false);
        return false;
    }

    auto& endpoint = BridgeEndpoint::Get();
    std::vector<EventRecord> records;
    records.reserve(1 + a_tints.size() * 7);
    const auto appendAction = [&](const GameplayAction a_action, GameplayActionPayload a_payload) {
        auto& record = records.emplace_back();
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
        a_payload.TargetHandle = kLocalPlayerHandle;
        a_payload.TargetLocalFormId = a_player.GetFormID();
        a_payload.Domain = static_cast<std::uint16_t>(GameplayDomain::Appearance);
        a_payload.Action = static_cast<std::uint16_t>(a_action);
        record.Payload.LocalGameplayAction = a_payload;
    };
    appendAction(GameplayAction::ResetTints, {});
    for (std::size_t index = 0; index < a_tints.size(); ++index) {
        const auto& tint = a_tints[index];
        GameplayActionPayload payload{};
        payload.LocalFormIdB = tint.Color;
        payload.ValueA = static_cast<std::int32_t>(index);
        payload.ValueB = tint.Type;
        payload.ScalarA = std::bit_cast<float>(tint.AlphaBits);
        appendAction(GameplayAction::SetTint, payload);
        if (tint.TexturePath.empty())
            continue;

        const auto chunkCount = static_cast<std::uint16_t>(
            (tint.TexturePath.size() + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
        const auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto textId = g_nextTextId.fetch_add(1, std::memory_order_relaxed) + 1;
        for (std::uint16_t chunk = 0; chunk < chunkCount; ++chunk) {
            auto& record = records.emplace_back();
            record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayTextChunk);
            record.Header.PayloadSize = kFixedPayloadBytes;
            record.Header.Identity = endpoint.SnapshotIdentity(0);
            record.Header.Identity.ActionId = actionId;
            auto& text = record.Payload.LocalGameplayTextChunk;
            text.TargetHandle = kLocalPlayerHandle;
            text.TargetLocalFormId = a_player.GetFormID();
            text.Domain = static_cast<std::uint16_t>(GameplayDomain::Appearance);
            text.Action = static_cast<std::uint16_t>(GameplayAction::SetTint);
            text.TextId = textId;
            text.ChunkIndex = chunk;
            text.ChunkCount = chunkCount;
            text.Reserved0 = kGameplayTextAppearanceDeferred;
            text.AuxiliaryLocalFormId = static_cast<std::uint32_t>(index) + 1;
            const auto offset = static_cast<std::size_t>(chunk) * kGameplayTextBytesPerChunk;
            text.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
                kGameplayTextBytesPerChunk, tint.TexturePath.size() - offset));
            std::memcpy(text.Utf8Bytes, tint.TexturePath.data() + offset, text.ByteCount);
        }
    }
    const auto accepted = endpoint.TryPushEvents(records.data(), records.size());
    RecordPeriodicPublication(accepted);
    return accepted;
}

[[nodiscard]] bool CaptureWornEquipment(
    RE::PlayerCharacter& a_player, std::vector<WornEquipmentEntry>& ar_entries) noexcept
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

[[nodiscard]] bool PublishInventoryTransactionChunk(
    const std::uint32_t a_ownerFormId, const CapturedInventoryStack& ac_mutation,
    const bool a_drop) noexcept try
{
    if (!CanPublish(GameplayDomain::Inventory) || !IsValidFormId(a_ownerFormId) ||
        !IsValidFormId(ac_mutation.FormId) || !IsBoundedInventoryDelta(ac_mutation.Count) ||
        (ac_mutation.ItemFlags & ~kInventoryTransactionItemKnownFlags) != 0 ||
        (ac_mutation.ExtraFlags & ~kInventoryTransactionExtraKnownFlags) != 0 ||
        ac_mutation.Effects.size() > kMaximumInventoryEffects)
        return false;

    const auto recordCount = 4 + ac_mutation.Effects.size();
    if (recordCount > GameplayBridge::kMaximumInventoryTransactionRecords)
        return false;

    auto& endpoint = BridgeEndpoint::Get();
    if (!endpoint.IsOperational() || !HasCapability(
                                      endpoint.Mapping()->Header.ActiveCapabilities.load(std::memory_order_acquire),
                                      Capability::InventoryStackTransactions))
        return false;

    auto actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    if (actionId == 0)
        actionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
    std::vector<EventRecord> records;
    records.reserve(recordCount);
    auto identity = endpoint.SnapshotIdentity(0);
    identity.ActionId = actionId;
    const auto append = [&](const GameplayAction a_action, GameplayActionPayload a_payload) {
        EventRecord record{};
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = identity;
        a_payload.TargetHandle = kLocalPlayerHandle;
        a_payload.TargetLocalFormId = a_ownerFormId;
        a_payload.Domain = static_cast<std::uint16_t>(GameplayDomain::Inventory);
        a_payload.Action = static_cast<std::uint16_t>(a_action);
        record.Payload.LocalGameplayAction = a_payload;
        records.push_back(record);
    };

    GameplayActionPayload begin{};
    begin.ValueA = 1;
    append(GameplayAction::InventoryTransactionBegin, begin);

    GameplayActionPayload item{};
    item.LocalFormIdA = ac_mutation.FormId;
    item.LocalFormIdB = 1;
    item.ValueA = ac_mutation.Count;
    item.ActionFlags = ac_mutation.ItemFlags |
        (a_drop && ac_mutation.Count < 0 ? kInventoryDrop : 0u);
    append(GameplayAction::InventoryTransactionItem, item);

    GameplayActionPayload extra{};
    extra.LocalFormIdA = ac_mutation.EnchantmentFormId;
    extra.LocalFormIdB = ac_mutation.PoisonFormId;
    extra.LocalFormIdC = ac_mutation.SoulLevel;
    extra.LocalFormIdD = static_cast<std::uint32_t>(ac_mutation.Effects.size());
    extra.ValueA = ac_mutation.EnchantmentCharge;
    extra.ValueB = static_cast<std::int32_t>(ac_mutation.PoisonCount);
    extra.ScalarA = ac_mutation.Charge;
    extra.ScalarB = ac_mutation.Health;
    extra.ActionFlags = ac_mutation.ExtraFlags;
    append(GameplayAction::InventoryTransactionItemExtra, extra);
    for (std::size_t effectIndex = 0; effectIndex < ac_mutation.Effects.size(); ++effectIndex) {
        const auto& effect = ac_mutation.Effects[effectIndex];
        GameplayActionPayload effectPayload{};
        effectPayload.LocalFormIdA = effect.EffectFormId;
        effectPayload.LocalFormIdC = static_cast<std::uint32_t>(effectIndex);
        effectPayload.LocalFormIdD = static_cast<std::uint32_t>(ac_mutation.Effects.size());
        effectPayload.ValueA = effect.Area;
        effectPayload.ValueB = effect.Duration;
        effectPayload.ScalarA = effect.Magnitude;
        effectPayload.ScalarB = effect.RawCost;
        append(GameplayAction::InventoryTransactionItemEffect, effectPayload);
    }
    append(GameplayAction::InventoryTransactionEnd, {});
    if (records.size() != recordCount)
        return false;
    const auto accepted = endpoint.TryPushEvents(records.data(), records.size());
    RecordPeriodicPublication(accepted);
    return accepted;
}
catch (...)
{
    return false;
}

[[nodiscard]] bool PublishInventoryTransaction(
    const std::uint32_t a_ownerFormId, const std::vector<CapturedInventoryStack>& ac_mutations,
    const bool a_drop = false, std::vector<CapturedInventoryStack>* ap_publishedMutations = nullptr) noexcept try
{
    if (ac_mutations.empty() || ac_mutations.size() > kMaximumInventoryEntries)
        return false;

    std::size_t chunkCount{};
    for (const auto& mutation : ac_mutations) {
        if (!IsValidFormId(mutation.FormId) || !IsCapturedInventoryDelta(mutation.Count) ||
            (mutation.ItemFlags & ~kInventoryTransactionItemKnownFlags) != 0 ||
            (mutation.ExtraFlags & ~kInventoryTransactionExtraKnownFlags) != 0 ||
            mutation.Effects.size() > kMaximumInventoryEffects)
            return false;
        const auto magnitude = mutation.Count < 0 ? -static_cast<std::int64_t>(mutation.Count) : mutation.Count;
        chunkCount += static_cast<std::size_t>((magnitude + kMaximumInventoryDelta - 1) / kMaximumInventoryDelta);
    }
    if (ap_publishedMutations &&
        chunkCount > ap_publishedMutations->max_size() - ap_publishedMutations->size())
        return false;
    if (ap_publishedMutations)
        ap_publishedMutations->reserve(ap_publishedMutations->size() + chunkCount);

    for (const auto& mutation : ac_mutations) {
        auto remaining = mutation.Count < 0 ? -static_cast<std::int64_t>(mutation.Count) : mutation.Count;
        const auto sign = mutation.Count < 0 ? -1 : 1;
        while (remaining != 0) {
            auto chunk = mutation;
            const auto magnitude = static_cast<std::int32_t>(std::min<std::int64_t>(remaining, kMaximumInventoryDelta));
            chunk.Count = sign * magnitude;
            if (!PublishInventoryTransactionChunk(a_ownerFormId, chunk, a_drop))
                return false;
            if (ap_publishedMutations)
                ap_publishedMutations->push_back(std::move(chunk));
            remaining -= magnitude;
        }
    }
    return true;
}
catch (...)
{
    return false;
}

void ApplyCapturedInventoryMutation(
    std::vector<CapturedInventoryStack>& ar_stacks, const CapturedInventoryStack& ac_mutation)
{
    const auto existing = std::find_if(ar_stacks.begin(), ar_stacks.end(), [&ac_mutation](const auto& ac_stack) {
        return ac_stack.IsSameMetadata(ac_mutation);
    });
    if (existing == ar_stacks.end()) {
        if (ac_mutation.Count > 0)
            ar_stacks.push_back(ac_mutation);
        return;
    }
    const auto count = static_cast<std::int64_t>(existing->Count) + ac_mutation.Count;
    if (count <= 0) {
        ar_stacks.erase(existing);
    } else if (count <= std::numeric_limits<std::int32_t>::max()) {
        existing->Count = static_cast<std::int32_t>(count);
    }
}

[[nodiscard]] bool ReplaceContainerInventoryBaseline(
    const std::uint32_t a_ownerFormId, const std::vector<CapturedInventoryStack>& ac_stacks) noexcept try
{
    if (a_ownerFormId == 0 || ac_stacks.size() > kMaximumInventoryEntries)
        return false;

    std::size_t effectCount{};
    for (const auto& stack : ac_stacks) {
        if (!IsValidFormId(stack.FormId) || stack.Count <= 0 || stack.Count > kMaximumCapturedInventoryCount ||
            stack.Effects.size() > kMaximumInventoryEffects - effectCount)
            return false;
        effectCount += stack.Effects.size();
    }

    // Copy before evicting so an allocation failure leaves the old bounded
    // baseline intact. Lowest form ID eviction makes pressure deterministic.
    auto replacement = ac_stacks;
    auto existing = g_containerInventoryBaselines.find(a_ownerFormId);
    if (existing != g_containerInventoryBaselines.end()) {
        existing->second = std::move(replacement);
        return true;
    }
    if (g_containerInventoryBaselines.size() >= kMaximumContainerInventoryBaselines)
        g_containerInventoryBaselines.erase(g_containerInventoryBaselines.begin());
    g_containerInventoryBaselines.emplace(a_ownerFormId, std::move(replacement));
    return true;
}
catch (...)
{
    return false;
}

void ApplyAcceptedInventoryMutationToBaseline(
    const std::uint32_t a_ownerFormId,
    const std::vector<CapturedInventoryStack>& ac_acceptedMutations) noexcept
{
    if (ac_acceptedMutations.empty())
        return;

    const auto* player = RE::PlayerCharacter::GetSingleton();
    if (player && a_ownerFormId == player->GetFormID()) {
        if (g_snapshot.InventoryCaptured) {
            for (const auto& mutation : ac_acceptedMutations)
                ApplyCapturedInventoryMutation(g_snapshot.Inventory, mutation);
        }

        if (g_assignmentInventorySeed.Valid) {
            const auto identity = BridgeEndpoint::Get().SnapshotIdentity(0);
            if (identity.ServerInstanceNonce == g_assignmentInventorySeed.ServerInstanceNonce &&
                identity.ConnectionGeneration == g_assignmentInventorySeed.ConnectionGeneration &&
                identity.LifecycleEpoch == g_assignmentInventorySeed.LifecycleEpoch) {
                for (const auto& mutation : ac_acceptedMutations)
                    ApplyCapturedInventoryMutation(g_assignmentInventorySeed.Inventory, mutation);
            }
        }
        return;
    }

    if (RE::TESForm::LookupByID<RE::Actor>(a_ownerFormId) != nullptr)
        return;

    if (const auto baseline = g_containerInventoryBaselines.find(a_ownerFormId);
        baseline != g_containerInventoryBaselines.end()) {
        for (const auto& mutation : ac_acceptedMutations)
            ApplyCapturedInventoryMutation(baseline->second, mutation);
    }
}

[[nodiscard]] bool PublishInventoryDifferences(
    const std::uint32_t a_ownerFormId,
    const std::vector<CapturedInventoryStack>& ac_baseline,
    const std::vector<CapturedInventoryStack>& ac_captured,
    std::vector<CapturedInventoryStack>& ar_published,
    bool& ar_publishedAny) noexcept try
{
    ar_published = ac_baseline;
    std::vector<bool> previousMatched(ac_baseline.size());
    std::vector<bool> currentMatched(ac_captured.size());
    const auto publish = [&](const std::vector<CapturedInventoryStack>& ac_mutations) {
        std::vector<CapturedInventoryStack> accepted;
        const auto complete = PublishInventoryTransaction(a_ownerFormId, ac_mutations, false, &accepted);
        for (const auto& mutation : accepted)
            ApplyCapturedInventoryMutation(ar_published, mutation);
        ar_publishedAny = ar_publishedAny || !accepted.empty();
        return complete;
    };

    // Exact metadata matches are count-only updates. Metadata replacements
    // remain ordered removals followed by additions.
    for (std::size_t previousIndex = 0; previousIndex < ac_baseline.size(); ++previousIndex) {
        for (std::size_t currentIndex = 0; currentIndex < ac_captured.size(); ++currentIndex) {
            if (currentMatched[currentIndex] || !ac_baseline[previousIndex].IsSameMetadata(ac_captured[currentIndex]))
                continue;
            previousMatched[previousIndex] = true;
            currentMatched[currentIndex] = true;
            const auto delta = static_cast<std::int64_t>(ac_captured[currentIndex].Count) -
                static_cast<std::int64_t>(ac_baseline[previousIndex].Count);
            if (delta != 0) {
                if (delta < -kMaximumCapturedInventoryCount || delta > kMaximumCapturedInventoryCount)
                    return false;
                auto mutation = ac_captured[currentIndex];
                mutation.Count = static_cast<std::int32_t>(delta);
                if (!publish({mutation}))
                    return false;
            }
            break;
        }
    }

    for (std::size_t previousIndex = 0; previousIndex < ac_baseline.size(); ++previousIndex) {
        if (previousMatched[previousIndex])
            continue;
        const auto current = std::find_if(ac_captured.begin(), ac_captured.end(), [&](const auto& ac_stack) {
            const auto index = static_cast<std::size_t>(std::addressof(ac_stack) - ac_captured.data());
            return !currentMatched[index] && ac_stack.FormId == ac_baseline[previousIndex].FormId;
        });
        if (current == ac_captured.end())
            continue;
        const auto currentIndex = static_cast<std::size_t>(current - ac_captured.begin());
        auto removal = ac_baseline[previousIndex];
        removal.Count = -removal.Count;
        if (!publish({removal}) || !publish({*current}))
            return false;
        previousMatched[previousIndex] = true;
        currentMatched[currentIndex] = true;
    }
    for (std::size_t previousIndex = 0; previousIndex < ac_baseline.size(); ++previousIndex) {
        if (previousMatched[previousIndex])
            continue;
        auto removal = ac_baseline[previousIndex];
        removal.Count = -removal.Count;
        if (!publish({removal}))
            return false;
    }
    for (std::size_t currentIndex = 0; currentIndex < ac_captured.size(); ++currentIndex) {
        if (currentMatched[currentIndex])
            continue;
        if (!publish({ac_captured[currentIndex]}))
            return false;
    }
    return true;
}
catch (...)
{
    return false;
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

void ScheduleInventoryReconciliation(const std::uint32_t a_ownerFormId) noexcept
{
    if (a_ownerFormId != 0 &&
        (g_pendingInventoryReconciliations.contains(a_ownerFormId) ||
         g_pendingInventoryReconciliations.size() < kMaximumObservedNpcs))
        g_pendingInventoryReconciliations.insert(a_ownerFormId);
}

[[nodiscard]] bool IsNonPlayerActorInventoryOwner(
    const std::uint32_t a_ownerFormId, const RE::PlayerCharacter* ap_player) noexcept
{
    const auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_ownerFormId);
    return actor != nullptr && actor != ap_player;
}

void ScheduleNpcInventorySnapshot(const std::uint32_t a_ownerFormId, const RE::PlayerCharacter* ap_player) noexcept
{
    if (IsNonPlayerActorInventoryOwner(a_ownerFormId, ap_player) &&
        (g_observedNpcReferences.contains(a_ownerFormId) ||
         g_observedNpcReferences.size() < kMaximumObservedNpcs))
        g_observedNpcReferences.insert(a_ownerFormId);
}

void RefreshContainerInventoryBaselineFromOwner(const std::uint32_t a_ownerFormId) noexcept
{
    auto* reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ownerFormId);
    const auto* base = reference ? reference->GetBaseObject() : nullptr;
    if (!reference || !base || base->GetFormType() != RE::FormType::Container)
        return;

    std::vector<CapturedInventoryStack> captured;
    if (CaptureInventoryStacks(*reference, captured))
        static_cast<void>(ReplaceContainerInventoryBaseline(a_ownerFormId, captured));
}

void OnContainerChangedEvent(const RE::TESContainerChangedEvent& a_event) noexcept
{
    try {
        if (IsRemoteInventorySuppressed())
            return;
        if (a_event.itemCount <= 0 || a_event.itemCount > kMaximumCapturedInventoryCount ||
            !IsValidFormId(a_event.baseObj) ||
            (a_event.oldContainer == 0 && a_event.newContainer == 0))
            return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        const bool oldNpcOwner = IsNonPlayerActorInventoryOwner(a_event.oldContainer, player);
        const bool newNpcOwner = IsNonPlayerActorInventoryOwner(a_event.newContainer, player);
        if (oldNpcOwner)
            ScheduleNpcInventorySnapshot(a_event.oldContainer, player);
        if (newNpcOwner)
            ScheduleNpcInventorySnapshot(a_event.newContainer, player);

        auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_event.baseObj);
        const auto reference = a_event.reference.get();
        CapturedInventoryStack stack{};
        const auto captureFromOwner = [&](const std::uint32_t a_ownerFormId) {
            if (a_event.uniqueID == 0)
                return false;
            auto* owner = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ownerFormId);
            if (!owner)
                return false;
            std::vector<CapturedInventoryStack> stacks;
            if (!CaptureInventoryStacks(*owner, stacks))
                return false;
            const auto candidate = std::find_if(stacks.begin(), stacks.end(), [&](const auto& ac_stack) {
                return ac_stack.FormId == a_event.baseObj && ac_stack.SourceUniqueId == a_event.uniqueID;
            });
            if (candidate == stacks.end() ||
                std::find_if(std::next(candidate), stacks.end(), [&](const auto& ac_stack) {
                    return ac_stack.FormId == a_event.baseObj && ac_stack.SourceUniqueId == a_event.uniqueID;
                }) != stacks.end())
                return false;
            stack = *candidate;
            stack.Count = a_event.itemCount;
            return true;
        };
        const auto capturedFromReference = object && reference && reference->GetBaseObject() == object &&
            CaptureInventoryStack(*object, a_event.itemCount, std::addressof(reference->extraList), stack) &&
            (a_event.uniqueID == 0 || stack.SourceUniqueId == a_event.uniqueID);
        if (!capturedFromReference &&
            !(a_event.newContainer != 0 && captureFromOwner(a_event.newContainer)) &&
            !(a_event.oldContainer != 0 && captureFromOwner(a_event.oldContainer))) {
            ScheduleInventoryReconciliation(a_event.oldContainer);
            ScheduleInventoryReconciliation(a_event.newContainer);
            return;
        }

        const auto publishForOwner = [&](const std::uint32_t a_ownerFormId,
                                         const CapturedInventoryStack& ac_mutation,
                                         const bool a_drop) {
            if (a_ownerFormId == 0 || IsNonPlayerActorInventoryOwner(a_ownerFormId, player))
                return;
            std::vector<CapturedInventoryStack> accepted;
            const auto complete = PublishInventoryTransaction(a_ownerFormId, {ac_mutation}, a_drop, &accepted);
            ApplyAcceptedInventoryMutationToBaseline(a_ownerFormId, accepted);
            if (!complete) {
                ScheduleInventoryReconciliation(a_ownerFormId);
                return;
            }
            RefreshContainerInventoryBaselineFromOwner(a_ownerFormId);
        };

        if (a_event.oldContainer != 0 && !oldNpcOwner) {
            auto removal = stack;
            removal.Count = -removal.Count;
            const auto isActorDrop = a_event.newContainer == 0 &&
                RE::TESForm::LookupByID<RE::Actor>(a_event.oldContainer) != nullptr;
            publishForOwner(a_event.oldContainer, removal, isActorDrop);
        }
        if (a_event.newContainer != 0 && !newNpcOwner)
            publishForOwner(a_event.newContainer, stack, false);
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

void OnGrabReleaseEvent(const RE::TESGrabReleaseEvent& a_event) noexcept
{
    try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* reference = a_event.ref.get();
        if (!player || !reference || !IsValidFormId(reference->GetFormID()))
            return;

        auto* base = reference->GetBaseObject();
        auto* cell = reference->GetParentCell();
        auto* worldspace = reference->GetWorldspace();
        if (!base || !cell || !IsValidFormId(cell->GetFormID()) ||
            (worldspace && !IsValidFormId(worldspace->GetFormID())))
            return;

        const auto position = reference->GetPosition();
        if (!IsFinite(position.x) || !IsFinite(position.y) || !IsFinite(position.z))
            return;

        GameplayActionPayload payload{};
        if (!PreparePlayerPayload(payload, *player))
            return;
        payload.LocalFormIdA = reference->GetFormID();
        payload.LocalFormIdB = cell->GetFormID();
        payload.LocalFormIdC = worldspace ? worldspace->GetFormID() : 0;
        payload.ValueA = static_cast<std::int32_t>(base->GetFormType());
        payload.ScalarA = position.x;
        payload.ScalarB = position.y;
        payload.ScalarC = position.z;
        Publish(GameplayDomain::Higgs,
                a_event.grabbed ? GameplayAction::HiggsGrab : GameplayAction::HiggsDrop,
                payload);
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
LocalSink<RE::TESGrabReleaseEvent, OnGrabReleaseEvent> g_grabReleaseSink;
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
    holder->AddEventSink(&g_grabReleaseSink);
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

void CaptureAppearance(RE::PlayerCharacter& a_player, Snapshot& a_current)
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

    const auto hairColorFormId = base->headRelatedData && base->headRelatedData->hairColor &&
                                     IsValidFormId(base->headRelatedData->hairColor->GetFormID()) ?
        base->headRelatedData->hairColor->GetFormID() : 0;
    if (!g_snapshot.HairColorCaptured || hairColorFormId != g_snapshot.HairColorFormId) {
        GameplayActionPayload payload{};
        if (PreparePlayerPayload(payload, a_player)) {
            payload.LocalFormIdA = hairColorFormId;
            if (Publish(GameplayDomain::Appearance, GameplayAction::SetHairColor, payload)) {
                a_current.HairColorFormId = hairColorFormId;
                a_current.HairColorCaptured = true;
            }
        }
    } else {
        a_current.HairColorFormId = hairColorFormId;
        a_current.HairColorCaptured = true;
    }

    const auto faceTextureFormId = base->headRelatedData && base->headRelatedData->faceDetails &&
                                       IsValidFormId(base->headRelatedData->faceDetails->GetFormID()) ?
        base->headRelatedData->faceDetails->GetFormID() : 0;
    if (!g_snapshot.FaceTextureCaptured || faceTextureFormId != g_snapshot.FaceTextureFormId) {
        GameplayActionPayload payload{};
        if (PreparePlayerPayload(payload, a_player)) {
            payload.LocalFormIdA = faceTextureFormId;
            if (Publish(GameplayDomain::Appearance, GameplayAction::SetFaceTexture, payload)) {
                a_current.FaceTextureFormId = faceTextureFormId;
                a_current.FaceTextureCaptured = true;
            }
        }
    } else {
        a_current.FaceTextureFormId = faceTextureFormId;
        a_current.FaceTextureCaptured = true;
    }

    const bool hasFaceData = base->faceData != nullptr;
    if (g_snapshot.FaceDataPresenceCaptured && g_snapshot.FaceDataPresent && !hasFaceData) {
        GameplayActionPayload payload{};
        if (PreparePlayerPayload(payload, a_player) &&
            Publish(GameplayDomain::Appearance, GameplayAction::ResetFaceData, payload)) {
            a_current.FaceDataPresent = false;
            a_current.FaceDataPresenceCaptured = true;
        }
    } else {
        a_current.FaceDataPresent = hasFaceData;
        a_current.FaceDataPresenceCaptured = true;
    }

    if (hasFaceData) {
        for (std::size_t index = 0; index < kFaceMorphCount; ++index) {
            const auto morph = base->faceData->morphs[index];
            if (!IsFinite(morph) || std::abs(morph) > kMaximumFaceMorphMagnitude)
                continue;
            const auto bits = std::bit_cast<std::uint32_t>(morph);
            if (!g_snapshot.FaceMorphCaptured[index] || bits != g_snapshot.FaceMorphBits[index]) {
                GameplayActionPayload payload{};
                if (PreparePlayerPayload(payload, a_player)) {
                    payload.ValueA = static_cast<std::int32_t>(index);
                    payload.ScalarA = morph;
                    if (Publish(GameplayDomain::Appearance, GameplayAction::SetFaceMorph, payload)) {
                        a_current.FaceMorphBits[index] = bits;
                        a_current.FaceMorphCaptured[index] = true;
                    }
                }
            } else {
                a_current.FaceMorphBits[index] = bits;
                a_current.FaceMorphCaptured[index] = true;
            }
        }
        for (std::size_t index = 0; index < kFacePartCount; ++index) {
            const auto part = base->faceData->parts[index];
            if (part != kFacePartDefault && (part < 0 || part > kMaximumFacePartPreset))
                continue;
            if (!g_snapshot.FacePartCaptured[index] || part != g_snapshot.FaceParts[index]) {
                GameplayActionPayload payload{};
                if (PreparePlayerPayload(payload, a_player)) {
                    payload.ValueA = static_cast<std::int32_t>(index);
                    payload.ValueB = part;
                    if (Publish(GameplayDomain::Appearance, GameplayAction::SetFacePart, payload)) {
                        a_current.FaceParts[index] = part;
                        a_current.FacePartCaptured[index] = true;
                    }
                }
            } else {
                a_current.FaceParts[index] = part;
                a_current.FacePartCaptured[index] = true;
            }
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
        if (!headPart || !IsValidFormId(headPart->GetFormID())) {
            if (g_snapshot.Valid && g_snapshot.HeadParts[index] != 0) {
                GameplayActionPayload payload{};
                if (PreparePlayerPayload(payload, a_player)) {
                    payload.ValueA = static_cast<std::int32_t>(index);
                    if (!Publish(GameplayDomain::Appearance, GameplayAction::ClearHeadPart, payload))
                        a_current.HeadParts[index] = g_snapshot.HeadParts[index];
                }
            }
            continue;
        }
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

    std::vector<CapturedTint> tints;
    const auto* runtime = a_player.GetVRPlayerRuntimeData();
    if (!runtime)
        return;
    const auto* tintMasks = runtime->overlayTintMasks ? runtime->overlayTintMasks : std::addressof(runtime->tintMasks);
    if (!tintMasks)
        return;
    tints.reserve(std::min<std::vector<CapturedTint>::size_type>(tintMasks->size(), kMaximumAppearanceTints));
    for (const auto* tint : *tintMasks) {
        if (!tint || static_cast<std::uint32_t>(tint->type.get()) >= kTintTypeCount ||
            !IsFinite(tint->alpha) || tint->alpha < 0.0F || tint->alpha > 1.0F)
            continue;
        std::string texturePath;
        if (tint->texture && tint->texture->textureName.c_str()) {
            const auto* path = tint->texture->textureName.c_str();
            const auto length = std::char_traits<char>::length(path);
            if (length > kMaximumAppearanceTexturePathBytes)
                continue;
            texturePath.assign(path, length);
        }
        if (!IsSafeTintTexturePath(texturePath))
            continue;
        const auto color = (static_cast<std::uint32_t>(tint->color.red) << 16) |
                           (static_cast<std::uint32_t>(tint->color.green) << 8) |
                           static_cast<std::uint32_t>(tint->color.blue);
        tints.push_back({color, std::bit_cast<std::uint32_t>(tint->alpha),
                         static_cast<std::uint8_t>(tint->type.get()), std::move(texturePath)});
        if (tints.size() >= kMaximumAppearanceTints)
            break;
    }
    if (tints != g_snapshot.Tints) {
        if (PublishTintSnapshot(a_player, tints))
            a_current.Tints = std::move(tints);
    } else {
        a_current.Tints = std::move(tints);
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

    const auto* actorState = a_player.AsActorState();
    const auto dead = a_player.IsDead() || (actorState && actorState->IsBleedingOut());
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

void CaptureInventory(RE::PlayerCharacter& a_player, Snapshot& a_current) noexcept try
{
    std::vector<CapturedInventoryStack> captured;
    if (!CaptureInventoryStacks(a_player, captured))
        return;

    const auto baseline = g_snapshot.InventoryCaptured ? g_snapshot.Inventory :
        decltype(g_snapshot.Inventory){};
    std::vector<CapturedInventoryStack> published;
    bool publishedAny{};
    const auto complete = PublishInventoryDifferences(
        a_player.GetFormID(), baseline, captured, published, publishedAny);
    a_current.Inventory = std::move(published);
    a_current.InventoryCaptured = g_snapshot.InventoryCaptured || complete || publishedAny;
}
catch (...)
{
}

void CapturePendingInventoryReconciliations(RE::PlayerCharacter& a_player) noexcept try
{
    if (g_pendingInventoryReconciliations.empty())
        return;
    const auto playerFormId = a_player.GetFormID();
    bool refreshObjects{};
    std::vector<std::uint32_t> retry;
    retry.reserve(g_pendingInventoryReconciliations.size());
    for (const auto ownerFormId : g_pendingInventoryReconciliations) {
        if (ownerFormId == playerFormId)
            continue;
        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(ownerFormId); actor &&
            actor != std::addressof(a_player) &&
            (g_observedNpcReferences.contains(ownerFormId) ||
             g_observedNpcReferences.size() < kMaximumObservedNpcs))
        {
            g_observedNpcReferences.insert(ownerFormId);
            continue;
        }
        auto* reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(ownerFormId);
        const auto* base = reference ? reference->GetBaseObject() : nullptr;
        if (!reference || !base || base->GetFormType() != RE::FormType::Container)
            continue;

        const auto baseline = g_containerInventoryBaselines.find(ownerFormId);
        if (baseline == g_containerInventoryBaselines.end()) {
            refreshObjects = true;
            continue;
        }

        std::vector<CapturedInventoryStack> captured;
        if (!CaptureInventoryStacks(*reference, captured)) {
            retry.push_back(ownerFormId);
            continue;
        }
        std::vector<CapturedInventoryStack> published;
        bool publishedAny{};
        const auto complete = PublishInventoryDifferences(
            ownerFormId, baseline->second, captured, published, publishedAny);
        if (complete)
            static_cast<void>(ReplaceContainerInventoryBaseline(ownerFormId, captured));
        else if (publishedAny)
            static_cast<void>(ReplaceContainerInventoryBaseline(ownerFormId, published));
        if (!complete)
            retry.push_back(ownerFormId);
    }
    g_pendingInventoryReconciliations.clear();
    for (const auto ownerFormId : retry)
        ScheduleInventoryReconciliation(ownerFormId);
    if (refreshObjects) {
        g_cellObjectSnapshot = {};
        g_lastObjectCellFormId = 0;
    }
}
catch (...)
{
    g_pendingInventoryReconciliations.clear();
}

void CaptureWorldObjects(RE::PlayerCharacter& a_player)
{
    auto* cell = a_player.GetParentCell();
    if (!cell || !IsValidFormId(cell->GetFormID()) || !CanPublish(GameplayDomain::Object))
        return;
    const auto cellFormId = cell->GetFormID();
    if (g_cellObjectSnapshot.Valid && g_cellObjectSnapshot.CellFormId != cellFormId)
        g_cellObjectSnapshot = {};
    if (!g_cellObjectSnapshot.Valid && cellFormId == g_lastObjectCellFormId)
        return;

    if (!g_cellObjectSnapshot.Valid) {
        CellObjectSnapshot capture{};
        capture.CellFormId = cellFormId;
        const auto* loaded = cell->GetRuntimeData().loadedData;
        const auto encounterZoneId = loaded && loaded->encounterZone ? loaded->encounterZone->GetFormID() : 0;
        const bool playerHome = encounterZoneId == 0x000F90B1 && cellFormId != 0x000EEC55;
        const auto* worldspace = cell->GetRuntimeData().worldSpace;
        const auto worldspaceId = worldspace ? worldspace->GetFormID() : 0;
        bool captureFailed{};

        cell->ForEachReference([&](RE::TESObjectREFR* a_reference) {
            if (captureFailed)
                return RE::BSContainer::ForEachResult::kStop;
            try {
                if (!a_reference || !IsValidFormId(a_reference->GetFormID()) ||
                    a_reference->GetFormID() == 0x00039CF1 || a_reference->GetFormID() == 0x0003EF03)
                    return RE::BSContainer::ForEachResult::kContinue;
                const auto* base = a_reference->GetBaseObject();
                if (!base || (base->GetFormType() != RE::FormType::Container &&
                              base->GetFormType() != RE::FormType::Door))
                    return RE::BSContainer::ForEachResult::kContinue;

                ObjectSnapshotTransaction objectSnapshot{};
                objectSnapshot.ReferenceFormId = a_reference->GetFormID();
                objectSnapshot.CellFormId = cellFormId;
                objectSnapshot.WorldspaceFormId = worldspaceId;
                objectSnapshot.OpenState = base->GetFormType() == RE::FormType::Door ?
                    static_cast<std::int32_t>(RE::BGSOpenCloseForm::GetOpenState(a_reference)) : 0;
                objectSnapshot.LockLevel = a_reference->IsLocked() ?
                    static_cast<std::int32_t>(a_reference->GetLockLevel()) : -1;
                objectSnapshot.Position = a_reference->GetPosition();
                if (!IsFinite(objectSnapshot.Position.x) || !IsFinite(objectSnapshot.Position.y) ||
                    !IsFinite(objectSnapshot.Position.z) || objectSnapshot.OpenState < 0 ||
                    objectSnapshot.OpenState > 2 || objectSnapshot.LockLevel < -1 ||
                    objectSnapshot.LockLevel > 255) {
                    captureFailed = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                objectSnapshot.Flags =
                    (base->GetFormType() == RE::FormType::Container ? kObjectSnapshotContainer : 0u) |
                    (playerHome ? kObjectSnapshotPlayerHome : 0u);

                if (base->GetFormType() == RE::FormType::Container &&
                    !CaptureInventoryStacks(*a_reference, objectSnapshot.Inventory,
                                            kMaximumInventoryEntries, kMaximumInventoryEffects)) {
                    captureFailed = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                for (const auto& item : objectSnapshot.Inventory)
                    objectSnapshot.EffectCount += item.Effects.size();
                if (objectSnapshot.Inventory.size() > kMaximumInventoryEntries ||
                    objectSnapshot.EffectCount > kMaximumInventoryEffects) {
                    captureFailed = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                capture.Objects.push_back(std::move(objectSnapshot));
                return RE::BSContainer::ForEachResult::kContinue;
            } catch (...) {
                captureFailed = true;
                return RE::BSContainer::ForEachResult::kStop;
            }
        });
        if (captureFailed)
            return;
        std::sort(capture.Objects.begin(), capture.Objects.end(), [](const auto& acLeft, const auto& acRight) {
            return acLeft.ReferenceFormId < acRight.ReferenceFormId;
        });
        capture.Valid = true;
        g_cellObjectSnapshot = std::move(capture);
    }

    auto& endpoint = BridgeEndpoint::Get();
    constexpr std::size_t kPublicationBudget = 256;
    std::size_t published{};
    const auto push = [&](const GameplayAction aAction,
                          const ObjectSnapshotTransaction& acObject,
                          GameplayActionPayload aPayload) {
        EventRecord record{};
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayAction);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = endpoint.SnapshotIdentity(0);
        record.Header.Identity.ActionId = acObject.ActionId;
        aPayload.TargetLocalFormId = acObject.ReferenceFormId;
        aPayload.Domain = static_cast<std::uint16_t>(GameplayDomain::Object);
        aPayload.Action = static_cast<std::uint16_t>(aAction);
        record.Payload.LocalGameplayAction = aPayload;
        return endpoint.TryPushEvent(record);
    };

    while (g_cellObjectSnapshot.ObjectIndex < g_cellObjectSnapshot.Objects.size() &&
           published < kPublicationBudget) {
        auto& object = g_cellObjectSnapshot.Objects[g_cellObjectSnapshot.ObjectIndex];
        if (object.ActionId == 0) {
            object.ActionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
            if (object.ActionId == 0)
                object.ActionId = g_nextActionId.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        if (!object.BeginPublished) {
            GameplayActionPayload begin{};
            begin.LocalFormIdA = object.CellFormId;
            begin.LocalFormIdB = object.WorldspaceFormId;
            begin.LocalFormIdC = static_cast<std::uint32_t>(object.Inventory.size());
            begin.ValueA = object.OpenState;
            begin.ValueB = object.LockLevel;
            begin.ScalarA = object.Position.x;
            begin.ScalarB = object.Position.y;
            begin.ScalarC = object.Position.z;
            begin.ActionFlags = object.Flags;
            if (!push(GameplayAction::ObjectSnapshotBegin, object, begin))
                return;
            object.BeginPublished = true;
            ++published;
            continue;
        }
        if (object.ItemIndex < object.Inventory.size()) {
            const auto& item = object.Inventory[object.ItemIndex];
            if (object.ItemStage == 0) {
                GameplayActionPayload payload{};
                payload.LocalFormIdA = item.FormId;
                payload.LocalFormIdB = static_cast<std::uint32_t>(object.Inventory.size());
                payload.ValueA = item.Count;
                payload.ValueB = static_cast<std::int32_t>(object.ItemIndex);
                payload.ActionFlags = ToAssignmentInventoryFlags(item.ItemFlags);
                if (!push(GameplayAction::ObjectSnapshotItem, object, payload))
                    return;
                object.ItemStage = 1;
            } else if (object.ItemStage == 1) {
                GameplayActionPayload payload{};
                payload.LocalFormIdA = item.EnchantmentFormId;
                payload.LocalFormIdB = item.PoisonFormId;
                payload.LocalFormIdC = item.SoulLevel;
                payload.LocalFormIdD = static_cast<std::uint32_t>(item.Effects.size());
                payload.ValueA = item.EnchantmentCharge;
                payload.ValueB = static_cast<std::int32_t>(item.PoisonCount);
                payload.ScalarA = item.Charge;
                payload.ScalarB = item.Health;
                payload.ActionFlags = ToAssignmentInventoryExtraFlags(item.ExtraFlags);
                if (!push(GameplayAction::ObjectSnapshotItemExtra, object, payload))
                    return;
                object.ItemStage = 2;
            } else if (object.EffectIndex < item.Effects.size()) {
                const auto& effect = item.Effects[object.EffectIndex];
                GameplayActionPayload payload{};
                payload.LocalFormIdA = effect.EffectFormId;
                payload.LocalFormIdB = static_cast<std::uint32_t>(object.ItemIndex);
                payload.LocalFormIdC = static_cast<std::uint32_t>(object.EffectIndex);
                payload.LocalFormIdD = static_cast<std::uint32_t>(item.Effects.size());
                payload.ValueA = effect.Area;
                payload.ValueB = effect.Duration;
                payload.ScalarA = effect.Magnitude;
                payload.ScalarB = effect.RawCost;
                if (!push(GameplayAction::ObjectSnapshotItemEffect, object, payload))
                    return;
                ++object.EffectIndex;
            } else {
                ++object.ItemIndex;
                object.EffectIndex = 0;
                object.ItemStage = 0;
                continue;
            }
            ++published;
            continue;
        }

        GameplayActionPayload end{};
        end.LocalFormIdA = static_cast<std::uint32_t>(object.Inventory.size());
        if (!push(GameplayAction::ObjectSnapshotEnd, object, end))
            return;
        if ((object.Flags & kObjectSnapshotContainer) != 0)
            static_cast<void>(ReplaceContainerInventoryBaseline(object.ReferenceFormId, object.Inventory));
        ++published;
        ++g_cellObjectSnapshot.ObjectIndex;
    }

    if (g_cellObjectSnapshot.ObjectIndex == g_cellObjectSnapshot.Objects.size()) {
        g_lastObjectCellFormId = g_cellObjectSnapshot.CellFormId;
        g_cellObjectSnapshot = {};
    }
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
    g_cellObjectSnapshot = {};
    g_lastNpcDiscoveryAt = {};
    g_lastNpcObservationAt = {};
    g_lastNpcDiscoveryCellFormId = 0;
    g_npcDiscoveryOffset = 0;
    g_npcObservationOffset = 0;
    g_observedNpcReferences.clear();
    g_pendingInventoryReconciliations.clear();
    g_containerInventoryBaselines.clear();
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
        const auto identity = BridgeEndpoint::Get().SnapshotIdentity(0);
        if (g_assignmentInventorySeed.Valid &&
            (identity.ServerInstanceNonce != g_assignmentInventorySeed.ServerInstanceNonce ||
             identity.ConnectionGeneration != g_assignmentInventorySeed.ConnectionGeneration ||
             identity.LifecycleEpoch < g_assignmentInventorySeed.LifecycleEpoch))
            g_assignmentInventorySeed = {};

        const bool seedBaseline = g_assignmentInventorySeed.Valid &&
            identity.ServerInstanceNonce == g_assignmentInventorySeed.ServerInstanceNonce &&
            identity.ConnectionGeneration == g_assignmentInventorySeed.ConnectionGeneration &&
            identity.LifecycleEpoch >= g_assignmentInventorySeed.LifecycleEpoch;
        ResetCaptureBaselinesUnlocked();
        if (seedBaseline) {
            g_snapshot.Inventory = std::move(g_assignmentInventorySeed.Inventory);
            g_snapshot.InventoryCaptured = true;
            g_assignmentInventorySeed = {};
        }
        g_armed.store(true, std::memory_order_release);
    } catch (...) {
    }
}

ScopedRemoteInventorySuppression::ScopedRemoteInventorySuppression() noexcept
{
    ++g_remoteInventorySuppressionDepth;
}

ScopedRemoteInventorySuppression::~ScopedRemoteInventorySuppression() noexcept
{
    if (g_remoteInventorySuppressionDepth != 0)
        --g_remoteInventorySuppressionDepth;
}

bool IsRemoteInventorySuppressed() noexcept
{
    return g_remoteInventorySuppressionDepth != 0;
}

void RefreshInventoryBaseline(const std::uint32_t a_ownerFormId) noexcept
{
    if (a_ownerFormId == 0)
        return;
    try {
        const std::scoped_lock lock{g_captureLock};
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && a_ownerFormId == player->GetFormID()) {
            std::vector<CapturedInventoryStack> captured;
            if (CaptureInventoryStacks(*player, captured)) {
                g_snapshot.Inventory = std::move(captured);
                g_snapshot.InventoryCaptured = true;
            }
            return;
        }

        if (RE::TESForm::LookupByID<RE::Actor>(a_ownerFormId) != nullptr)
            return;

        auto* reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ownerFormId);
        const auto* base = reference ? reference->GetBaseObject() : nullptr;
        if (!reference || !base || base->GetFormType() != RE::FormType::Container)
            return;
        std::vector<CapturedInventoryStack> captured;
        if (CaptureInventoryStacks(*reference, captured))
            static_cast<void>(ReplaceContainerInventoryBaseline(a_ownerFormId, captured));
    } catch (...) {
    }
}

GameplayBridge::CommandStatus CaptureAssignmentBootstrap(
    const GameplayBridge::CommandRecord& acCommand) noexcept
{
    const auto requestId = acCommand.Payload.CaptureAssignmentBootstrap.RequestId;
    const auto publishFailure = [&](const CommandStatus aStatus,
                                    const AssignmentBootstrapFailureReason aReason) noexcept {
        EventRecord record{};
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::AssignmentBootstrapRecord);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = acCommand.Header.Identity;
        auto& payload = record.Payload.AssignmentBootstrapRecord;
        payload.TargetHandle = kLocalPlayerHandle;
        payload.RequestId = requestId;
        payload.RecordKind = static_cast<std::uint16_t>(AssignmentBootstrapRecordKind::Failure);
        payload.ValueA = static_cast<std::int32_t>(aStatus);
        payload.ValueB = static_cast<std::int32_t>(aReason);
        payload.TotalRecords = 1;
        return BridgeEndpoint::Get().TryPushEvent(record);
    };

    try {
        const std::scoped_lock lock{g_captureLock};
        if (!g_initialized)
            InitializeUnlocked();

        if (g_assignmentBootstrapPublication.Active) {
            static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                             AssignmentBootstrapFailureReason::Capacity));
            return CommandStatus::QueueOverflow;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetActorBase()) {
            static_cast<void>(publishFailure(CommandStatus::Inactive, AssignmentBootstrapFailureReason::Unavailable));
            return CommandStatus::Inactive;
        }

        auto& publication = g_assignmentBootstrapPublication;
        publication.Reset();
        publication.Records.reserve(kMaximumAssignmentBootstrapEvents);
        EventRecord discardedRecord{};
        bool overflow{};
        const auto append = [&](const AssignmentBootstrapRecordKind aKind) -> AssignmentBootstrapRecordPayload& {
            if (publication.Count >= VRAssignmentLimits::kMaximumLogicalBootstrapRecords) {
                overflow = true;
                return discardedRecord.Payload.AssignmentBootstrapRecord;
            }
            auto& record = publication.Records.emplace_back();
            ++publication.Count;
            record = {};
            record.Header.Kind = static_cast<std::uint16_t>(EventKind::AssignmentBootstrapRecord);
            record.Header.PayloadSize = kFixedPayloadBytes;
            record.Header.Identity = acCommand.Header.Identity;
            auto& payload = record.Payload.AssignmentBootstrapRecord;
            payload.TargetHandle = kLocalPlayerHandle;
            payload.RequestId = requestId;
            payload.RecordKind = static_cast<std::uint16_t>(aKind);
            return payload;
        };
        const auto appendText = [&]() -> EventRecord& {
            if (publication.Count >= VRAssignmentLimits::kMaximumLogicalBootstrapRecords) {
                overflow = true;
                return discardedRecord;
            }
            auto& record = publication.Records.emplace_back();
            ++publication.Count;
            record = {};
            return record;
        };

    auto& begin = append(AssignmentBootstrapRecordKind::Begin);
    begin.LocalFormIdA = player->GetFormID();
    begin.LocalFormIdB = player->GetActorBase()->GetFormID();

    auto& state = append(AssignmentBootstrapRecordKind::ActorState);
    state.ValueA = player->GetLevel();
    state.RecordFlags = (player->IsDead() ? kAssignmentBootstrapDead : 0u) |
        ((player->AsActorState() && player->AsActorState()->IsWeaponDrawn()) ?
             kAssignmentBootstrapWeaponDrawn : 0u);

    for (const auto actorValue : kAssignmentActorValues) {
        const auto value = player->GetActorValue(actorValue);
        const auto maximum = player->GetActorValueMax(actorValue);
        if (!IsFinite(value) || !IsFinite(maximum)) {
            static_cast<void>(publishFailure(CommandStatus::EngineRejected,
                                             AssignmentBootstrapFailureReason::EssentialActorValues));
            return CommandStatus::EngineRejected;
        }
        auto& actorValueRecord = append(AssignmentBootstrapRecordKind::ActorValue);
        actorValueRecord.LocalFormIdA = static_cast<std::uint32_t>(actorValue);
        actorValueRecord.ScalarA = value;
        actorValueRecord.ScalarB = maximum;
    }

    std::vector<CapturedInventoryStack> inventory;
    if (!CaptureInventoryStacks(*player, inventory)) {
        static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                         AssignmentBootstrapFailureReason::Capacity));
        return CommandStatus::QueueOverflow;
    }
    for (const auto& entry : inventory) {
        auto& item = append(AssignmentBootstrapRecordKind::InventoryEntry);
        item.LocalFormIdA = entry.FormId;
        item.ValueA = entry.Count;
        item.RecordFlags = ToAssignmentInventoryFlags(entry.ItemFlags);

        auto& extra = append(AssignmentBootstrapRecordKind::InventoryExtra);
        extra.LocalFormIdA = entry.EnchantmentFormId;
        extra.LocalFormIdB = entry.PoisonFormId;
        extra.LocalFormIdC = entry.SoulLevel;
        extra.LocalFormIdD = static_cast<std::uint32_t>(entry.Effects.size());
        extra.ValueA = entry.EnchantmentCharge;
        extra.ValueB = static_cast<std::int32_t>(entry.PoisonCount);
        extra.ScalarA = entry.Charge;
        extra.ScalarB = entry.Health;
        extra.RecordFlags = ToAssignmentInventoryExtraFlags(entry.ExtraFlags);
        for (const auto& capturedEffect : entry.Effects) {
            auto& effect = append(AssignmentBootstrapRecordKind::InventoryEffect);
            effect.LocalFormIdA = capturedEffect.EffectFormId;
            effect.ValueA = capturedEffect.Area;
            effect.ValueB = capturedEffect.Duration;
            effect.ScalarA = capturedEffect.Magnitude;
            effect.ScalarB = capturedEffect.RawCost;
        }
    }

    const auto magic = CaptureSelectedMagicEquipment(*player);
    auto& magicRecord = append(AssignmentBootstrapRecordKind::MagicEquipment);
    magicRecord.LocalFormIdA = magic.LeftSpellFormId;
    magicRecord.LocalFormIdB = magic.RightSpellFormId;
    magicRecord.LocalFormIdC = magic.PowerOrShoutFormId;

    const auto* runtime = player->GetVRPlayerRuntimeData();
    if (!runtime) {
        static_cast<void>(publishFailure(CommandStatus::EngineRejected,
                                         AssignmentBootstrapFailureReason::Unavailable));
        return CommandStatus::EngineRejected;
    }

    std::vector<std::pair<std::uint32_t, std::uint16_t>> quests;
    quests.reserve(VRAssignmentLimits::kMaximumQuestEntries);
    const auto objectiveCount = runtime->objectives.size();
    for (std::uint32_t index = 0; index < objectiveCount; ++index) {
        const auto& objective = runtime->objectives[index];
        const auto* quest = objective.Objective ? objective.Objective->ownerQuest : nullptr;
        if (!quest || !IsSyncableQuest(*quest) || !IsValidFormId(quest->GetFormID()))
            continue;
        if (std::any_of(quests.begin(), quests.end(), [quest](const auto& acQuest) {
                return acQuest.first == quest->GetFormID();
            }))
            continue;
        if (quests.size() >= VRAssignmentLimits::kMaximumQuestEntries) {
            static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                             AssignmentBootstrapFailureReason::Capacity));
            return CommandStatus::QueueOverflow;
        }
        quests.emplace_back(quest->GetFormID(), quest->GetCurrentStageID());
    }
    std::sort(quests.begin(), quests.end());
    quests.erase(std::unique(quests.begin(), quests.end(), [](const auto& acLeft, const auto& acRight) {
        return acLeft.first == acRight.first;
    }), quests.end());
    for (const auto& [formId, stage] : quests) {
        auto& quest = append(AssignmentBootstrapRecordKind::Quest);
        quest.LocalFormIdA = formId;
        quest.ValueA = stage;
    }

    const auto appendFaction = [&](const AssignmentBootstrapRecordKind aKind,
                                   const RE::FACTION_RANK& acFaction) {
        if (!acFaction.faction || !IsValidFormId(acFaction.faction->GetFormID()))
            return;
        auto& faction = append(aKind);
        faction.LocalFormIdA = acFaction.faction->GetFormID();
        faction.ValueA = acFaction.rank;
    };
    if (player->GetActorBase()->factions.size() > kMaximumAssignmentFactionEntries) {
        static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                         AssignmentBootstrapFailureReason::Capacity));
        return CommandStatus::QueueOverflow;
    }
    for (const auto& faction : player->GetActorBase()->factions)
        appendFaction(AssignmentBootstrapRecordKind::NpcFaction, faction);
    if (const auto* changes = player->extraList.GetByType<RE::ExtraFactionChanges>()) {
        if (changes->factionChanges.size() > kMaximumAssignmentFactionEntries) {
            static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                             AssignmentBootstrapFailureReason::Capacity));
            return CommandStatus::QueueOverflow;
        }
        for (const auto& faction : changes->factionChanges)
            appendFaction(AssignmentBootstrapRecordKind::ExtraFaction, faction);
    }

    auto* actorBase = player->GetActorBase();
    const auto* race = player->GetRace();
    const auto sex = static_cast<std::int32_t>(actorBase->GetSex());
    const auto level = player->GetLevel();
    auto hairColorFormId = actorBase->headRelatedData && actorBase->headRelatedData->hairColor ?
        actorBase->headRelatedData->hairColor->GetFormID() : 0;
    auto faceTextureFormId = actorBase->headRelatedData && actorBase->headRelatedData->faceDetails ?
        actorBase->headRelatedData->faceDetails->GetFormID() : 0;
    if (!race || !IsValidFormId(race->GetFormID()) || sex < 0 || sex > 1 ||
        !IsFinite(actorBase->weight) || actorBase->weight < 0.0F || actorBase->weight > 100.0F ||
        level <= 0 || level > std::numeric_limits<std::uint16_t>::max()) {
        static_cast<void>(publishFailure(CommandStatus::EngineRejected,
                                         AssignmentBootstrapFailureReason::AppearanceCore));
        return CommandStatus::EngineRejected;
    }
    if (hairColorFormId != 0 && !IsValidFormId(hairColorFormId))
        hairColorFormId = 0;
    if (faceTextureFormId != 0 && !IsValidFormId(faceTextureFormId))
        faceTextureFormId = 0;

    std::array<float, kFaceMorphCount> faceMorphs{};
    std::array<std::int32_t, kFacePartCount> faceParts{};
    bool hasFaceData = actorBase->faceData != nullptr;
    if (hasFaceData) {
        for (std::size_t index = 0; index < kFaceMorphCount; ++index) {
            faceMorphs[index] = actorBase->faceData->morphs[index];
            if (!IsFinite(faceMorphs[index]) || std::abs(faceMorphs[index]) > kMaximumFaceMorphMagnitude) {
                hasFaceData = false;
                break;
            }
        }
        if (hasFaceData)
            for (std::size_t index = 0; index < kFacePartCount; ++index) {
                faceParts[index] = actorBase->faceData->parts[index];
                if (faceParts[index] != kFacePartDefault &&
                    (faceParts[index] < 0 || faceParts[index] > kMaximumFacePartPreset)) {
                    hasFaceData = false;
                    break;
                }
            }
    }

    auto& appearance = append(AssignmentBootstrapRecordKind::AppearanceCore);
    appearance.LocalFormIdA = race->GetFormID();
    appearance.LocalFormIdB = hairColorFormId;
    appearance.LocalFormIdC = faceTextureFormId;
    appearance.ValueA = sex;
    appearance.ValueB = level;
    appearance.ScalarA = actorBase->weight;
    appearance.RecordFlags = (hasFaceData ? kAssignmentBootstrapHasFaceData : 0u) |
        (player->IsEssential() ? kAssignmentBootstrapEssential : 0u);

    if (hasFaceData) {
        for (std::size_t index = 0; index < kFaceMorphCount; ++index) {
            auto& record = append(AssignmentBootstrapRecordKind::FaceMorph);
            record.ValueA = static_cast<std::int32_t>(index);
            record.ScalarA = faceMorphs[index];
        }
        for (std::size_t index = 0; index < kFacePartCount; ++index) {
            auto& record = append(AssignmentBootstrapRecordKind::FacePart);
            record.ValueA = static_cast<std::int32_t>(index);
            record.ValueB = faceParts[index];
        }
    }

    for (std::size_t slot = 0; slot < kHeadPartCount; ++slot) {
        const auto type = static_cast<RE::BGSHeadPart::HeadPartType>(slot);
        const auto* headPart = actorBase->GetCurrentHeadPartByType(type);
        if (!headPart)
            continue;
        if (!IsValidFormId(headPart->GetFormID()))
            continue;
        auto& record = append(AssignmentBootstrapRecordKind::HeadPart);
        record.LocalFormIdA = headPart->GetFormID();
        record.ValueA = static_cast<std::int32_t>(slot);
    }

    const auto* displayName = player->GetDisplayFullName();
    std::size_t nameLength{};
    if (displayName) {
        while (nameLength <= kMaximumAppearanceNameBytes && displayName[nameLength] != '\0')
            ++nameLength;
    }
    if (nameLength == 0 || nameLength > kMaximumAppearanceNameBytes) {
        static_cast<void>(publishFailure(CommandStatus::EngineRejected,
                                         AssignmentBootstrapFailureReason::Name));
        return CommandStatus::EngineRejected;
    }
    const auto nameChunkCount = static_cast<std::uint16_t>(
        (nameLength + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
    for (std::uint16_t chunk = 0; chunk < nameChunkCount; ++chunk) {
        auto& record = appendText();
        record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayTextChunk);
        record.Header.PayloadSize = kFixedPayloadBytes;
        record.Header.Identity = acCommand.Header.Identity;
        auto& text = record.Payload.LocalGameplayTextChunk;
        text.TargetHandle = kLocalPlayerHandle;
        text.TargetLocalFormId = player->GetFormID();
        text.Domain = static_cast<std::uint16_t>(GameplayDomain::Appearance);
        text.Action = static_cast<std::uint16_t>(GameplayAction::SetName);
        text.TextId = requestId;
        text.ChunkIndex = chunk;
        text.ChunkCount = nameChunkCount;
        text.Reserved0 = kGameplayTextAppearanceDeferred;
        const auto offset = static_cast<std::size_t>(chunk) * kGameplayTextBytesPerChunk;
        text.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
            kGameplayTextBytesPerChunk, nameLength - offset));
        std::memcpy(text.Utf8Bytes, displayName + offset, text.ByteCount);
    }

    const auto* tintMasks = runtime->overlayTintMasks ? runtime->overlayTintMasks : std::addressof(runtime->tintMasks);
    if (tintMasks) {
        std::size_t emittedTintIndex{};
        for (std::size_t index = 0; index < tintMasks->size(); ++index) {
            const auto* tint = (*tintMasks)[index];
            if (!tint || static_cast<std::uint32_t>(tint->type.get()) >= kTintTypeCount ||
                !IsFinite(tint->alpha) || tint->alpha < 0.0F || tint->alpha > 1.0F)
                continue;
            std::string_view texturePath{};
            if (tint->texture && tint->texture->textureName.c_str())
                texturePath = tint->texture->textureName.c_str();
            if (!IsSafeTintTexturePath(texturePath))
                continue;
            // The wire format caps tints at 32. Preserve the runtime order so
            // heavily modded saves always produce the same bounded subset.
            if (emittedTintIndex >= kMaximumAppearanceTints)
                break;
            auto& tintRecord = append(AssignmentBootstrapRecordKind::Tint);
            tintRecord.LocalFormIdB = static_cast<std::uint32_t>(emittedTintIndex++);
            tintRecord.LocalFormIdA = (static_cast<std::uint32_t>(tint->color.red) << 16) |
                                      (static_cast<std::uint32_t>(tint->color.green) << 8) |
                                      static_cast<std::uint32_t>(tint->color.blue);
            tintRecord.ValueA = static_cast<std::int32_t>(tint->type.get());
            tintRecord.ScalarA = tint->alpha;
            if (texturePath.empty())
                continue;
            tintRecord.RecordFlags |= kAssignmentBootstrapTintHasTexturePath;
            const auto chunkCount = static_cast<std::uint16_t>(
                (texturePath.size() + kGameplayTextBytesPerChunk - 1) / kGameplayTextBytesPerChunk);
            for (std::uint16_t chunk = 0; chunk < chunkCount; ++chunk) {
                auto& record = appendText();
                record.Header.Kind = static_cast<std::uint16_t>(EventKind::LocalGameplayTextChunk);
                record.Header.PayloadSize = kFixedPayloadBytes;
                record.Header.Identity = acCommand.Header.Identity;
                auto& text = record.Payload.LocalGameplayTextChunk;
                text.TargetHandle = kLocalPlayerHandle;
                text.TargetLocalFormId = player->GetFormID();
                text.Domain = static_cast<std::uint16_t>(GameplayDomain::Appearance);
                text.Action = static_cast<std::uint16_t>(GameplayAction::SetTint);
                text.TextId = requestId;
                text.ChunkIndex = chunk;
                text.ChunkCount = chunkCount;
                text.Reserved0 = kGameplayTextAppearanceDeferred;
                text.AuxiliaryLocalFormId = static_cast<std::uint32_t>(emittedTintIndex);
                const auto offset = static_cast<std::size_t>(chunk) * kGameplayTextBytesPerChunk;
                text.ByteCount = static_cast<std::uint16_t>(std::min<std::size_t>(
                    kGameplayTextBytesPerChunk, texturePath.size() - offset));
                std::memcpy(text.Utf8Bytes, texturePath.data() + offset, text.ByteCount);
            }
        }
    }

    append(AssignmentBootstrapRecordKind::End);
    if (overflow || publication.Count > VRAssignmentLimits::kMaximumLogicalBootstrapRecords) {
        publication.Reset();
        static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                         AssignmentBootstrapFailureReason::Capacity));
        return CommandStatus::QueueOverflow;
    }

    const auto assignmentRecordCount = static_cast<std::uint32_t>(std::count_if(
        publication.Records.begin(), publication.Records.end(), [](const EventRecord& acRecord) {
            return acRecord.Header.Kind == static_cast<std::uint16_t>(EventKind::AssignmentBootstrapRecord);
        }));
    if (assignmentRecordCount == 0 ||
        assignmentRecordCount > VRAssignmentLimits::kMaximumLogicalBootstrapRecords) {
        publication.Reset();
        static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                         AssignmentBootstrapFailureReason::Capacity));
        return CommandStatus::QueueOverflow;
    }
    std::uint32_t ordinal{};
    for (auto record = publication.Records.begin();
         record != publication.Records.end(); ++record) {
        if (record->Header.Kind != static_cast<std::uint16_t>(EventKind::AssignmentBootstrapRecord))
            continue;
        auto& payload = record->Payload.AssignmentBootstrapRecord;
        payload.Ordinal = ordinal++;
        payload.TotalRecords = assignmentRecordCount;
    }
    publication.ServerInstanceNonce = acCommand.Header.Identity.ServerInstanceNonce;
    publication.ConnectionGeneration = acCommand.Header.Identity.ConnectionGeneration;
    publication.LifecycleEpoch = acCommand.Header.Identity.LifecycleEpoch;
    publication.Inventory = std::move(inventory);
    publication.Next = 0;
    publication.Active = true;
    return CommandStatus::Success;
    } catch (const std::bad_alloc&) {
        g_assignmentBootstrapPublication.Reset();
        static_cast<void>(publishFailure(CommandStatus::QueueOverflow,
                                         AssignmentBootstrapFailureReason::Capacity));
        return CommandStatus::QueueOverflow;
    } catch (...) {
        g_assignmentBootstrapPublication.Reset();
        static_cast<void>(publishFailure(CommandStatus::EngineRejected,
                                         AssignmentBootstrapFailureReason::Exception));
        return GameplayBridge::CommandStatus::EngineRejected;
    }
}

[[nodiscard]] bool PublishPendingAssignmentBootstrap() noexcept
{
    auto& publication = g_assignmentBootstrapPublication;
    if (!publication.Active)
        return true;

    auto& endpoint = BridgeEndpoint::Get();
    const auto identity = endpoint.SnapshotIdentity(0);
    if (identity.ServerInstanceNonce != publication.ServerInstanceNonce ||
        identity.ConnectionGeneration != publication.ConnectionGeneration ||
        identity.LifecycleEpoch != publication.LifecycleEpoch || publication.Next >= publication.Count) {
        publication.Reset();
        return false;
    }

    const auto pageCount = std::min(VRAssignmentLimits::kBootstrapPageRecords, publication.Count - publication.Next);
    if (!endpoint.TryPushEvents(publication.Records.data() + publication.Next, pageCount))
        return false;

    publication.Next += pageCount;
    if (publication.Next != publication.Count)
        return true;

    g_assignmentInventorySeed = {
        publication.ServerInstanceNonce,
        publication.ConnectionGeneration,
        publication.LifecycleEpoch,
        std::move(publication.Inventory),
        true,
    };
    publication.Reset();
    return true;
}

void CapturePeriodic() noexcept
{
    try {
        const std::scoped_lock lock{g_captureLock};
        // Keep pages contiguous and give bootstrap priority over ordinary
        // capture so the ring can always make forward progress.
        if (g_assignmentBootstrapPublication.Active) {
            static_cast<void>(PublishPendingAssignmentBootstrap());
            return;
        }
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
        GameplayActionPayload appearanceCommit{};
        if (PreparePlayerPayload(appearanceCommit, *player))
            Publish(GameplayDomain::Appearance, GameplayAction::CommitAppearance, appearanceCommit);
        CaptureExperience(*player, current);
        CaptureInventory(*player, current);
        CapturePendingInventoryReconciliations(*player);
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
                holder->RemoveEventSink(&g_grabReleaseSink);
                holder->RemoveEventSink(&g_playerBowShotSink);
                holder->RemoveEventSink(&g_questStartStopSink);
                holder->RemoveEventSink(&g_questStageSink);
            }
            g_scriptSinksRegistered = false;
        }

        g_initialized = false;
        g_assignmentBootstrapPublication.Reset();
        g_assignmentInventorySeed = {};
        ResetCaptureBaselinesUnlocked();
    } catch (...) {
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::LocalGameplayCapture
