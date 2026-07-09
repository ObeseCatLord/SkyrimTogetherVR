#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>

#include <vr_common/VRHandoffPath.h>

using PluginHandle = std::uint32_t;

static constexpr PluginHandle kPluginHandleInvalid = static_cast<PluginHandle>(-1);
static constexpr std::uint32_t kPluginInfoVersion = 1;
static constexpr std::uint32_t kInterfaceMessaging = 5;
static constexpr std::uint32_t kSkseMessagePostPostLoad = 1;
static constexpr std::uint32_t kPlanckMessageGetInterface = 0x92F38745;

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

struct NiPoint3
{
    float x;
    float y;
    float z;
};

struct PlanckHitDataBoundary
{
    NiPoint3 position;
    NiPoint3 velocity;
    void* node;
    const char* nodeName;
    bool isLeft;
};

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
SKSEMessagingInterface* g_messaging = nullptr;
std::atomic<PlanckPluginAPI::IPlanckInterface001*> g_planck{nullptr};
std::atomic_bool g_running{false};
std::atomic_bool g_requestAttempted{false};
std::atomic_uint32_t g_planckBuildNumber{0};
std::atomic_uint64_t g_bridgeEpoch{0};
std::thread g_writerThread;

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

void WriteBridgeFile(std::uint32_t aSequence, bool aLoaded)
{
    const auto path = GetHandoffPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const auto tempPath = path.string() + ".tmp";
    std::ofstream file(tempPath, std::ios::trunc);
    if (!file)
        return;

    auto* const pPlanck = g_planck.load(std::memory_order_acquire);

    file << "bridge.loaded=" << (aLoaded ? "1" : "0") << "\n";
    file << "bridge.sequence=" << aSequence << "\n";
    file << "bridge.epoch=" << g_bridgeEpoch.load(std::memory_order_acquire) << "\n";
    file << "planck.detected=" << (IsPlanckInstalled() || pPlanck ? "1" : "0") << "\n";
    file << "planck.interfaceRequestAttempted=" << (g_requestAttempted.load(std::memory_order_acquire) ? "1" : "0") << "\n";
    file << "planck.interfaceAvailable=" << (pPlanck ? "1" : "0") << "\n";
    file << "planck.buildNumber=" << g_planckBuildNumber.load(std::memory_order_acquire) << "\n";
    file << "planck.currentHitEventAddress=0\n";
    file << "planck.currentHitEventAvailable=0\n";
    file << "planck.currentHitEventObservationOnly=1\n";
    file << "planck.lastHitDataAvailable=0\n";
    file << "planck.lastHitDataProbeEnabled=0\n";
    file << "planck.lastHitDataReason=not_polled_nontrivial_return_boundary\n";
    file << "planck.lastHitDataBoundary=disabled_unvalidated_by_value_abi\n";
    file << "planck.policy=observation_only\n";

    file.close();
    std::filesystem::rename(tempPath, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tempPath, path, ec);
    }
}

void WriterMain()
{
    std::uint32_t sequence = 0;
    while (g_running)
    {
        WriteBridgeFile(++sequence, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    WriteBridgeFile(++sequence, false);
}

void StartWriter()
{
    if (g_running.exchange(true))
        return;

    g_bridgeEpoch.store(static_cast<std::uint64_t>(GetTickCount64()), std::memory_order_release);
    g_writerThread = std::thread(WriterMain);
}

void StopWriter()
{
    if (!g_running.exchange(false))
        return;

    if (g_writerThread.joinable())
        g_writerThread.join();
}

bool RequestPlanckInterface()
{
    g_requestAttempted.store(true, std::memory_order_release);

    if (!g_messaging || g_pluginHandle == kPluginHandleInvalid)
        return false;

    PlanckMessage message{};
    if (!g_messaging->Dispatch(g_pluginHandle, kPlanckMessageGetInterface, &message, sizeof(PlanckMessage*), "PLANCK"))
        return false;

    if (!message.GetApiFunction)
        return false;

    auto* const pPlanck = static_cast<PlanckPluginAPI::IPlanckInterface001*>(message.GetApiFunction(1));
    g_planck.store(pPlanck, std::memory_order_release);
    if (pPlanck)
        g_planckBuildNumber.store(pPlanck->GetBuildNumber(), std::memory_order_release);
    return pPlanck != nullptr;
}

void OnSkseMessage(SKSEMessagingInterface::Message* apMessage)
{
    if (!apMessage)
        return;

    if (apMessage->type == kSkseMessagePostPostLoad)
    {
        RequestPlanckInterface();
        StartWriter();
    }
}

struct ShutdownJoiner
{
    ~ShutdownJoiner()
    {
        StopWriter();
    }
};

ShutdownJoiner g_shutdownJoiner;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSEInterface* apSkse, PluginInfo* apInfo)
{
    if (!apSkse || !apInfo || apSkse->isEditor)
        return false;

    apInfo->infoVersion = kPluginInfoVersion;
    apInfo->name = "SkyrimTogetherVRPlanckBridge";
    apInfo->version = 1;

    g_pluginHandle = apSkse->GetPluginHandle ? apSkse->GetPluginHandle() : kPluginHandleInvalid;
    return true;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    if (!apSkse || !apSkse->QueryInterface)
        return false;

    g_messaging = static_cast<SKSEMessagingInterface*>(apSkse->QueryInterface(kInterfaceMessaging));
    if (!g_messaging || !g_messaging->RegisterListener)
        return false;

    g_messaging->RegisterListener(g_pluginHandle, "SKSE", reinterpret_cast<void*>(OnSkseMessage));
    StartWriter();
    return true;
}
