#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>

#include <vr_common/VRHandoffPath.h>

using PluginHandle = std::uint32_t;

static constexpr PluginHandle kPluginHandleInvalid = static_cast<PluginHandle>(-1);
static constexpr std::uint32_t kPluginInfoVersion = 1;
static constexpr std::uint32_t kInterfaceMessaging = 5;
static constexpr std::uint32_t kSkseMessagePostPostLoad = 1;
static constexpr std::uint32_t kPlanckMessageGetInterface = 0x92F38745;
static constexpr std::uint32_t kPlanckInterfaceRevision = 1;
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

struct Actor;
struct TESTopic;
struct TESHitEvent;
struct NiAVObject;

struct NiPoint3
{
    float x;
    float y;
    float z;
};

template <class T>
struct NiPointerBoundary
{
    T* data = nullptr;
};

struct BSFixedStringBoundary
{
    const char* data = nullptr;
};

// This is intentionally a layout-only mirror. GetLastHitData is not polled:
// PLANCK supplies no hit callback, and its current-event pointer is valid only
// during the engine's hit-event dispatch. Polling either from this bridge would
// create an unauthoritative second combat path.
struct PlanckHitDataBoundary
{
    NiPoint3 position;
    NiPoint3 velocity;
    NiPointerBoundary<NiAVObject> node;
    BSFixedStringBoundary nodeName;
    bool isLeft;
};

static_assert(sizeof(NiPointerBoundary<NiAVObject>) == sizeof(void*));
static_assert(sizeof(BSFixedStringBoundary) == sizeof(void*));
static_assert(sizeof(PlanckHitDataBoundary) == 48);

namespace PlanckPluginAPI
{
struct IPlanckInterface001
{
    virtual unsigned int GetBuildNumber() = 0;
    virtual bool Deprecated1(const std::string_view& name, double& out) = 0;
    virtual bool Deprecated2(const std::string& name, double val) = 0;
    virtual void AddIgnoredActor(Actor* actor) = 0;
    virtual void RemoveIgnoredActor(Actor* actor) = 0;
    virtual void AddAggressionIgnoredActor(Actor* actor) = 0;
    virtual void RemoveAggressionIgnoredActor(Actor* actor) = 0;
    virtual void SetAggressionLowTopic(Actor* actor, TESTopic* topic) = 0;
    virtual void SetAggressionHighTopic(Actor* actor, TESTopic* topic) = 0;
    virtual void AddRagdollCollisionIgnoredActor(Actor* actor) = 0;
    virtual void RemoveRagdollCollisionIgnoredActor(Actor* actor) = 0;
    virtual PlanckHitDataBoundary GetLastHitData() = 0;
    virtual TESHitEvent* GetCurrentHitEvent() = 0;
    virtual bool GetSettingDouble(const char* name, double& out) = 0;
    virtual bool SetSettingDouble(const char* name, double val) = 0;
};
}

namespace
{
struct PlanckMessage
{
    void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
};

PluginHandle g_pluginHandle = kPluginHandleInvalid;
std::atomic<SKSEMessagingInterface*> g_messaging{nullptr};
std::atomic<PlanckPluginAPI::IPlanckInterface001*> g_planck{nullptr};
std::atomic_bool g_handoffActive{false};
std::atomic_bool g_requestAttempted{false};
std::atomic_uint32_t g_requestCount{0};
std::atomic_uint32_t g_planckBuildNumber{0};
std::atomic_uint64_t g_bridgeEpoch{0};
std::atomic_uint32_t g_handoffSequence{0};
std::atomic_uint64_t g_nextHandoffWriteTick{0};
std::atomic_flag g_handoffWriteInProgress = ATOMIC_FLAG_INIT;

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
        file << "planck.buildNumber=" << g_planckBuildNumber.load(std::memory_order_acquire) << "\n";
        file << "planck.buildNumberMatches0080="
             << (g_planckBuildNumber.load(std::memory_order_acquire) == kPlanck0080BuildNumber ? "1" : "0") << "\n";
        file << "planck.currentHitEventAddress=0\n";
        file << "planck.currentHitEventAvailable=0\n";
        file << "planck.currentHitEventObservationOnly=1\n";
        file << "planck.lastHitDataAvailable=0\n";
        file << "planck.lastHitDataProbeEnabled=0\n";
        file << "planck.lastHitDataReason=no_stable_hit_callback_or_canonical_action_producer\n";
        file << "planck.lastHitDataBoundary=not_invoked_no_authoritative_transport\n";
        file << "planck.observationBufferCapacity=0\n";
        file << "planck.damageAuthority=canonical_engine_hit_events\n";
        file << "planck.remotePhysicsReplay=unsupported_public_api\n";
        file << "planck.policy=canonical_combat_observation_only\n";

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

    auto* const pPlanck = static_cast<PlanckPluginAPI::IPlanckInterface001*>(message.GetApiFunction(kPlanckInterfaceRevision));
    if (!pPlanck)
        return false;

    // Revision 1 defines this first virtual slot. The build number is recorded
    // for diagnostics only; later PLANCK builds retaining revision 1 are valid.
    const auto buildNumber = pPlanck->GetBuildNumber();
    g_planckBuildNumber.store(buildNumber, std::memory_order_release);
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
