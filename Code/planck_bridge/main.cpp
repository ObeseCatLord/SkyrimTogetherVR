#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

#include <vr_common/VRHandoffPath.h>
#include <vr_common/VRPlanckPhysicsBridge.h>

using PluginHandle = std::uint32_t;

static constexpr PluginHandle kPluginHandleInvalid = static_cast<PluginHandle>(-1);
static constexpr std::uint32_t kPluginInfoVersion = 1;
static constexpr std::uint32_t kInterfaceMessaging = 5;
static constexpr std::uint32_t kSkseMessagePostPostLoad = 1;
static constexpr std::uint32_t kPlanckMessageGetInterface = 0x92F38745;
static constexpr std::uint32_t kPlanckInterfaceRevision = 2;
static constexpr std::uint64_t kHandoffWriteIntervalMs = 250;
// PLANCK 0.8.0 reports V00.08.00.00 as 80000. Interface revision, rather than
// this diagnostic build number, is the public compatibility contract.
static constexpr std::uint32_t kPlanck0080BuildNumber = 80000;

struct PluginInfo
{
    std::uint32_t infoVersion;
    const char* name;
    std::uint32_t version;
};

struct SKSEInterface
{
    std::uint32_t skseVersion;
    std::uint32_t runtimeVersion;
    std::uint32_t editorVersion;
    std::uint32_t isEditor;
    void* (*QueryInterface)(std::uint32_t);
    PluginHandle (*GetPluginHandle)();
    std::uint32_t (*GetReleaseIndex)();
    const void* (*GetPluginInfo)(const char*);
};

struct SKSEMessagingInterface
{
    struct Message
    {
        const char* sender;
        std::uint32_t type;
        std::uint32_t dataLen;
        void* data;
    };

    std::uint32_t interfaceVersion;
    bool (*RegisterListener)(PluginHandle, const char*, void*);
    bool (*Dispatch)(PluginHandle, std::uint32_t, void*, std::uint32_t, const char*);
    void* (*GetEventDispatcher)(std::uint32_t);
};

namespace PlanckPluginAPI
{
constexpr std::uint32_t kNodeCapacity = 64;
enum class ResultCode : std::uint32_t { Accepted, Empty, InvalidRequest, Duplicate, QueueFull, Unsupported };
struct Vector3 { float x, y, z; };
struct Quaternion { float x, y, z, w; };
struct Header { std::uint32_t size, reserved; std::uint64_t sourceSession, eventId; };
struct Hit { Header header; std::uint32_t targetFormId; float impulseMultiplier; char nodeName[kNodeCapacity]; Vector3 position, velocity; };
struct Ragdoll { Header header; std::uint32_t targetFormId, reserved; Vector3 sourcePosition; };
struct RagdollExit { Header header; std::uint32_t targetFormId, reserved; };
struct GripState { Vector3 worldPosition; Quaternion worldRotation; Vector3 linearVelocity, angularVelocity; float ttlSeconds; std::uint32_t reserved; };
struct BeginGrip { Header header; std::uint64_t gripId; std::uint32_t targetFormId; char nodeName[kNodeCapacity]; GripState state; };
struct UpdateGrip { Header header; std::uint64_t gripId; std::uint32_t targetFormId, reserved; GripState state; };
struct EndGrip { Header header; std::uint64_t gripId; std::uint32_t targetFormId, reserved; };
struct ClearSession { Header header; };
struct DiscardLocalEvents { std::uint32_t size, reserved; std::uint64_t lifecycleGeneration; };
struct CapsRequest { std::uint32_t size, reserved; };
struct CapsResult { std::uint32_t size, interfaceRevision; std::uint64_t featureBits; std::uint32_t maxPendingCommands, maxLocalEvents; };
struct DequeueRequest { std::uint32_t size, reserved; };
struct LocalEvent {
    std::uint32_t size, targetFormId;
    std::uint64_t eventId;
    Vector3 position, velocity;
    std::uint32_t flags;
    char nodeName[kNodeCapacity];
    std::uint8_t kind;
    std::uint8_t reserved[3];
    std::uint64_t gripId;
    Quaternion rotation;
    Vector3 linearVelocity, angularVelocity, sourcePosition;
    float impulseMultiplier, ttlSeconds;
};
struct Result { std::uint32_t size; ResultCode code; std::uint64_t sequence; };
struct IPlanckInterface002 {
    virtual Result GetCapabilities(const CapsRequest&, CapsResult&) = 0;
    virtual Result SubmitRemoteHitImpulse(const Hit&) = 0;
    virtual Result SubmitRemoteRagdoll(const Ragdoll&) = 0;
    virtual Result SubmitRemoteRagdollExit(const RagdollExit&) = 0;
    virtual Result BeginRemoteGrip(const BeginGrip&) = 0;
    virtual Result UpdateRemoteGrip(const UpdateGrip&) = 0;
    virtual Result EndRemoteGrip(const EndGrip&) = 0;
    virtual Result ClearRemoteSession(const ClearSession&) = 0;
    virtual Result DequeueLocalPhysicalEvent(const DequeueRequest&, LocalEvent&) = 0;
    virtual Result DiscardLocalPhysicalEvents(const DiscardLocalEvents&) = 0;
};
}

// Keep this standalone bridge's private declarations pinned to PLANCK's
// interface002 ABI. The PLANCK SDK is intentionally not a build dependency of
// this target, so every cross-DLL POD size, offset, and virtual signature is
// asserted here instead of relying on a hand-maintained partial prefix.
static_assert(sizeof(PlanckPluginAPI::Header) == 0x18);
static_assert(sizeof(PlanckPluginAPI::Hit) == 0x78);
static_assert(sizeof(PlanckPluginAPI::Ragdoll) == 0x30);
static_assert(sizeof(PlanckPluginAPI::RagdollExit) == 0x20);
static_assert(sizeof(PlanckPluginAPI::GripState) == 0x3C);
static_assert(sizeof(PlanckPluginAPI::BeginGrip) == 0xA0);
static_assert(sizeof(PlanckPluginAPI::UpdateGrip) == 0x68);
static_assert(sizeof(PlanckPluginAPI::EndGrip) == 0x28);
static_assert(sizeof(PlanckPluginAPI::ClearSession) == 0x18);
static_assert(sizeof(PlanckPluginAPI::DiscardLocalEvents) == 0x10);
static_assert(sizeof(PlanckPluginAPI::CapsRequest) == 0x08);
static_assert(sizeof(PlanckPluginAPI::CapsResult) == 0x18);
static_assert(sizeof(PlanckPluginAPI::DequeueRequest) == 0x08);
static_assert(sizeof(PlanckPluginAPI::Result) == 0x10);
static_assert(offsetof(PlanckPluginAPI::Header, size) == 0x00);
static_assert(offsetof(PlanckPluginAPI::Header, sourceSession) == 0x08);
static_assert(offsetof(PlanckPluginAPI::Header, eventId) == 0x10);
static_assert(offsetof(PlanckPluginAPI::Hit, header) == 0x00);
static_assert(offsetof(PlanckPluginAPI::Hit, targetFormId) == 0x18);
static_assert(offsetof(PlanckPluginAPI::Hit, impulseMultiplier) == 0x1C);
static_assert(offsetof(PlanckPluginAPI::Hit, nodeName) == 0x20);
static_assert(offsetof(PlanckPluginAPI::Hit, position) == 0x60);
static_assert(offsetof(PlanckPluginAPI::Hit, velocity) == 0x6C);
static_assert(offsetof(PlanckPluginAPI::Ragdoll, targetFormId) == 0x18);
static_assert(offsetof(PlanckPluginAPI::Ragdoll, reserved) == 0x1C);
static_assert(offsetof(PlanckPluginAPI::Ragdoll, sourcePosition) == 0x20);
static_assert(offsetof(PlanckPluginAPI::RagdollExit, targetFormId) == 0x18);
static_assert(offsetof(PlanckPluginAPI::RagdollExit, reserved) == 0x1C);
static_assert(offsetof(PlanckPluginAPI::GripState, worldPosition) == 0x00);
static_assert(offsetof(PlanckPluginAPI::GripState, worldRotation) == 0x0C);
static_assert(offsetof(PlanckPluginAPI::GripState, linearVelocity) == 0x1C);
static_assert(offsetof(PlanckPluginAPI::GripState, angularVelocity) == 0x28);
static_assert(offsetof(PlanckPluginAPI::GripState, ttlSeconds) == 0x34);
static_assert(offsetof(PlanckPluginAPI::GripState, reserved) == 0x38);
static_assert(offsetof(PlanckPluginAPI::BeginGrip, gripId) == 0x18);
static_assert(offsetof(PlanckPluginAPI::BeginGrip, targetFormId) == 0x20);
static_assert(offsetof(PlanckPluginAPI::BeginGrip, nodeName) == 0x24);
static_assert(offsetof(PlanckPluginAPI::BeginGrip, state) == 0x64);
static_assert(offsetof(PlanckPluginAPI::UpdateGrip, gripId) == 0x18);
static_assert(offsetof(PlanckPluginAPI::UpdateGrip, targetFormId) == 0x20);
static_assert(offsetof(PlanckPluginAPI::UpdateGrip, reserved) == 0x24);
static_assert(offsetof(PlanckPluginAPI::UpdateGrip, state) == 0x28);
static_assert(offsetof(PlanckPluginAPI::EndGrip, gripId) == 0x18);
static_assert(offsetof(PlanckPluginAPI::EndGrip, targetFormId) == 0x20);
static_assert(offsetof(PlanckPluginAPI::EndGrip, reserved) == 0x24);
static_assert(offsetof(PlanckPluginAPI::DiscardLocalEvents, lifecycleGeneration) == 0x08);
static_assert(offsetof(PlanckPluginAPI::CapsRequest, size) == 0x00);
static_assert(offsetof(PlanckPluginAPI::CapsResult, featureBits) == 0x08);
static_assert(offsetof(PlanckPluginAPI::DequeueRequest, size) == 0x00);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::GetCapabilities),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::CapsRequest&, PlanckPluginAPI::CapsResult&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::SubmitRemoteHitImpulse),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::Hit&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::SubmitRemoteRagdoll),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::Ragdoll&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::SubmitRemoteRagdollExit),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::RagdollExit&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::BeginRemoteGrip),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::BeginGrip&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::UpdateRemoteGrip),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::UpdateGrip&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::EndRemoteGrip),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::EndGrip&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::ClearRemoteSession),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::ClearSession&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::DequeueLocalPhysicalEvent),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::DequeueRequest&, PlanckPluginAPI::LocalEvent&)>);
static_assert(std::is_same_v<decltype(&PlanckPluginAPI::IPlanckInterface002::DiscardLocalPhysicalEvents),
    PlanckPluginAPI::Result (PlanckPluginAPI::IPlanckInterface002::*)(const PlanckPluginAPI::DiscardLocalEvents&)>);
static_assert(std::is_standard_layout_v<PlanckPluginAPI::LocalEvent>);
static_assert(std::is_trivially_copyable_v<PlanckPluginAPI::LocalEvent>);
static_assert(sizeof(PlanckPluginAPI::LocalEvent) == sizeof(SkyrimTogetherVR::PlanckBridge::LocalEvent));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, size) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Size));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, targetFormId) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, TargetFormId));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, eventId) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, EventId));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, position) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Position));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, velocity) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Velocity));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, flags) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Flags));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, nodeName) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, NodeName));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, kind) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Kind));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, reserved) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Reserved));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, gripId) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, GripId));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, rotation) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, Rotation));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, linearVelocity) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, LinearVelocity));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, angularVelocity) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, AngularVelocity));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, sourcePosition) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, SourcePosition));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, impulseMultiplier) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, ImpulseMultiplier));
static_assert(offsetof(PlanckPluginAPI::LocalEvent, ttlSeconds) == offsetof(SkyrimTogetherVR::PlanckBridge::LocalEvent, TtlSeconds));

namespace
{
struct PlanckMessage
{
    void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
};

PluginHandle g_pluginHandle = kPluginHandleInvalid;
std::atomic<SKSEMessagingInterface*> g_messaging{nullptr};
std::atomic<PlanckPluginAPI::IPlanckInterface002*> g_planck{nullptr};
std::atomic_bool g_handoffActive{false};
std::atomic_bool g_requestAttempted{false};
std::atomic_uint32_t g_requestCount{0};
std::atomic_uint64_t g_planckFeatures{0};
std::atomic_uint64_t g_bridgeEpoch{0};
std::atomic_uint32_t g_handoffSequence{0};
std::atomic_uint64_t g_nextHandoffWriteTick{0};
std::atomic_flag g_handoffWriteInProgress = ATOMIC_FLAG_INIT;

bool IsFinite(const float aValue, const float aLimit) noexcept
{
    return std::isfinite(aValue) && std::abs(aValue) <= aLimit;
}

bool IsFiniteVector(const PlanckPluginAPI::Vector3& acValue, const float aLimit) noexcept
{
    return IsFinite(acValue.x, aLimit) && IsFinite(acValue.y, aLimit) && IsFinite(acValue.z, aLimit);
}

bool HasValidNodeName(const char* apName, const std::size_t aLength, const bool aRequired) noexcept
{
    if (!apName || aLength > PlanckPluginAPI::kNodeCapacity || (aRequired && aLength == 0))
        return false;
    for (std::size_t i = 0; i < aLength; ++i)
    {
        const auto character = static_cast<unsigned char>(apName[i]);
        if (character < 0x20 || character == 0x7F)
            return false;
    }
    return true;
}

bool HasValidLocalNodeName(const char (&acName)[PlanckPluginAPI::kNodeCapacity], const bool aRequired) noexcept
{
    std::size_t length = 0;
    while (length < PlanckPluginAPI::kNodeCapacity && acName[length] != '\0')
        ++length;
    return length < PlanckPluginAPI::kNodeCapacity && HasValidNodeName(acName, length, aRequired);
}

bool IsValidGripState(const PlanckPluginAPI::LocalEvent& acEvent) noexcept
{
    const auto& q = acEvent.rotation;
    const float quaternionLengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return IsFiniteVector(acEvent.position, 10000000.0F) &&
           IsFiniteVector(acEvent.linearVelocity, 1000000.0F) &&
           IsFiniteVector(acEvent.angularVelocity, 1000000.0F) &&
           IsFinite(q.x, 1.0F) && IsFinite(q.y, 1.0F) && IsFinite(q.z, 1.0F) && IsFinite(q.w, 1.0F) &&
           quaternionLengthSquared >= 0.25F && quaternionLengthSquared <= 4.0F &&
           IsFinite(acEvent.ttlSeconds, 10.0F) && acEvent.ttlSeconds >= 0.02F && acEvent.ttlSeconds <= 10.0F;
}

bool HasSaneEventFields(const PlanckPluginAPI::LocalEvent& acEvent) noexcept
{
    const auto& q = acEvent.rotation;
    const float quaternionLengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return IsFiniteVector(acEvent.position, 10000000.0F) && IsFiniteVector(acEvent.velocity, 1000000.0F) &&
           IsFiniteVector(acEvent.linearVelocity, 1000000.0F) && IsFiniteVector(acEvent.angularVelocity, 1000000.0F) &&
           IsFiniteVector(acEvent.sourcePosition, 10000000.0F) &&
           IsFinite(q.x, 1.0F) && IsFinite(q.y, 1.0F) && IsFinite(q.z, 1.0F) && IsFinite(q.w, 1.0F) &&
           quaternionLengthSquared >= 0.25F && quaternionLengthSquared <= 4.0F &&
           IsFinite(acEvent.impulseMultiplier, 10.0F) && acEvent.impulseMultiplier >= 0.0F &&
           IsFinite(acEvent.ttlSeconds, 10.0F) && acEvent.ttlSeconds >= 0.0F;
}

bool IsValidLocalEvent(const PlanckPluginAPI::LocalEvent& acEvent) noexcept
{
    if (acEvent.size != sizeof(acEvent) || acEvent.targetFormId == 0 || acEvent.eventId == 0 ||
        acEvent.reserved[0] != 0 || acEvent.reserved[1] != 0 || acEvent.reserved[2] != 0 ||
        !HasValidLocalNodeName(acEvent.nodeName, false) || !HasSaneEventFields(acEvent))
        return false;
    switch (static_cast<SkyrimTogetherVR::PlanckBridge::EventKind>(acEvent.kind))
    {
    case SkyrimTogetherVR::PlanckBridge::EventKind::HitImpulse:
        return HasValidLocalNodeName(acEvent.nodeName, true) && IsFiniteVector(acEvent.position, 10000000.0F) &&
               IsFiniteVector(acEvent.velocity, 1000000.0F) && IsFinite(acEvent.impulseMultiplier, 10.0F) && acEvent.impulseMultiplier >= 0.0F;
    case SkyrimTogetherVR::PlanckBridge::EventKind::RagdollEnter:
        return IsFiniteVector(acEvent.sourcePosition, 10000000.0F);
    case SkyrimTogetherVR::PlanckBridge::EventKind::RagdollExit:
        return true;
    case SkyrimTogetherVR::PlanckBridge::EventKind::GripBegin:
    case SkyrimTogetherVR::PlanckBridge::EventKind::GripUpdate:
        return acEvent.gripId != 0 && HasValidLocalNodeName(acEvent.nodeName, true) && IsValidGripState(acEvent);
    case SkyrimTogetherVR::PlanckBridge::EventKind::GripEnd:
        return acEvent.gripId != 0;
    default:
        return false;
    }
}

bool IsValidRemoteEvent(const SkyrimTogetherVR::PlanckBridge::RemoteEvent& acEvent) noexcept
{
    using namespace SkyrimTogetherVR::PlanckBridge;
    const PlanckPluginAPI::LocalEvent fields{ 0, acEvent.TargetFormId, 0,
        { acEvent.Position.X, acEvent.Position.Y, acEvent.Position.Z },
        { acEvent.Velocity.X, acEvent.Velocity.Y, acEvent.Velocity.Z }, 0, {}, 0, {}, acEvent.GripId,
        { acEvent.Rotation.X, acEvent.Rotation.Y, acEvent.Rotation.Z, acEvent.Rotation.W },
        { acEvent.LinearVelocity.X, acEvent.LinearVelocity.Y, acEvent.LinearVelocity.Z },
        { acEvent.AngularVelocity.X, acEvent.AngularVelocity.Y, acEvent.AngularVelocity.Z },
        { acEvent.SourcePosition.X, acEvent.SourcePosition.Y, acEvent.SourcePosition.Z }, acEvent.ImpulseMultiplier, acEvent.TtlSeconds };
    if (acEvent.Size != sizeof(RemoteEvent) || acEvent.SourceSession == 0 || acEvent.EventId == 0 || acEvent.TargetFormId == 0 ||
        acEvent.NodeNameLength >= kNodeNameCapacity || acEvent.Reserved0[0] != 0 || acEvent.Reserved0[1] != 0 || acEvent.Reserved0[2] != 0 ||
        !HasValidNodeName(acEvent.NodeName, acEvent.NodeNameLength, false) || !HasSaneEventFields(fields))
        return false;
    switch (acEvent.Kind)
    {
    case EventKind::HitImpulse:
        return HasValidNodeName(acEvent.NodeName, acEvent.NodeNameLength, true) &&
               IsFinite(acEvent.Position.X, 10000000.0F) && IsFinite(acEvent.Position.Y, 10000000.0F) && IsFinite(acEvent.Position.Z, 10000000.0F) &&
               IsFinite(acEvent.Velocity.X, 1000000.0F) && IsFinite(acEvent.Velocity.Y, 1000000.0F) && IsFinite(acEvent.Velocity.Z, 1000000.0F) &&
               IsFinite(acEvent.ImpulseMultiplier, 10.0F) && acEvent.ImpulseMultiplier >= 0.0F;
    case EventKind::RagdollEnter:
        return IsFinite(acEvent.SourcePosition.X, 10000000.0F) && IsFinite(acEvent.SourcePosition.Y, 10000000.0F) && IsFinite(acEvent.SourcePosition.Z, 10000000.0F);
    case EventKind::RagdollExit:
        return true;
    case EventKind::GripBegin:
    case EventKind::GripUpdate: {
        return acEvent.GripId != 0 && HasValidNodeName(acEvent.NodeName, acEvent.NodeNameLength, acEvent.Kind == EventKind::GripBegin) && IsValidGripState(fields);
    }
    case EventKind::GripEnd:
        return acEvent.GripId != 0;
    default:
        return false;
    }
}

std::filesystem::path GetHandoffPath()
{
    return SkyrimTogetherVR::Handoff::GetFile("SkyrimTogetherVR.planck");
}

bool EqualsFilenameInsensitive(const std::wstring& acName, const wchar_t* apExpected) noexcept
{
    if (!apExpected)
        return false;

    std::size_t index = 0;
    for (; index < acName.size() && apExpected[index] != L'\0'; ++index)
    {
        if (std::towlower(acName[index]) != std::towlower(apExpected[index]))
            return false;
    }

    return index == acName.size() && apExpected[index] == L'\0';
}

bool HasPluginFile(const std::filesystem::path& acPluginPath, std::initializer_list<const wchar_t*> aPluginNames) noexcept
{
    std::error_code ec;
    if (!std::filesystem::is_directory(acPluginPath, ec))
        return false;

    std::filesystem::directory_iterator it(acPluginPath, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && it != end)
    {
        const auto name = it->path().filename().wstring();
        for (const auto* pExpectedName : aPluginNames)
        {
            if (EqualsFilenameInsensitive(name, pExpectedName))
                return true;
        }

        it.increment(ec);
    }

    return false;
}

bool IsPlanckInstalled()
{
    static const bool installed = []()
    {
        auto pluginPath = GetHandoffPath().parent_path().parent_path() / "SKSE" / "Plugins";
        return HasPluginFile(pluginPath, {L"activeragdoll.dll"});
    }();

    return installed;
}

void WriteBridgeFile(std::uint32_t aSequence, bool aLoaded) noexcept
{
    try
    {
        const auto path = GetHandoffPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        auto tempPath = path;
        tempPath += L".tmp";
        std::ofstream file(tempPath, std::ios::trunc);
        if (!file)
            return;

        auto* const pPlanck = g_planck.load(std::memory_order_acquire);

        file << "bridge.loaded=" << (aLoaded ? "1" : "0") << "\n";
        file << "bridge.sequence=" << aSequence << "\n";
        file << "bridge.epoch=" << g_bridgeEpoch.load(std::memory_order_acquire) << "\n";
        file << "planck.detected=" << (IsPlanckInstalled() || pPlanck ? "1" : "0") << "\n";
        file << "planck.interfaceRequestAttempted=" << (g_requestAttempted.load(std::memory_order_acquire) ? "1" : "0") << "\n";
        file << "planck.interfaceRequestCount=" << g_requestCount.load(std::memory_order_acquire) << "\n";
        file << "planck.interfaceAvailable=" << (pPlanck ? "1" : "0") << "\n";
        file << "planck.interfaceRevision=" << kPlanckInterfaceRevision << "\n";
        file << "planck.features=0x" << std::hex << g_planckFeatures.load(std::memory_order_acquire) << std::dec << "\n";
        file << "planck.interface002RequiredFeatures=0x" << std::hex << SkyrimTogetherVR::PlanckBridge::kRequiredFeatures << std::dec << "\n";
        file << "planck.damageAuthority=none_remote_physics_only\n";
        file << "planck.remotePhysicsReplay=data_only_interface002\n";

        file.close();
        std::filesystem::rename(tempPath, path, ec);
        if (ec)
        {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tempPath, path, ec);
        }
    }
    catch (...)
    {
        // Handoff telemetry must never escape an SKSE callback.
    }
}

void PublishHandoff(bool aForce) noexcept
{
    if (!g_handoffActive.load(std::memory_order_acquire))
        return;

    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    if (!aForce)
    {
        auto next = g_nextHandoffWriteTick.load(std::memory_order_acquire);
        while (now >= next)
        {
            if (g_nextHandoffWriteTick.compare_exchange_weak(
                    next,
                    now + kHandoffWriteIntervalMs,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
                break;
        }

        if (now < next)
            return;
    }
    else
    {
        g_nextHandoffWriteTick.store(now + kHandoffWriteIntervalMs, std::memory_order_release);
    }

    if (g_handoffWriteInProgress.test_and_set(std::memory_order_acquire))
        return;

    WriteBridgeFile(g_handoffSequence.fetch_add(1, std::memory_order_acq_rel) + 1, true);
    g_handoffWriteInProgress.clear(std::memory_order_release);
}

void StartHandoff() noexcept
{
    bool expected = false;
    if (g_handoffActive.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        g_bridgeEpoch.store(static_cast<std::uint64_t>(GetTickCount64()), std::memory_order_release);

    PublishHandoff(true);
}

bool RequestPlanckInterface()
{
    if (g_planck.load(std::memory_order_acquire))
        return true;

    g_requestAttempted.store(true, std::memory_order_release);
    g_requestCount.fetch_add(1, std::memory_order_acq_rel);

    auto* const pMessaging = g_messaging.load(std::memory_order_acquire);
    if (!pMessaging || g_pluginHandle == kPluginHandleInvalid)
        return false;

    PlanckMessage message{};
    if (!pMessaging->Dispatch(g_pluginHandle, kPlanckMessageGetInterface, &message, sizeof(PlanckMessage*), "PLANCK"))
        return false;

    if (!message.GetApiFunction)
        return false;

    auto* const pPlanck = static_cast<PlanckPluginAPI::IPlanckInterface002*>(message.GetApiFunction(kPlanckInterfaceRevision));
    if (!pPlanck)
        return false;
    PlanckPluginAPI::CapsResult capabilities{sizeof(capabilities)};
    const PlanckPluginAPI::CapsRequest request{sizeof(request), 0};
    const auto result = pPlanck->GetCapabilities(request, capabilities);
    if (result.code != PlanckPluginAPI::ResultCode::Accepted ||
        capabilities.interfaceRevision != kPlanckInterfaceRevision ||
        (capabilities.featureBits & SkyrimTogetherVR::PlanckBridge::kRequiredFeatures) !=
            SkyrimTogetherVR::PlanckBridge::kRequiredFeatures)
        return false;
    g_planckFeatures.store(capabilities.featureBits, std::memory_order_release);
    g_planck.store(pPlanck, std::memory_order_release);
    return true;
}

void OnSkseMessage(SKSEMessagingInterface::Message* apMessage)
{
    if (!apMessage)
        return;

    // PLANCK's public API permits interface acquisition only after this
    // broadcast. The initial write from SKSEPlugin_Load remains diagnostics
    // only and never dereferences PLANCK.
    if (apMessage->type == kSkseMessagePostPostLoad)
    {
        RequestPlanckInterface();
        StartHandoff();
    }
}
}

extern "C" __declspec(dllexport) SkyrimTogetherVR::PlanckBridge::Result
SkyrimTogetherVR_Planck002_GetCapabilities(SkyrimTogetherVR::PlanckBridge::Capabilities* apResult) noexcept
{
    using namespace SkyrimTogetherVR::PlanckBridge;
    if (!apResult || apResult->Size != sizeof(Capabilities)) return Result::Rejected;
    const auto* api = g_planck.load(std::memory_order_acquire);
    if (!api) return Result::Unavailable;
    apResult->AbiRevision = kAbiRevision; apResult->InterfaceRevision = kPlanckInterfaceRevision;
    apResult->Reserved = 0; apResult->Features = g_planckFeatures.load(std::memory_order_acquire);
    apResult->BridgeEpoch = g_bridgeEpoch.load(std::memory_order_acquire);
    return (apResult->Features & kRequiredFeatures) == kRequiredFeatures ? Result::Accepted : Result::Unavailable;
}

extern "C" __declspec(dllexport) SkyrimTogetherVR::PlanckBridge::Result
SkyrimTogetherVR_Planck002_DequeueLocalEvent(SkyrimTogetherVR::PlanckBridge::LocalEvent* apResult) noexcept
{
    using namespace SkyrimTogetherVR::PlanckBridge;
    if (!apResult || apResult->Size != sizeof(LocalEvent)) return Result::Rejected;
    auto* api = g_planck.load(std::memory_order_acquire); if (!api) return Result::Unavailable;
    PlanckPluginAPI::LocalEvent event{sizeof(event)};
    const auto result = api->DequeueLocalPhysicalEvent({sizeof(PlanckPluginAPI::DequeueRequest), 0}, event);
    if (result.code == PlanckPluginAPI::ResultCode::Empty) return Result::Empty;
    if (result.code != PlanckPluginAPI::ResultCode::Accepted || !IsValidLocalEvent(event)) return Result::Rejected;
    apResult->TargetFormId = event.targetFormId; apResult->EventId = event.eventId;
    apResult->Position = {event.position.x, event.position.y, event.position.z}; apResult->Velocity = {event.velocity.x, event.velocity.y, event.velocity.z}; apResult->Flags = event.flags;
    apResult->Kind = static_cast<EventKind>(event.kind); apResult->Reserved[0] = apResult->Reserved[1] = apResult->Reserved[2] = 0;
    apResult->GripId = event.gripId; apResult->Rotation = {event.rotation.x, event.rotation.y, event.rotation.z, event.rotation.w};
    apResult->LinearVelocity = {event.linearVelocity.x, event.linearVelocity.y, event.linearVelocity.z};
    apResult->AngularVelocity = {event.angularVelocity.x, event.angularVelocity.y, event.angularVelocity.z};
    apResult->SourcePosition = {event.sourcePosition.x, event.sourcePosition.y, event.sourcePosition.z};
    apResult->ImpulseMultiplier = event.impulseMultiplier; apResult->TtlSeconds = event.ttlSeconds;
    std::memcpy(apResult->NodeName, event.nodeName, sizeof(apResult->NodeName));
    return Result::Accepted;
}

extern "C" __declspec(dllexport) SkyrimTogetherVR::PlanckBridge::Result
SkyrimTogetherVR_Planck002_SubmitRemoteEvent(const SkyrimTogetherVR::PlanckBridge::RemoteEvent* apEvent) noexcept
{
    using namespace SkyrimTogetherVR::PlanckBridge;
    if (!apEvent || !IsValidRemoteEvent(*apEvent)) return Result::Rejected;
    auto* api = g_planck.load(std::memory_order_acquire); if (!api) return Result::Unavailable;
    const auto header = [apEvent](const std::uint32_t aSize) {
        return PlanckPluginAPI::Header{aSize, 0, apEvent->SourceSession, apEvent->EventId};
    };
    char node[kNodeNameCapacity]{}; if (apEvent->NodeNameLength != 0) std::memcpy(node, apEvent->NodeName, apEvent->NodeNameLength);
    const auto vector = [](const Vector3& v) { return PlanckPluginAPI::Vector3{v.X, v.Y, v.Z}; };
    PlanckPluginAPI::Result result{};
    switch (apEvent->Kind) {
    case EventKind::HitImpulse: { PlanckPluginAPI::Hit request{header(sizeof(PlanckPluginAPI::Hit)), apEvent->TargetFormId, apEvent->ImpulseMultiplier, {}, vector(apEvent->Position), vector(apEvent->Velocity)}; std::memcpy(request.nodeName, node, sizeof(node)); result = api->SubmitRemoteHitImpulse(request); break; }
    case EventKind::RagdollEnter: result = api->SubmitRemoteRagdoll({header(sizeof(PlanckPluginAPI::Ragdoll)), apEvent->TargetFormId, 0, vector(apEvent->SourcePosition)}); break;
    case EventKind::RagdollExit: result = api->SubmitRemoteRagdollExit({header(sizeof(PlanckPluginAPI::RagdollExit)), apEvent->TargetFormId, 0}); break;
    case EventKind::GripBegin: { PlanckPluginAPI::BeginGrip request{header(sizeof(PlanckPluginAPI::BeginGrip)), apEvent->GripId, apEvent->TargetFormId, {}, {vector(apEvent->Position), {apEvent->Rotation.X, apEvent->Rotation.Y, apEvent->Rotation.Z, apEvent->Rotation.W}, vector(apEvent->LinearVelocity), vector(apEvent->AngularVelocity), apEvent->TtlSeconds, 0}}; std::memcpy(request.nodeName, node, sizeof(node)); result = api->BeginRemoteGrip(request); break; }
    case EventKind::GripUpdate: result = api->UpdateRemoteGrip({header(sizeof(PlanckPluginAPI::UpdateGrip)), apEvent->GripId, apEvent->TargetFormId, 0, {vector(apEvent->Position), {apEvent->Rotation.X, apEvent->Rotation.Y, apEvent->Rotation.Z, apEvent->Rotation.W}, vector(apEvent->LinearVelocity), vector(apEvent->AngularVelocity), apEvent->TtlSeconds, 0}}); break;
    case EventKind::GripEnd: result = api->EndRemoteGrip({header(sizeof(PlanckPluginAPI::EndGrip)), apEvent->GripId, apEvent->TargetFormId, 0}); break;
    default: return Result::Rejected;
    }
    return MapPlanckSubmitResult(static_cast<std::uint32_t>(result.code));
}

extern "C" __declspec(dllexport) SkyrimTogetherVR::PlanckBridge::Result
SkyrimTogetherVR_Planck002_ClearRemoteSession(const std::uint64_t aSession, const std::uint64_t aEventId) noexcept
{
    auto* api = g_planck.load(std::memory_order_acquire);
    if (!api) return SkyrimTogetherVR::PlanckBridge::Result::Unavailable;
    if (aSession == 0 || aEventId == 0) return SkyrimTogetherVR::PlanckBridge::Result::Rejected;
    const auto result = api->ClearRemoteSession({{sizeof(PlanckPluginAPI::ClearSession), 0, aSession, aEventId}});
    using namespace SkyrimTogetherVR::PlanckBridge;
    return MapPlanckSubmitResult(static_cast<std::uint32_t>(result.code));
}

extern "C" __declspec(dllexport) SkyrimTogetherVR::PlanckBridge::Result
SkyrimTogetherVR_Planck002_DiscardLocalEvents(const std::uint64_t aLifecycleGeneration) noexcept
{
    auto* api = g_planck.load(std::memory_order_acquire);
    if (!api)
        return SkyrimTogetherVR::PlanckBridge::Result::Unavailable;
    const auto result = api->DiscardLocalPhysicalEvents(
        {sizeof(PlanckPluginAPI::DiscardLocalEvents), 0, aLifecycleGeneration});
    using namespace SkyrimTogetherVR::PlanckBridge;
    return MapPlanckSubmitResult(static_cast<std::uint32_t>(result.code));
}

// HIGGS invokes the VRIK bridge on its post-VRIK game-thread callback. That is
// the only periodic callback available to this bridge family; it replaces the
// old background writer without risking code execution after DLL unload.
extern "C" __declspec(dllexport) void __cdecl SkyrimTogetherVR_PumpPlanckHandoff() noexcept
{
    PublishHandoff(false);
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSEInterface* apSkse, PluginInfo* apInfo)
{
    if (!apSkse || !apInfo || apSkse->isEditor)
        return false;

    apInfo->infoVersion = kPluginInfoVersion;
    apInfo->name = "SkyrimTogetherVRPlanckBridge";
    apInfo->version = 1;

    if (!apSkse->GetPluginHandle)
        return false;

    g_pluginHandle = apSkse->GetPluginHandle();
    return g_pluginHandle != kPluginHandleInvalid;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse || !apSkse->QueryInterface)
        return false;

    auto* const pMessaging = static_cast<SKSEMessagingInterface*>(apSkse->QueryInterface(kInterfaceMessaging));
    if (g_pluginHandle == kPluginHandleInvalid || !pMessaging || !pMessaging->RegisterListener)
        return false;

    if (!pMessaging->RegisterListener(g_pluginHandle, "SKSE", reinterpret_cast<void*>(OnSkseMessage)))
        return false;

    g_messaging.store(pMessaging, std::memory_order_release);
    StartHandoff();
    return true;
}
