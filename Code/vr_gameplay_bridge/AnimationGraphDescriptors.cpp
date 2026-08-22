#include "pch.h"

#include "AnimationGraphDescriptors.h"

#include <vr_common/VRHandoffPath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>

namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
{
namespace
{
#include "AnimationGraphDescriptors.generated.inc"

constexpr std::size_t kMaximumCachedManagers = 64;

struct CacheEntry
{
    const RE::BSAnimationGraphManager* Manager{};
    const AnimationGraphDescriptor* Descriptor{};
};

std::array<CacheEntry, kMaximumCachedManagers> g_cache{};
std::size_t g_nextEviction{};
std::mutex g_cacheLock;

struct RuntimeDescriptor
{
    const ConfiguredAnimationGraphDescriptor* Source{};
    AnimationGraphDescriptor Descriptor{};
    std::vector<std::string> BooleanStorage;
    std::vector<std::string> FloatStorage;
    std::vector<std::string> IntegerStorage;
    std::vector<std::string_view> Booleans;
    std::vector<std::string_view> Floats;
    std::vector<std::string_view> Integers;
};

std::once_flag g_configurationOnce;
std::vector<ConfiguredAnimationGraphDescriptor> g_configuredDescriptors;
std::mutex g_runtimeDescriptorLock;
std::vector<std::unique_ptr<RuntimeDescriptor>> g_runtimeDescriptors;

[[nodiscard]] bool Probe(RE::Actor& a_actor, const AnimationGraphDescriptor& a_descriptor) noexcept;

void LoadConfiguration() noexcept
{
    try
    {
        g_configuredDescriptors = LoadConfiguredAnimationGraphDescriptors(SkyrimTogetherVR::Handoff::GetGameDirectory() / "Data" / "SkyrimTogetherRebornBehaviors");
    }
    catch (...)
    {
        g_configuredDescriptors.clear();
    }
}

[[nodiscard]] bool HasGraphVariable(RE::Actor& a_actor, const std::string_view a_name) noexcept
{
    bool boolean{};
    float floating{};
    std::int32_t integer{};
    const RE::BSFixedString name(a_name);
    return a_actor.GetGraphVariableBool(name, boolean) || a_actor.GetGraphVariableFloat(name, floating) || a_actor.GetGraphVariableInt(name, integer);
}

[[nodiscard]] bool MatchesSignature(RE::Actor& a_actor, const std::string_view a_signature) noexcept
{
    if (a_signature.empty())
        return false;
    std::size_t offset{};
    while (offset < a_signature.size())
    {
        const auto comma = a_signature.find(',', offset);
        const auto token = a_signature.substr(offset, comma == std::string_view::npos ? a_signature.size() - offset : comma - offset);
        const bool negated = !token.empty() && token.front() == '!';
        const auto name = negated ? token.substr(1) : token;
        if (name.empty() || HasGraphVariable(a_actor, name) == negated)
            return false;
        if (comma == std::string_view::npos)
            return true;
        offset = comma + 1;
    }
    return false;
}

[[nodiscard]] const AnimationGraphDescriptor* FindCatalogDescriptor(const std::uint64_t a_key) noexcept
{
    for (const auto& descriptor : kDescriptors)
        if (descriptor.Key == a_key)
            return &descriptor;
    return nullptr;
}

[[nodiscard]] bool AppendUnique(std::vector<std::string>& ar_destination,
                                const std::span<const std::string_view> a_base,
                                const std::vector<std::string>& a_configured)
{
    ar_destination.reserve(a_base.size() + a_configured.size());
    for (const auto name : a_base)
        if (std::find(ar_destination.begin(), ar_destination.end(), name) == ar_destination.end())
            ar_destination.emplace_back(name);
    const auto configuredBegin = ar_destination.end();
    for (const auto& name : a_configured) {
        if (name.empty())
            return false;
        if (std::find(configuredBegin, ar_destination.end(), name) != ar_destination.end())
            return false;
        if (std::find(ar_destination.begin(), configuredBegin, name) == configuredBegin)
            ar_destination.push_back(name);
    }
    return true;
}

void MakeViews(const std::vector<std::string>& a_storage, std::vector<std::string_view>& ar_views)
{
    ar_views.reserve(a_storage.size());
    for (const auto& name : a_storage)
        ar_views.emplace_back(name);
}

[[nodiscard]] bool BuildContract(AnimationGraphDescriptor& ar_descriptor) noexcept
{
    const auto direction = std::find(ar_descriptor.Floats.begin(), ar_descriptor.Floats.end(), "Direction");
    if (direction == ar_descriptor.Floats.end())
        return false;
    const auto directionIndex = static_cast<std::uint16_t>(direction - ar_descriptor.Floats.begin());
    if (!AnimationGraphProtocol::IsValidDescriptorContract(
            ar_descriptor.Booleans.size(), ar_descriptor.Floats.size(), ar_descriptor.Integers.size(), 1,
            directionIndex))
        return false;
    ar_descriptor.DirectionFloatIndex = directionIndex;
    ar_descriptor.Digest = AnimationGraphProtocol::ComputeDescriptorDigest(
        ar_descriptor.Booleans, ar_descriptor.Floats, ar_descriptor.Integers, directionIndex);
    return ar_descriptor.Digest != 0;
}

[[nodiscard]] const AnimationGraphDescriptor* ResolveConfiguredDescriptor(RE::Actor& a_actor) noexcept
{
    std::call_once(g_configurationOnce, LoadConfiguration);
    std::size_t signatureMatches{};
    for (const auto& configured : g_configuredDescriptors) {
        if (!MatchesSignature(a_actor, configured.Signature))
            continue;
        if (++signatureMatches > 1)
            return nullptr;
    }
    for (const auto& configured : g_configuredDescriptors)
    {
        if (!MatchesSignature(a_actor, configured.Signature))
            continue;

        std::scoped_lock lock{g_runtimeDescriptorLock};
        for (const auto& runtime : g_runtimeDescriptors)
            if (runtime->Source == &configured && Probe(a_actor, runtime->Descriptor))
                return &runtime->Descriptor;

        auto runtime = std::make_unique<RuntimeDescriptor>();
        runtime->Source = &configured;
        const auto* base = configured.OriginalKey != 0 ? FindCatalogDescriptor(configured.OriginalKey) : nullptr;
        if (!AppendUnique(runtime->BooleanStorage, base ? base->Booleans : std::span<const std::string_view>{}, configured.Booleans) ||
            !AppendUnique(runtime->FloatStorage, base ? base->Floats : std::span<const std::string_view>{}, configured.Floats) ||
            !AppendUnique(runtime->IntegerStorage, base ? base->Integers : std::span<const std::string_view>{}, configured.Integers))
            continue;
        MakeViews(runtime->BooleanStorage, runtime->Booleans);
        MakeViews(runtime->FloatStorage, runtime->Floats);
        MakeViews(runtime->IntegerStorage, runtime->Integers);
        runtime->Descriptor = {configured.OriginalKey, runtime->Booleans, runtime->Floats, runtime->Integers};
        if (!BuildContract(runtime->Descriptor) || !Probe(a_actor, runtime->Descriptor))
            continue;
        const auto* descriptor = &runtime->Descriptor;
        g_runtimeDescriptors.push_back(std::move(runtime));
        return descriptor;
    }
    return nullptr;
}

[[nodiscard]] const AnimationGraphDescriptor* FindCachedDescriptor(
    const RE::BSAnimationGraphManager* a_manager) noexcept
{
    std::scoped_lock lock{g_cacheLock};
    for (auto& entry : g_cache)
        if (entry.Manager == a_manager)
            return entry.Descriptor;
    return nullptr;
}

void StoreCacheEntry(const RE::BSAnimationGraphManager* a_manager,
                     const AnimationGraphDescriptor* a_descriptor) noexcept
{
    std::scoped_lock lock{g_cacheLock};
    for (auto& entry : g_cache) {
        if (!entry.Manager) {
            entry = {a_manager, a_descriptor};
            return;
        }
    }

    g_cache[g_nextEviction] = {a_manager, a_descriptor};
    g_nextEviction = (g_nextEviction + 1) % g_cache.size();
}

void InvalidateCacheEntry(const RE::BSAnimationGraphManager* a_manager) noexcept
{
    std::scoped_lock lock{g_cacheLock};
    for (auto& entry : g_cache) {
        if (entry.Manager == a_manager) {
            entry = {};
            return;
        }
    }
}

[[nodiscard]] bool Probe(RE::Actor& a_actor, const AnimationGraphDescriptor& a_descriptor) noexcept
{
    bool boolean{}; float floating{}; std::int32_t integer{};
    for (const auto name : a_descriptor.Booleans)
        if (!a_actor.GetGraphVariableBool(RE::BSFixedString(name.data()), boolean)) return false;
    for (const auto name : a_descriptor.Floats)
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(name.data()), floating) || !std::isfinite(floating)) return false;
    for (const auto name : a_descriptor.Integers)
        if (!a_actor.GetGraphVariableInt(RE::BSFixedString(name.data()), integer)) return false;
    return true;
}
[[nodiscard]] bool GetManager(RE::Actor& a_actor, const RE::BSAnimationGraphManager*& ar_manager) noexcept
{
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!a_actor.GetAnimationGraphManager(manager) || !manager) return false;
    ar_manager = manager.get(); return true;
}
} // namespace

std::span<const AnimationGraphDescriptor> Catalog() noexcept { return kDescriptors; }
void ResetCache() noexcept
{
    std::scoped_lock lock{g_cacheLock};
    g_cache = {};
    g_nextEviction = 0;
}

bool Resolve(RE::Actor& a_actor, ResolvedDescriptor& ar_result) noexcept
{
    ar_result = {};
    const RE::BSAnimationGraphManager* manager{};
    if (!GetManager(a_actor, manager)) return false;
    if (const auto* cached = FindCachedDescriptor(manager); cached) {
        // Re-probe before reuse: a raw manager address can be recycled after teardown.
        if (Probe(a_actor, *cached)) {
            ar_result = {cached, manager}; return true;
        }
        InvalidateCacheEntry(manager);
    }
    if (const auto* configured = ResolveConfiguredDescriptor(a_actor); configured)
    {
        StoreCacheEntry(manager, configured);
        ar_result = {configured, manager};
        return true;
    }
    const AnimationGraphDescriptor* best{};
    bool ambiguous{};
    for (const auto& candidate : Catalog()) {
        if (!Probe(a_actor, candidate)) continue;
        if (!best || candidate.VariableCount() > best->VariableCount()) { best = &candidate; ambiguous = false; }
        else if (candidate.VariableCount() == best->VariableCount()) ambiguous = true;
    }
    if (!best || ambiguous) return false;
    StoreCacheEntry(manager, best);
    ar_result = {best, manager}; return true;
}

bool Validate(RE::Actor& a_actor, const ResolvedDescriptor& a_descriptor) noexcept
{
    return ManagerMatches(a_actor, a_descriptor) && Probe(a_actor, *a_descriptor.Descriptor);
}

bool ManagerMatches(RE::Actor& a_actor, const ResolvedDescriptor& a_descriptor) noexcept
{
    const RE::BSAnimationGraphManager* manager{};
    return a_descriptor && GetManager(a_actor, manager) && manager == a_descriptor.Manager;
}

bool Capture(RE::Actor& a_actor, AnimationGraphProtocol::SnapshotBuffer& ar_snapshot) noexcept
{
    ResolvedDescriptor resolved;
    if (!Resolve(a_actor, resolved)) return false;
    AnimationGraphProtocol::SnapshotBuffer snapshot{};
    if (!GetContract(*resolved.Descriptor, snapshot.DescriptorDigest, snapshot.DirectionFloatIndex))
        return false;
    snapshot.BooleanCount = static_cast<std::uint16_t>(resolved.Descriptor->Booleans.size());
    snapshot.FloatCount = static_cast<std::uint16_t>(resolved.Descriptor->Floats.size());
    snapshot.IntegerCount = static_cast<std::uint16_t>(resolved.Descriptor->Integers.size());
    for (std::size_t i = 0; i < snapshot.BooleanCount; ++i)
        if (!a_actor.GetGraphVariableBool(RE::BSFixedString(resolved.Descriptor->Booleans[i].data()), snapshot.Booleans[i])) return false;
    for (std::size_t i = 0; i < snapshot.FloatCount; ++i)
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(resolved.Descriptor->Floats[i].data()), snapshot.Floats[i]) || !std::isfinite(snapshot.Floats[i])) return false;
    for (std::size_t i = 0; i < snapshot.IntegerCount; ++i)
        if (!a_actor.GetGraphVariableInt(RE::BSFixedString(resolved.Descriptor->Integers[i].data()), snapshot.Integers[i])) return false;
    snapshot.Direction = snapshot.Floats[snapshot.DirectionFloatIndex];
    if (!std::isfinite(snapshot.Direction) || !ManagerMatches(a_actor, resolved)) return false;
    ar_snapshot = snapshot; return true;
}

bool CaptureForApply(RE::Actor& a_actor, const AnimationGraphProtocol::SnapshotBuffer& a_expected,
                     AnimationGraphProtocol::SnapshotBuffer& ar_previous) noexcept
{
    ResolvedDescriptor resolved;
    if (!a_expected.IsComplete() || !Resolve(a_actor, resolved) || !MatchesCounts(resolved, a_expected))
        return false;

    AnimationGraphProtocol::SnapshotBuffer previous{};
    previous.SnapshotId = a_expected.SnapshotId;
    previous.DescriptorDigest = a_expected.DescriptorDigest;
    previous.DirectionFloatIndex = a_expected.DirectionFloatIndex;
    previous.BooleanCount = a_expected.BooleanCount;
    previous.FloatCount = a_expected.FloatCount;
    previous.IntegerCount = a_expected.IntegerCount;
    previous.BooleanChunkMask = AnimationGraphProtocol::ExpectedChunkMask(
        AnimationGraphProtocol::ValueType::BooleanBits, previous.BooleanCount);
    previous.FloatChunkMask = AnimationGraphProtocol::ExpectedChunkMask(
        AnimationGraphProtocol::ValueType::Float, previous.FloatCount);
    previous.IntegerChunkMask = AnimationGraphProtocol::ExpectedChunkMask(
        AnimationGraphProtocol::ValueType::Integer, previous.IntegerCount);
    for (std::size_t i = 0; i < previous.BooleanCount; ++i)
        if (!a_actor.GetGraphVariableBool(RE::BSFixedString(resolved.Descriptor->Booleans[i].data()), previous.Booleans[i]))
            return false;
    for (std::size_t i = 0; i < previous.FloatCount; ++i)
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(resolved.Descriptor->Floats[i].data()), previous.Floats[i]) ||
            !std::isfinite(previous.Floats[i]))
            return false;
    for (std::size_t i = 0; i < previous.IntegerCount; ++i)
        if (!a_actor.GetGraphVariableInt(RE::BSFixedString(resolved.Descriptor->Integers[i].data()), previous.Integers[i]))
            return false;
    previous.Direction = previous.Floats[previous.DirectionFloatIndex];
    if (!std::isfinite(previous.Direction) || !Validate(a_actor, resolved))
        return false;
    ar_previous = previous;
    return true;
}

bool MatchesCounts(const ResolvedDescriptor& a_descriptor, const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept
{
    std::uint64_t digest{};
    std::uint16_t directionFloatIndex{};
    return a_descriptor && GetContract(*a_descriptor.Descriptor, digest, directionFloatIndex) &&
           a_snapshot.DescriptorDigest == digest && a_snapshot.DirectionFloatIndex == directionFloatIndex &&
           a_snapshot.BooleanCount == a_descriptor.Descriptor->Booleans.size() &&
           a_snapshot.FloatCount == a_descriptor.Descriptor->Floats.size() &&
           a_snapshot.IntegerCount == a_descriptor.Descriptor->Integers.size();
}

bool GetContract(const AnimationGraphDescriptor& a_descriptor, std::uint64_t& ar_digest,
                 std::uint16_t& ar_directionFloatIndex) noexcept
{
    const auto direction = std::find(a_descriptor.Floats.begin(), a_descriptor.Floats.end(), "Direction");
    if (direction == a_descriptor.Floats.end())
        return false;
    ar_directionFloatIndex = static_cast<std::uint16_t>(direction - a_descriptor.Floats.begin());
    if (!AnimationGraphProtocol::IsValidDescriptorContract(
            a_descriptor.Booleans.size(), a_descriptor.Floats.size(), a_descriptor.Integers.size(), 1,
            ar_directionFloatIndex))
        return false;
    ar_digest = AnimationGraphProtocol::ComputeDescriptorDigest(
        a_descriptor.Booleans, a_descriptor.Floats, a_descriptor.Integers, ar_directionFloatIndex);
    return ar_digest != 0;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
