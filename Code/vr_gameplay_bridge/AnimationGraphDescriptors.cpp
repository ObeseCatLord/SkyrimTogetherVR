#include "pch.h"

#include "AnimationGraphDescriptors.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    snapshot.BooleanCount = static_cast<std::uint16_t>(resolved.Descriptor->Booleans.size());
    snapshot.FloatCount = static_cast<std::uint16_t>(resolved.Descriptor->Floats.size());
    snapshot.IntegerCount = static_cast<std::uint16_t>(resolved.Descriptor->Integers.size());
    for (std::size_t i = 0; i < snapshot.BooleanCount; ++i)
        if (!a_actor.GetGraphVariableBool(RE::BSFixedString(resolved.Descriptor->Booleans[i].data()), snapshot.Booleans[i])) return false;
    for (std::size_t i = 0; i < snapshot.FloatCount; ++i)
        if (!a_actor.GetGraphVariableFloat(RE::BSFixedString(resolved.Descriptor->Floats[i].data()), snapshot.Floats[i]) || !std::isfinite(snapshot.Floats[i])) return false;
    for (std::size_t i = 0; i < snapshot.IntegerCount; ++i)
        if (!a_actor.GetGraphVariableInt(RE::BSFixedString(resolved.Descriptor->Integers[i].data()), snapshot.Integers[i])) return false;
    const auto direction = std::find(resolved.Descriptor->Floats.begin(), resolved.Descriptor->Floats.end(), "Direction");
    if (direction == resolved.Descriptor->Floats.end()) return false;
    snapshot.Direction = snapshot.Floats[static_cast<std::size_t>(direction - resolved.Descriptor->Floats.begin())];
    if (!std::isfinite(snapshot.Direction) || !ManagerMatches(a_actor, resolved)) return false;
    ar_snapshot = snapshot; return true;
}

bool MatchesCounts(const ResolvedDescriptor& a_descriptor, const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept
{
    return a_descriptor && a_snapshot.BooleanCount == a_descriptor.Descriptor->Booleans.size() &&
           a_snapshot.FloatCount == a_descriptor.Descriptor->Floats.size() &&
           a_snapshot.IntegerCount == a_descriptor.Descriptor->Integers.size();
}
} // namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
