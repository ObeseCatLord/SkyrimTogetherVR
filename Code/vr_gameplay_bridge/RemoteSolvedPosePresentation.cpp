#include "RemoteSolvedPosePresentation.h"

#include "BridgeEndpoint.h"
#include "VrNoThrow.h"
#include "pch.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation
{
namespace
{
constexpr REL::Version kExpectedSkyrimVrRuntime{1, 4, 15, 0};
constexpr float kMaximumWorldPosition = 1000000.0F;
constexpr float kMaximumLocalPosition = 500.0F;
constexpr float kMinimumLocalScale = 0.5F;
constexpr float kMaximumLocalScale = 2.0F;
constexpr float kMinimumParentScale = 0.05F;
constexpr float kMaximumParentScale = 20.0F;
constexpr float kMinimumBasisLength = 0.95F;
constexpr float kMaximumBasisLength = 1.05F;
constexpr float kMaximumBasisDot = 0.05F;
constexpr float kMinimumBasisDeterminant = 0.95F;
constexpr std::uint32_t kPlayerFormId = 0x14;
constexpr std::size_t kFirstBodyNode = 3;
constexpr std::uint32_t kBodyMask = (1u << kPoseNodeCount) - 1u;
constexpr std::uint32_t kJointMask = (1u << kJointCount) - 1u;
constexpr std::uint32_t kQuiescenceAttempts = 10000;
constexpr std::uint32_t kRequiredStableQuiescencePasses = 64;

constexpr std::array<const char*, kPoseNodeCount> kBodyNodeNames{
    "NPC Head [Head]", "NPC L Hand [LHnd]", "NPC R Hand [RHnd]", "NPC Pelvis [Pelv]",
    "NPC Spine [Spn0]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]",
    "NPC L Clavicle [LClv]", "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]",
    "NPC R Clavicle [RClv]", "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]",
    "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]",
    "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]",
};

constexpr std::array<const char*, kJointCount> kJointNodeNames{
    "NPC L Finger11 [LF11]", "NPC L Finger12 [LF12]", "NPC L Finger13 [LF13]",
    "NPC L Finger21 [LF21]", "NPC L Finger22 [LF22]", "NPC L Finger23 [LF23]",
    "NPC L Finger31 [LF31]", "NPC L Finger32 [LF32]", "NPC L Finger33 [LF33]",
    "NPC L Finger41 [LF41]", "NPC L Finger42 [LF42]", "NPC L Finger43 [LF43]",
    "NPC L Finger51 [LF51]", "NPC L Finger52 [LF52]", "NPC L Finger53 [LF53]",
    "NPC R Finger11 [RF11]", "NPC R Finger12 [RF12]", "NPC R Finger13 [RF13]",
    "NPC R Finger21 [RF21]", "NPC R Finger22 [RF22]", "NPC R Finger23 [RF23]",
    "NPC R Finger31 [RF31]", "NPC R Finger32 [RF32]", "NPC R Finger33 [RF33]",
    "NPC R Finger41 [RF41]", "NPC R Finger42 [RF42]", "NPC R Finger43 [RF43]",
    "NPC R Finger51 [RF51]", "NPC R Finger52 [RF52]", "NPC R Finger53 [RF53]",
};

using UpdateAnimation = void (*)(RE::Actor*, float);

FrameCache g_frameCache;
Diagnostics g_diagnostics;
std::array<RE::BSFixedString, kPoseNodeCount> g_bodyNames{};
std::array<RE::BSFixedString, kJointCount> g_jointNames{};
std::atomic<UpdateAnimation> g_original{};
std::atomic_bool g_installed{};
std::atomic_bool g_presentationEnabled{};
std::atomic_bool g_slotRestored{true};
std::atomic_bool g_slotProtectionRestored{true};
std::atomic<std::uint64_t> g_inFlightCallbacks{};
std::atomic<std::uint64_t> g_callbackEntries{};
void** g_characterSlot{};
DWORD g_originalSlotProtection{};

struct PoseApplication
{
    RE::NiAVObject* Node{};
    RE::NiTransform Original{};
    RE::NiTransform Desired{};
};

class ScopedApplications final
{
public:
    ScopedApplications(
        RE::NiAVObject& a_root,
        std::array<PoseApplication, kPoseNodeCount>& a_body,
        const std::size_t& a_bodyCount,
        std::array<PoseApplication, kFirstBodyNode>& a_endpoints,
        const std::size_t& a_endpointCount,
        std::array<PoseApplication, kJointCount>& a_joints,
        const std::size_t& a_jointCount) noexcept :
        _root(a_root), _body(a_body), _bodyCount(a_bodyCount),
        _endpoints(a_endpoints), _endpointCount(a_endpointCount),
        _joints(a_joints), _jointCount(a_jointCount)
    {}

    ~ScopedApplications() noexcept
    {
        if (!_active)
            return;
        try {
            Restore(_body, _bodyCount);
            Restore(_endpoints, _endpointCount);
            Restore(_joints, _jointCount);
            RE::NiUpdateData update{};
            _root.UpdateDownwardPass(update, 0);
            _root.UpdateWorldBound();
        } catch (...) {
        }
    }

    void Release() noexcept { _active = false; }

private:
    template <std::size_t N>
    static void Restore(std::array<PoseApplication, N>& a_applications, const std::size_t a_count) noexcept
    {
        for (std::size_t index = 0; index < a_count; ++index)
            a_applications[index].Node->local = a_applications[index].Original;
    }

    RE::NiAVObject& _root;
    std::array<PoseApplication, kPoseNodeCount>& _body;
    const std::size_t& _bodyCount;
    std::array<PoseApplication, kFirstBodyNode>& _endpoints;
    const std::size_t& _endpointCount;
    std::array<PoseApplication, kJointCount>& _joints;
    const std::size_t& _jointCount;
    bool _active{true};
};

class CallbackScope final
{
public:
    CallbackScope() noexcept
    {
        g_inFlightCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_callbackEntries.fetch_add(1, std::memory_order_release);
    }
    ~CallbackScope() noexcept { g_inFlightCallbacks.fetch_sub(1, std::memory_order_acq_rel); }
};

[[nodiscard]] bool IsSpanWithin(
    const std::uintptr_t a_base,
    const std::uintptr_t a_size,
    const std::uintptr_t a_address,
    const std::uintptr_t a_span) noexcept
{
    if (a_span == 0 || a_address < a_base || a_base > std::numeric_limits<std::uintptr_t>::max() - a_size)
        return false;
    const auto offset = a_address - a_base;
    return offset <= a_size && a_span <= a_size - offset;
}

[[nodiscard]] bool IsReadableSpan(const std::uintptr_t a_address, const std::size_t a_size) noexcept
{
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithin(reinterpret_cast<std::uintptr_t>(memory.BaseAddress), memory.RegionSize, a_address, a_size))
        return false;
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kReadable) != 0;
}

[[nodiscard]] bool IsExecutableSpan(const std::uintptr_t a_address, const std::size_t a_size) noexcept
{
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !IsSpanWithin(reinterpret_cast<std::uintptr_t>(memory.BaseAddress), memory.RegionSize, a_address, a_size))
        return false;
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutable) != 0;
}

[[nodiscard]] bool IsExpectedTarget(
    const std::uintptr_t a_moduleBase,
    const std::uintptr_t a_slotAddress,
    const std::uintptr_t a_targetAddress) noexcept
{
    if (!HasPinnedTargetConfiguration() || !REL::Module::IsVR() ||
        REL::Module::get().version() != kExpectedSkyrimVrRuntime || a_moduleBase == 0)
        return false;
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    const auto functionSize = kActorUpdateAnimationVrEndRva - kActorUpdateAnimationVrRva;
    return a_slotAddress == a_moduleBase + kCharacterUpdateAnimationSlotVrRva &&
           a_targetAddress == a_moduleBase + kActorUpdateAnimationVrRva &&
           IsReadableSpan(a_slotAddress, sizeof(void*)) &&
           IsSpanWithin(text.address(), text.size(), a_targetAddress, functionSize) &&
           IsExecutableSpan(a_targetAddress, functionSize) &&
           std::memcmp(reinterpret_cast<const void*>(a_targetAddress),
                       kActorUpdateAnimationVrPrefix.data(), kActorUpdateAnimationVrPrefix.size()) == 0;
}

[[nodiscard]] bool IsFinitePoint(const RE::NiPoint3& a_value) noexcept
{
    return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z);
}

[[nodiscard]] bool IsBoundedPoint(const RE::NiPoint3& a_value, const float a_limit) noexcept
{
    return IsFinitePoint(a_value) && std::abs(a_value.x) <= a_limit &&
           std::abs(a_value.y) <= a_limit && std::abs(a_value.z) <= a_limit;
}

[[nodiscard]] float Dot(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right) noexcept
{
    return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
}

[[nodiscard]] RE::NiPoint3 Cross(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right) noexcept
{
    return {a_left.y * a_right.z - a_left.z * a_right.y,
            a_left.z * a_right.x - a_left.x * a_right.z,
            a_left.x * a_right.y - a_left.y * a_right.x};
}

[[nodiscard]] bool IsOrthonormal(const RE::NiMatrix3& a_matrix) noexcept
{
    const RE::NiPoint3 x{a_matrix.entry[0][0], a_matrix.entry[0][1], a_matrix.entry[0][2]};
    const RE::NiPoint3 y{a_matrix.entry[1][0], a_matrix.entry[1][1], a_matrix.entry[1][2]};
    const RE::NiPoint3 z{a_matrix.entry[2][0], a_matrix.entry[2][1], a_matrix.entry[2][2]};
    const auto minimumLengthSquared = kMinimumBasisLength * kMinimumBasisLength;
    const auto maximumLengthSquared = kMaximumBasisLength * kMaximumBasisLength;
    return IsFinitePoint(x) && IsFinitePoint(y) && IsFinitePoint(z) &&
           Dot(x, x) >= minimumLengthSquared && Dot(x, x) <= maximumLengthSquared &&
           Dot(y, y) >= minimumLengthSquared && Dot(y, y) <= maximumLengthSquared &&
           Dot(z, z) >= minimumLengthSquared && Dot(z, z) <= maximumLengthSquared &&
           std::abs(Dot(x, y)) <= kMaximumBasisDot && std::abs(Dot(x, z)) <= kMaximumBasisDot &&
           std::abs(Dot(y, z)) <= kMaximumBasisDot && Dot(Cross(x, y), z) >= kMinimumBasisDeterminant;
}

[[nodiscard]] bool IsSafeTransform(
    const RE::NiTransform& a_transform,
    const float a_minimumScale,
    const float a_maximumScale) noexcept
{
    return IsBoundedPoint(a_transform.translate, kMaximumWorldPosition) &&
           std::isfinite(a_transform.scale) && a_transform.scale >= a_minimumScale &&
           a_transform.scale <= a_maximumScale && IsOrthonormal(a_transform.rotate);
}

[[nodiscard]] bool QuaternionToMatrix(const Quaternion& a_value, RE::NiMatrix3& ar_matrix) noexcept
{
    if (!std::isfinite(a_value.X) || !std::isfinite(a_value.Y) ||
        !std::isfinite(a_value.Z) || !std::isfinite(a_value.W))
        return false;
    const auto lengthSquared = a_value.X * a_value.X + a_value.Y * a_value.Y +
                               a_value.Z * a_value.Z + a_value.W * a_value.W;
    if (lengthSquared < 0.95F || lengthSquared > 1.05F)
        return false;
    const auto q = Normalize(a_value);
    const auto xx = q.X * q.X;
    const auto yy = q.Y * q.Y;
    const auto zz = q.Z * q.Z;
    const auto xy = q.X * q.Y;
    const auto xz = q.X * q.Z;
    const auto yz = q.Y * q.Z;
    const auto xw = q.X * q.W;
    const auto yw = q.Y * q.W;
    const auto zw = q.Z * q.W;
    ar_matrix.entry[0][0] = 1.0F - 2.0F * (yy + zz);
    ar_matrix.entry[0][1] = 2.0F * (xy - zw);
    ar_matrix.entry[0][2] = 2.0F * (xz + yw);
    ar_matrix.entry[1][0] = 2.0F * (xy + zw);
    ar_matrix.entry[1][1] = 1.0F - 2.0F * (xx + zz);
    ar_matrix.entry[1][2] = 2.0F * (yz - xw);
    ar_matrix.entry[2][0] = 2.0F * (xz - yw);
    ar_matrix.entry[2][1] = 2.0F * (yz + xw);
    ar_matrix.entry[2][2] = 1.0F - 2.0F * (xx + yy);
    return IsOrthonormal(ar_matrix);
}

[[nodiscard]] bool ResolveApplication(
    RE::NiAVObject& a_root,
    const RE::BSFixedString& a_name,
    PoseApplication& ar_application) noexcept
{
    ar_application.Node = a_root.GetObjectByName(a_name);
    if (!ar_application.Node || !IsSafeTransform(ar_application.Node->local, kMinimumLocalScale, kMaximumLocalScale))
        return false;
    ar_application.Original = ar_application.Node->local;
    ar_application.Desired = ar_application.Original;
    return true;
}

[[nodiscard]] bool BuildBodyLocal(
    const std::size_t a_nodeIndex,
    const Transform& a_source,
    PoseApplication& ar_application) noexcept
{
    if (!QuaternionToMatrix(a_source.Rotation, ar_application.Desired.rotate))
        return false;
    if (a_nodeIndex == kFirstBodyNode) {
        const RE::NiPoint3 position{a_source.X, a_source.Y, a_source.Z};
        if (!IsBoundedPoint(position, kMaximumLocalPosition))
            return false;
        ar_application.Desired.translate = position;
    }
    return IsSafeTransform(ar_application.Desired, kMinimumLocalScale, kMaximumLocalScale);
}

[[nodiscard]] bool BuildEndpointLocal(
    const Transform& a_source,
    PoseApplication& ar_application) noexcept
{
    const RE::NiPoint3 worldPosition{a_source.X, a_source.Y, a_source.Z};
    RE::NiMatrix3 worldRotation{};
    auto* const parent = ar_application.Node->parent;
    if (!parent || !IsBoundedPoint(worldPosition, kMaximumWorldPosition) ||
        !QuaternionToMatrix(a_source.Rotation, worldRotation) ||
        !IsSafeTransform(parent->world, kMinimumParentScale, kMaximumParentScale) ||
        !std::isfinite(a_source.Scale))
        return false;
    const auto inverseParent = parent->world.Invert();
    ar_application.Desired.translate = inverseParent * worldPosition;
    ar_application.Desired.scale = a_source.Scale / parent->world.scale;
    ar_application.Desired.rotate = parent->world.rotate.Transpose() * worldRotation;
    return IsBoundedPoint(ar_application.Desired.translate, kMaximumLocalPosition) &&
           IsSafeTransform(ar_application.Desired, kMinimumLocalScale, kMaximumLocalScale);
}

[[nodiscard]] bool ApplySnapshot(
    RE::Actor& a_actor,
    RE::NiAVObject& a_root,
    const PoseSnapshot& a_snapshot,
    const std::uint64_t a_now) noexcept
{
    if (a_actor.IsInRagdollState())
        return false;
    const Frame body = ResolveSelection(a_snapshot.Body, a_now);
    const Frame joints = ResolveSelection(a_snapshot.Joints, a_now);
    if ((a_snapshot.Body.Count != 0 && (body.NodeMask == 0 || (body.NodeMask & ~kBodyMask) != 0)) ||
        (a_snapshot.Joints.Count != 0 && (joints.JointMask == 0 || (joints.JointMask & ~kJointMask) != 0)))
        return false;

    std::array<PoseApplication, kPoseNodeCount> bodyApplications{};
    std::array<PoseApplication, kFirstBodyNode> endpointApplications{};
    std::array<PoseApplication, kJointCount> jointApplications{};
    std::size_t bodyCount{};
    std::size_t endpointCount{};
    std::size_t jointCount{};

    if (a_snapshot.Body.Count != 0) {
        for (std::size_t index = kFirstBodyNode; index < kPoseNodeCount; ++index) {
            if ((body.NodeMask & (1u << index)) == 0)
                continue;
            auto& application = bodyApplications[bodyCount++];
            if (!ResolveApplication(a_root, g_bodyNames[index], application) ||
                !BuildBodyLocal(index, body.Nodes[index], application))
                return false;
        }
        for (std::size_t index = 0; index < kFirstBodyNode; ++index) {
            if ((body.NodeMask & (1u << index)) == 0)
                continue;
            if (!ResolveApplication(a_root, g_bodyNames[index], endpointApplications[endpointCount++]))
                return false;
        }
    }
    if (a_snapshot.Joints.Count != 0) {
        for (std::size_t index = 0; index < kJointCount; ++index) {
            if ((joints.JointMask & (1u << index)) == 0)
                continue;
            auto& application = jointApplications[jointCount++];
            if (!ResolveApplication(a_root, g_jointNames[index], application) ||
                !QuaternionToMatrix(joints.Joints[index], application.Desired.rotate) ||
                !IsSafeTransform(application.Desired, kMinimumLocalScale, kMaximumLocalScale))
                return false;
        }
    }
    if (bodyCount == 0 && endpointCount == 0 && jointCount == 0)
        return false;

    ScopedApplications rollback{
        a_root, bodyApplications, bodyCount, endpointApplications, endpointCount,
        jointApplications, jointCount};
    for (std::size_t index = 0; index < bodyCount; ++index)
        bodyApplications[index].Node->local = bodyApplications[index].Desired;

    RE::NiUpdateData update{};
    if (bodyCount != 0)
        a_root.UpdateDownwardPass(update, 0);

    std::size_t endpointApplicationIndex{};
    for (std::size_t nodeIndex = 0; nodeIndex < kFirstBodyNode; ++nodeIndex) {
        if ((body.NodeMask & (1u << nodeIndex)) == 0)
            continue;
        auto& application = endpointApplications[endpointApplicationIndex++];
        if (!BuildEndpointLocal(body.Nodes[nodeIndex], application))
            return false;
        application.Node->local = application.Desired;
    }
    for (std::size_t index = 0; index < jointCount; ++index)
        jointApplications[index].Node->local = jointApplications[index].Desired;

    a_root.UpdateDownwardPass(update, 0);
    a_root.UpdateWorldBound();
    rollback.Release();
    return true;
}

void ObserveThread(const std::uint32_t a_threadId) noexcept
{
    for (auto& observed : g_diagnostics.ObservedThreadIds) {
        auto value = observed.load(std::memory_order_relaxed);
        if (value == a_threadId)
            return;
        if (value == 0 && observed.compare_exchange_strong(value, a_threadId, std::memory_order_relaxed))
            return;
    }
}

void HookUpdateAnimation(RE::Actor* a_actor, const float a_delta) noexcept
{
    CallbackScope callbackScope;
    const auto original = g_original.load(std::memory_order_acquire);
    if (!original)
        return;
    original(a_actor, a_delta);

    g_diagnostics.Callbacks.fetch_add(1, std::memory_order_relaxed);
    ObserveThread(GetCurrentThreadId());
    if (!g_presentationEnabled.load(std::memory_order_acquire) || !a_actor)
        return;
    if (a_actor->GetFormID() == kPlayerFormId) {
        g_diagnostics.PlayerRejections.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (a_actor->IsInRagdollState()) {
        g_diagnostics.RagdollRejections.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    try {
        RE::NiPointer<RE::NiAVObject> root{a_actor->Get3D()};
        if (!root) {
            g_diagnostics.FastRejections.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto now = GetTickCount64();
        PoseSnapshot snapshot{};
        if (!g_frameCache.TrySnapshot(
                reinterpret_cast<std::uintptr_t>(a_actor),
                reinterpret_cast<std::uintptr_t>(root.get()), now, snapshot)) {
            g_diagnostics.SnapshotRejections.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (a_actor->IsInRagdollState()) {
            g_diagnostics.RagdollRejections.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (ApplySnapshot(*a_actor, *root, snapshot, now))
            g_diagnostics.ApplySuccesses.fetch_add(1, std::memory_order_relaxed);
        else
            g_diagnostics.ApplyFailures.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        g_diagnostics.ApplyFailures.fetch_add(1, std::memory_order_relaxed);
    }
}

[[nodiscard]] bool RestoreCharacterSlot() noexcept
{
    auto* const slot = g_characterSlot;
    const auto original = g_original.load(std::memory_order_acquire);
    if (!slot || !original)
        return false;
    DWORD currentProtect{};
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, std::addressof(currentProtect))) {
        g_slotProtectionRestored.store(false, std::memory_order_release);
        return false;
    }
    auto* const observed = InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot),
        reinterpret_cast<void*>(original),
        reinterpret_cast<void*>(&HookUpdateAnimation));
    const bool pointerRestored = observed == reinterpret_cast<void*>(&HookUpdateAnimation) ||
                                 observed == reinterpret_cast<void*>(original);
    DWORD ignored{};
    const bool protectionRestored = g_originalSlotProtection != 0 &&
        VirtualProtect(slot, sizeof(*slot), g_originalSlotProtection, std::addressof(ignored)) != 0;
    const auto verify = InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot), nullptr, nullptr);
    const bool restored = pointerRestored && verify == reinterpret_cast<void*>(original);
    g_slotRestored.store(restored, std::memory_order_release);
    g_slotProtectionRestored.store(protectionRestored, std::memory_order_release);
    return restored && protectionRestored;
}

[[nodiscard]] bool WaitForQuiescence() noexcept
{
    std::uint64_t stableEntryCount{};
    std::uint32_t stablePasses{};
    for (std::uint32_t attempt = 0; attempt < kQuiescenceAttempts; ++attempt) {
        const auto entriesBefore = g_callbackEntries.load(std::memory_order_acquire);
        const auto inFlight = g_inFlightCallbacks.load(std::memory_order_acquire);
        MemoryBarrier();
        const auto entriesAfter = g_callbackEntries.load(std::memory_order_acquire);
        if (CanClearCallableState(
                g_slotRestored.load(std::memory_order_acquire),
                g_slotProtectionRestored.load(std::memory_order_acquire), inFlight,
                entriesBefore == entriesAfter)) {
            if (stablePasses == 0 || stableEntryCount == entriesAfter) {
                stableEntryCount = entriesAfter;
                if (++stablePasses >= kRequiredStableQuiescencePasses)
                    return true;
            } else {
                stableEntryCount = entriesAfter;
                stablePasses = 1;
            }
        } else {
            stablePasses = 0;
        }
        SwitchToThread();
    }
    return false;
}

void RecordAggregate(
    const char* a_name,
    const std::uint64_t a_value,
    std::uint64_t& ar_lastReported) noexcept
{
    if (a_value == 0 || a_value == ar_lastReported || (a_value & (a_value - 1)) != 0)
        return;
    ar_lastReported = a_value;
    NoThrow::BestEffort([&] { SKSE::log::info(
        "SkyrimTogetherVRGameplayBridge: remote solved-pose {}={}", a_name, a_value); });
}
} // namespace

FrameCache& GetFrameCache() noexcept
{
    return g_frameCache;
}

Diagnostics& GetDiagnostics() noexcept
{
    return g_diagnostics;
}

bool Install() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return !g_slotRestored.load(std::memory_order_acquire);
    try {
        const auto moduleBase = REL::Module::get().base();
        if (moduleBase == 0 || moduleBase > std::numeric_limits<std::uintptr_t>::max() - kCharacterUpdateAnimationSlotVrRva)
            return false;
        const auto slotAddress = moduleBase + kCharacterUpdateAnimationSlotVrRva;
        const auto targetAddress = moduleBase + kActorUpdateAnimationVrRva;
        if (!IsExpectedTarget(moduleBase, slotAddress, targetAddress))
            return false;
        auto* const slot = reinterpret_cast<void**>(slotAddress);
        if ((slotAddress % alignof(void*)) != 0)
            return false;
        const auto currentTarget = reinterpret_cast<std::uintptr_t>(InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot), nullptr, nullptr));
        if (!ShouldPatchCharacterVtableSlot(slotAddress, slotAddress, currentTarget, targetAddress, false))
            return false;

        for (std::size_t index = 0; index < g_bodyNames.size(); ++index)
            g_bodyNames[index] = kBodyNodeNames[index];
        for (std::size_t index = 0; index < g_jointNames.size(); ++index)
            g_jointNames[index] = kJointNodeNames[index];

        g_characterSlot = slot;
        g_original.store(reinterpret_cast<UpdateAnimation>(targetAddress), std::memory_order_release);
        g_slotRestored.store(false, std::memory_order_release);
        g_slotProtectionRestored.store(false, std::memory_order_release);
        g_presentationEnabled.store(false, std::memory_order_release);
        g_installed.store(true, std::memory_order_release);

        DWORD oldProtect{};
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, std::addressof(oldProtect))) {
            g_slotRestored.store(true, std::memory_order_release);
            g_slotProtectionRestored.store(true, std::memory_order_release);
            g_installed.store(false, std::memory_order_release);
            g_characterSlot = nullptr;
            g_original.store(nullptr, std::memory_order_release);
            g_originalSlotProtection = 0;
            return false;
        }
        g_originalSlotProtection = oldProtect;
        const auto exchanged = InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot),
            reinterpret_cast<void*>(&HookUpdateAnimation),
            reinterpret_cast<void*>(targetAddress));
        DWORD ignored{};
        const bool protectionRestored =
            VirtualProtect(slot, sizeof(*slot), oldProtect, std::addressof(ignored)) != 0;
        g_slotProtectionRestored.store(protectionRestored, std::memory_order_release);
        const auto verify = InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot), nullptr, nullptr);
        if (exchanged != reinterpret_cast<void*>(targetAddress) || !protectionRestored ||
            verify != reinterpret_cast<void*>(&HookUpdateAnimation)) {
            static_cast<void>(RestoreCharacterSlot());
            return false;
        }
        g_presentationEnabled.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        g_presentationEnabled.store(false, std::memory_order_release);
        if (g_installed.load(std::memory_order_acquire))
            static_cast<void>(RestoreCharacterSlot());
        return false;
    }
}

bool Uninstall() noexcept
{
    if (!g_installed.load(std::memory_order_acquire))
        return true;
    g_presentationEnabled.store(false, std::memory_order_release);
    if ((!g_slotRestored.load(std::memory_order_acquire) ||
         !g_slotProtectionRestored.load(std::memory_order_acquire)) &&
        !RestoreCharacterSlot()) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault(
            "remote solved-pose Character vtable slot restoration failed"); });
        return false;
    }
    if (!WaitForQuiescence()) {
        NoThrow::BestEffort([] { BridgeEndpoint::Get().Fault(
            "remote solved-pose callback quiescence could not be proven"); });
        return false;
    }

    g_installed.store(false, std::memory_order_release);
    g_characterSlot = nullptr;
    g_original.store(nullptr, std::memory_order_release);
    g_originalSlotProtection = 0;
    g_slotProtectionRestored.store(true, std::memory_order_release);
    g_bodyNames = {};
    g_jointNames = {};
    g_frameCache.Reset();
    return true;
}

void ProcessDiagnostics() noexcept
{
    static std::array<std::uint64_t, 7> lastReported{};
    static std::uint64_t lastThreadReportCallbacks{};
    const std::array<std::pair<const char*, std::uint64_t>, 7> counters{
        std::pair{"callbacks", g_diagnostics.Callbacks.load(std::memory_order_relaxed)},
        std::pair{"fast_rejections", g_diagnostics.FastRejections.load(std::memory_order_relaxed)},
        std::pair{"player_rejections", g_diagnostics.PlayerRejections.load(std::memory_order_relaxed)},
        std::pair{"ragdoll_rejections", g_diagnostics.RagdollRejections.load(std::memory_order_relaxed)},
        std::pair{"snapshot_rejections", g_diagnostics.SnapshotRejections.load(std::memory_order_relaxed)},
        std::pair{"apply_successes", g_diagnostics.ApplySuccesses.load(std::memory_order_relaxed)},
        std::pair{"apply_failures", g_diagnostics.ApplyFailures.load(std::memory_order_relaxed)},
    };
    for (std::size_t index = 0; index < counters.size(); ++index)
        RecordAggregate(counters[index].first, counters[index].second, lastReported[index]);

    const auto callbacks = g_diagnostics.Callbacks.load(std::memory_order_relaxed);
    if (callbacks != 0 && callbacks != lastThreadReportCallbacks &&
        (callbacks & (callbacks - 1)) == 0) {
        lastThreadReportCallbacks = callbacks;
        for (const auto& thread : g_diagnostics.ObservedThreadIds) {
            const auto threadId = thread.load(std::memory_order_relaxed);
            if (threadId != 0)
                NoThrow::BestEffort([&] { SKSE::log::info(
                    "SkyrimTogetherVRGameplayBridge: remote solved-pose observed thread={}", threadId); });
        }
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::RemoteSolvedPosePresentation
