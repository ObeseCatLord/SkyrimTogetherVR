#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include <vr_common/VRHandoffPath.h>

using PluginHandle = std::uint32_t;

static constexpr PluginHandle kPluginHandleInvalid = static_cast<PluginHandle>(-1);
static constexpr std::uint32_t kPluginInfoVersion = 1;
static constexpr std::uint32_t kInterfaceMessaging = 5;
static constexpr std::uint32_t kSkseMessagePostPostLoad = 1;
static constexpr std::uint32_t kVrikMessageGetInterface = 0xF2AFAEE6;

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

struct NiPoint3
{
    float x;
    float y;
    float z;
};

namespace vrikPluginApi
{
class IVrikInterface001
{
public:
    virtual unsigned int getBuildNumber() = 0;
    virtual double getSettingDouble(const char* name) = 0;
    virtual void setSettingDouble(const char* name, double value) = 0;
    virtual void getSettingString(const char* name, char* buffer, size_t bufferSize) = 0;
    virtual void setSettingString(const char* name, const char* value) = 0;
    virtual void saveSettings() = 0;
    virtual void restoreSettings() = 0;

    using GestureCallback = void (*)(int pressCount);
    virtual void addGestureAction(GestureCallback callback, const char* mcmMenuName) = 0;
    virtual void beginGestureProfile() = 0;
    virtual void setProfileAction(int gestureNumber, GestureCallback callback) = 0;
    virtual void endGestureProfile() = 0;

    virtual float getFingerPos(bool isLeft, int fingerIndex) = 0;
    virtual void setFingerRange(bool isLeft, float min0, float max0, float min1, float max1, float min2, float max2, float min3, float max3, float min4, float max4) = 0;
    virtual void restoreFingers(bool isLeft) = 0;
    virtual NiPoint3 getCameraOffsettingAmount() = 0;
    virtual NiPoint3 getFinalCameraOffsettingAmount() = 0;
    virtual NiPoint3 getFinalSmoothingOffsettingAmount() = 0;
};
}

namespace
{
struct VrikMessage
{
    void* (*getApiFunction)(unsigned int revisionNumber) = nullptr;
};

struct VrikFingerSnapshot
{
    bool Valid{};
    float Values[5]{};
};

struct VrikSnapshot
{
    unsigned int BuildNumber{};
    VrikFingerSnapshot LeftFingers{};
    VrikFingerSnapshot RightFingers{};
    bool CameraOffsetsValid{};
    NiPoint3 CameraOffset{};
    NiPoint3 FinalCameraOffset{};
    NiPoint3 FinalSmoothingOffset{};
};

PluginHandle g_pluginHandle = kPluginHandleInvalid;
SKSEMessagingInterface* g_messaging = nullptr;
std::atomic<vrikPluginApi::IVrikInterface001*> g_vrik{nullptr};
std::atomic_bool g_running{false};
std::atomic_bool g_snapshotAvailable{false};
std::atomic_uint64_t g_bridgeEpoch{0};
std::thread g_writerThread;
std::mutex g_snapshotLock;
VrikSnapshot g_latestSnapshot;

std::filesystem::path GetHandoffPath()
{
    return SkyrimTogetherVR::Handoff::GetFile("SkyrimTogetherVR.vrik");
}

bool IsVrikInstalled()
{
    static const bool installed = []()
    {
        std::error_code ec;
        auto pluginPath = GetHandoffPath().parent_path().parent_path() / "SKSE" / "Plugins";
        return std::filesystem::exists(pluginPath / "vrik.dll", ec) ||
               std::filesystem::exists(pluginPath / "VRIK.dll", ec);
    }();

    return installed;
}

void WritePoint(std::ofstream& aFile, const char* apName, NiPoint3 aPoint)
{
    aFile << apName << "=" << aPoint.x << "," << aPoint.y << "," << aPoint.z << "\n";
}

void CaptureFingerSnapshot(VrikFingerSnapshot& aSnapshot, bool aIsLeft, vrikPluginApi::IVrikInterface001* apVrik)
{
    if (!apVrik)
        return;

    aSnapshot.Valid = true;
    for (int i = 0; i < 5; ++i)
        aSnapshot.Values[i] = apVrik->getFingerPos(aIsLeft, i);
}

void CaptureVrikSnapshot()
{
    auto* const pVrik = g_vrik.load(std::memory_order_acquire);
    if (!pVrik)
        return;

    VrikSnapshot snapshot{};
    snapshot.BuildNumber = pVrik->getBuildNumber();
    CaptureFingerSnapshot(snapshot.LeftFingers, true, pVrik);
    CaptureFingerSnapshot(snapshot.RightFingers, false, pVrik);
    snapshot.CameraOffsetsValid = true;
    snapshot.CameraOffset = pVrik->getCameraOffsettingAmount();
    snapshot.FinalCameraOffset = pVrik->getFinalCameraOffsettingAmount();
    snapshot.FinalSmoothingOffset = pVrik->getFinalSmoothingOffsettingAmount();

    {
        std::lock_guard lock(g_snapshotLock);
        g_latestSnapshot = snapshot;
    }
    g_snapshotAvailable.store(true, std::memory_order_release);
}

VrikSnapshot CopyLatestSnapshot(bool& aAvailable)
{
    aAvailable = g_snapshotAvailable.load(std::memory_order_acquire);
    if (!aAvailable)
        return {};

    std::lock_guard lock(g_snapshotLock);
    return g_latestSnapshot;
}

void WriteFingerState(std::ofstream& aFile, const char* apPrefix, const VrikFingerSnapshot* apFingers)
{
    if (!apFingers || !apFingers->Valid)
    {
        aFile << apPrefix << ".valid=0\n";
        return;
    }

    aFile << apPrefix << ".valid=1\n";
    aFile << apPrefix << ".thumb=" << apFingers->Values[0] << "\n";
    aFile << apPrefix << ".index=" << apFingers->Values[1] << "\n";
    aFile << apPrefix << ".middle=" << apFingers->Values[2] << "\n";
    aFile << apPrefix << ".ring=" << apFingers->Values[3] << "\n";
    aFile << apPrefix << ".pinky=" << apFingers->Values[4] << "\n";
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

    auto* const pVrik = g_vrik.load(std::memory_order_acquire);
    bool snapshotAvailable = false;
    const auto snapshot = CopyLatestSnapshot(snapshotAvailable);

    file << "bridge.loaded=" << (aLoaded ? "1" : "0") << "\n";
    file << "bridge.sequence=" << aSequence << "\n";
    file << "bridge.epoch=" << g_bridgeEpoch.load(std::memory_order_acquire) << "\n";
    file << "vrik.detected=" << (IsVrikInstalled() || pVrik ? "1" : "0") << "\n";
    file << "vrik.interfaceAvailable=" << (pVrik ? "1" : "0") << "\n";
    file << "vrik.snapshotAvailable=" << (snapshotAvailable ? "1" : "0") << "\n";

    if (aLoaded && pVrik && snapshotAvailable)
    {
        file << "vrik.buildNumber=" << snapshot.BuildNumber << "\n";
        WriteFingerState(file, "leftFingers", &snapshot.LeftFingers);
        WriteFingerState(file, "rightFingers", &snapshot.RightFingers);
        file << "cameraOffsetsValid=" << (snapshot.CameraOffsetsValid ? "1" : "0") << "\n";
        if (snapshot.CameraOffsetsValid)
        {
            WritePoint(file, "cameraOffset", snapshot.CameraOffset);
            WritePoint(file, "finalCameraOffset", snapshot.FinalCameraOffset);
            WritePoint(file, "finalSmoothingOffset", snapshot.FinalSmoothingOffset);
        }
    }
    else
    {
        file << "vrik.buildNumber=0\n";
        WriteFingerState(file, "leftFingers", nullptr);
        WriteFingerState(file, "rightFingers", nullptr);
        file << "cameraOffsetsValid=0\n";
    }

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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

bool RequestVrikInterface()
{
    if (!g_messaging || g_pluginHandle == kPluginHandleInvalid)
        return false;

    VrikMessage message{};
    if (!g_messaging->Dispatch(g_pluginHandle, kVrikMessageGetInterface, &message, sizeof(VrikMessage*), "VRIK"))
        return false;

    if (!message.getApiFunction)
        return false;

    auto* const pVrik = static_cast<vrikPluginApi::IVrikInterface001*>(message.getApiFunction(1));
    g_vrik.store(pVrik, std::memory_order_release);
    CaptureVrikSnapshot();
    return pVrik != nullptr;
}

void OnSkseMessage(SKSEMessagingInterface::Message* apMessage)
{
    if (!apMessage)
        return;

    if (apMessage->type == kSkseMessagePostPostLoad)
    {
        RequestVrikInterface();
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
    apInfo->name = "SkyrimTogetherVRVrikBridge";
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
