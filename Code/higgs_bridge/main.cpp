#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <vr_common/VRHandoffPath.h>
#include <vr_common/VRTickBridge.h>

using PluginHandle = std::uint32_t;

static constexpr PluginHandle kPluginHandleInvalid = static_cast<PluginHandle>(-1);
static constexpr std::uint32_t kPluginInfoVersion = 1;
static constexpr std::uint32_t kInterfaceMessaging = 5;
static constexpr std::uint32_t kSkseMessagePostLoad = 0;
static constexpr std::uint32_t kSkseMessagePostPostLoad = 1;
static constexpr std::uint32_t kHiggsMessageGetInterface = 0xF9279A57;
static constexpr std::uintptr_t kTesFormFormIdOffset = 0x14;
static constexpr std::size_t kMaxRecentEvents = 32;

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

struct TESForm;
struct TESObjectREFR;
struct NiObject;

struct NiPoint3
{
    float x;
    float y;
    float z;
};

struct NiMatrix3
{
    float entries[3][3];
};

struct NiTransform
{
    NiMatrix3 rotate;
    NiPoint3 translate;
    float scale;
};

struct BSFixedString
{
    const char* data;
};

namespace HiggsPluginAPI
{
struct IHiggsInterface001
{
    virtual unsigned int GetBuildNumber() = 0;

    using PulledCallback = void (*)(bool isLeft, TESObjectREFR* pulledRefr);
    virtual void AddPulledCallback(PulledCallback callback) = 0;
    using GrabbedCallback = void (*)(bool isLeft, TESObjectREFR* grabbedRefr);
    virtual void AddGrabbedCallback(GrabbedCallback callback) = 0;
    using DroppedCallback = void (*)(bool isLeft, TESObjectREFR* droppedRefr);
    virtual void AddDroppedCallback(DroppedCallback callback) = 0;
    using StashedCallback = void (*)(bool isLeft, TESForm* stashedForm);
    virtual void AddStashedCallback(StashedCallback callback) = 0;
    using ConsumedCallback = void (*)(bool isLeft, TESForm* consumedForm);
    virtual void AddConsumedCallback(ConsumedCallback callback) = 0;
    using CollisionCallback = void (*)(bool isLeft, float mass, float separatingVelocity);
    virtual void AddCollisionCallback(CollisionCallback callback) = 0;

    virtual void GrabObject(TESObjectREFR* object, bool isLeft) = 0;
    virtual TESObjectREFR* GetGrabbedObject(bool isLeft) = 0;
    virtual bool IsHandInGrabbableState(bool isLeft) = 0;
    virtual void DisableHand(bool isLeft) = 0;
    virtual void EnableHand(bool isLeft) = 0;
    virtual bool IsDisabled(bool isLeft) = 0;
    virtual void DisableWeaponCollision(bool isLeft) = 0;
    virtual void EnableWeaponCollision(bool isLeft) = 0;
    virtual bool IsWeaponCollisionDisabled(bool isLeft) = 0;
    virtual bool IsTwoHanding() = 0;

    using StartTwoHandingCallback = void (*)();
    virtual void AddStartTwoHandingCallback(StartTwoHandingCallback callback) = 0;
    using StopTwoHandingCallback = void (*)();
    virtual void AddStopTwoHandingCallback(StopTwoHandingCallback callback) = 0;

    virtual bool CanGrabObject(bool isLeft) = 0;

    enum class CollisionFilterComparisonResult : std::uint8_t
    {
        Continue,
        Collide,
        Ignore,
    };
    using CollisionFilterComparisonCallback = CollisionFilterComparisonResult (*)(void* collisionFilter, std::uint32_t filterInfoA, std::uint32_t filterInfoB);
    virtual void AddCollisionFilterComparisonCallback(CollisionFilterComparisonCallback callback) = 0;
    using PrePhysicsStepCallback = void (*)(void* world);
    virtual void AddPrePhysicsStepCallback(PrePhysicsStepCallback callback) = 0;

    virtual std::uint64_t GetHiggsLayerBitfield() = 0;
    virtual void SetHiggsLayerBitfield(std::uint64_t bitfield) = 0;
    virtual NiObject* GetHandRigidBody(bool isLeft) = 0;
    virtual NiObject* GetWeaponRigidBody(bool isLeft) = 0;
    virtual NiObject* GetGrabbedRigidBody(bool isLeft) = 0;
    virtual void ForceWeaponCollisionEnabled(bool isLeft) = 0;
    virtual bool IsHoldingObject(bool isLeft) = 0;
    virtual void GetFingerValues(bool isLeft, float values[5]) = 0;

    using NoArgCallback = void (*)();
    virtual void AddPreVrikPreHiggsCallback(NoArgCallback callback) = 0;
    virtual void AddPreVrikPostHiggsCallback(NoArgCallback callback) = 0;
    virtual void AddPostVrikPreHiggsCallback(NoArgCallback callback) = 0;
    virtual void AddPostVrikPostHiggsCallback(NoArgCallback callback) = 0;

    virtual bool Deprecated1(const std::string_view& name, double& out) = 0;
    virtual bool Deprecated2(const std::string& name, double val) = 0;
    virtual NiTransform GetGrabTransform(bool isLeft) = 0;
    virtual void SetGrabTransform(bool isLeft, const NiTransform& transform) = 0;
    virtual bool GetSettingDouble(const char* name, double& out) = 0;
    virtual bool SetSettingDouble(const char* name, double val) = 0;
    virtual BSFixedString GetGrabbedNodeName(bool isLeft) = 0;
};
}

namespace
{
struct HiggsMessage
{
    void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
};

struct HiggsEvent
{
    std::uint32_t Sequence{};
    const char* Type{};
    bool IsLeft{};
    bool HasHand{};
    std::uintptr_t ObjectAddress{};
    std::uint32_t FormId{};
    float Mass{};
    float SeparatingVelocity{};
};

struct HiggsHandSnapshot
{
    bool Valid{};
    bool HoldingObject{};
    bool CanGrabObject{};
    bool HandInGrabbableState{};
    bool Disabled{};
    bool WeaponCollisionDisabled{};
    std::uintptr_t GrabbedObjectAddress{};
    std::uint32_t GrabbedObjectFormId{};
    float Fingers[5]{};
    bool GrabTransformValid{};
    NiTransform GrabTransform{};
};

struct HiggsSnapshot
{
    std::uint32_t Sequence{};
    unsigned int BuildNumber{};
    bool TwoHanding{};
    HiggsHandSnapshot Left{};
    HiggsHandSnapshot Right{};
};

PluginHandle g_pluginHandle = kPluginHandleInvalid;
SKSEMessagingInterface* g_messaging = nullptr;
std::atomic<HiggsPluginAPI::IHiggsInterface001*> g_higgs{nullptr};
std::atomic_bool g_callbacksRegistered{false};
std::atomic_bool g_snapshotAvailable{false};
std::atomic_bool g_running{false};
std::atomic_uint32_t g_eventSequence{0};
std::atomic_uint32_t g_snapshotSequence{0};
std::atomic_uint64_t g_bridgeEpoch{0};
std::atomic_uint64_t g_bodyCaptureSequence{0};
std::atomic_uint64_t g_bodyCaptureAttemptCount{0};
std::atomic_uint64_t g_bodyCaptureSuccessCount{0};
std::atomic_uint32_t g_bodyCaptureLastResult{static_cast<std::uint32_t>(SkyrimTogetherVR::TickBridge::DispatchResult::Inactive)};
std::atomic_bool g_endpointFaulted{false};
std::thread g_writerThread;
std::mutex g_eventLock;
std::deque<HiggsEvent> g_recentEvents;
std::mutex g_snapshotLock;
HiggsSnapshot g_latestSnapshot;
std::atomic<SkyrimTogetherVR::TickBridge::Endpoint*> g_endpoint{nullptr};

LONG ReadEndpointState(const SkyrimTogetherVR::TickBridge::Endpoint& acEndpoint) noexcept
{
    return InterlockedCompareExchange(
        const_cast<volatile LONG*>(reinterpret_cast<const volatile LONG*>(&acEndpoint.State)), 0, 0);
}

bool IsExecutableReadOnlyPage(DWORD aProtection) noexcept
{
    if (aProtection & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    const auto baseProtection = aProtection & 0xFFu;
    return baseProtection == PAGE_EXECUTE || baseProtection == PAGE_EXECUTE_READ;
}

bool MapEndpoint() noexcept
{
    if (g_endpoint.load(std::memory_order_acquire))
        return true;
    if (g_endpointFaulted.load(std::memory_order_acquire))
        return false;

    wchar_t handleText[2 + sizeof(std::uintptr_t) * 2 + 1]{};
    const auto length = GetEnvironmentVariableW(
        SkyrimTogetherVR::TickBridge::kMappingHandleEnvironment,
        handleText,
        static_cast<DWORD>(_countof(handleText)));
    if (length == 0 || length >= _countof(handleText))
        return false;

    wchar_t* pEnd = nullptr;
    const auto handleValue = _wcstoui64(handleText, &pEnd, 0);
    if (!handleValue || pEnd == handleText || *pEnd != L'\0')
    {
        g_endpointFaulted.store(true, std::memory_order_release);
        return false;
    }

    const auto mapping = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handleValue));
    auto* const pMapped = static_cast<SkyrimTogetherVR::TickBridge::Endpoint*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SkyrimTogetherVR::TickBridge::Endpoint)));
    if (!pMapped)
    {
        g_endpointFaulted.store(true, std::memory_order_release);
        return false;
    }

    auto* expected = static_cast<SkyrimTogetherVR::TickBridge::Endpoint*>(nullptr);
    if (!g_endpoint.compare_exchange_strong(expected, pMapped, std::memory_order_release, std::memory_order_acquire))
        UnmapViewOfFile(pMapped);
    return true;
}

bool ResolveBodyCaptureCallback(
    SkyrimTogetherVR::TickBridge::BodyCaptureCallback& aCallback,
    std::uint64_t& aEpoch) noexcept
{
    using namespace SkyrimTogetherVR::TickBridge;

    aCallback = nullptr;
    aEpoch = 0;
    // Endpoint attachment is performed on the SKSE load thread before HIGGS
    // callbacks are registered. The real-time callback never maps or locks.
    auto* const pEndpoint = g_endpoint.load(std::memory_order_acquire);
    if (!pEndpoint)
        return false;
    if (ReadEndpointState(*pEndpoint) != static_cast<LONG>(EndpointState::Ready))
        return false;
    if (pEndpoint->Magic != kEndpointMagic || pEndpoint->AbiVersion != kEndpointAbiVersion ||
        pEndpoint->StructSize != sizeof(Endpoint) || pEndpoint->PublisherProcessId != GetCurrentProcessId() ||
        pEndpoint->Reserved0 != 0)
    {
        g_endpointFaulted.store(true, std::memory_order_release);
        return false;
    }
    for (const auto reserved : pEndpoint->Reserved)
    {
        if (reserved != 0)
        {
            g_endpointFaulted.store(true, std::memory_order_release);
            return false;
        }
    }

    if (!pEndpoint->BodyCaptureCallbackRva)
        return false;
    if (!pEndpoint->ImageBase ||
        pEndpoint->BodyCaptureCallbackRva > std::numeric_limits<std::uintptr_t>::max() - pEndpoint->ImageBase ||
        pEndpoint->ImageBase != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))
    {
        g_endpointFaulted.store(true, std::memory_order_release);
        return false;
    }

    const auto callbackAddress = pEndpoint->ImageBase + pEndpoint->BodyCaptureCallbackRva;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(callbackAddress), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.AllocationBase != reinterpret_cast<void*>(pEndpoint->ImageBase) ||
        !IsExecutableReadOnlyPage(memory.Protect))
    {
        g_endpointFaulted.store(true, std::memory_order_release);
        return false;
    }

    aCallback = reinterpret_cast<BodyCaptureCallback>(callbackAddress);
    aEpoch = pEndpoint->Epoch;
    return true;
}

void PublishPostHiggsBodyPose() noexcept
{
    SkyrimTogetherVR::TickBridge::BodyCaptureCallback callback = nullptr;
    std::uint64_t epoch = 0;
    if (!ResolveBodyCaptureCallback(callback, epoch))
        return;

    const auto sequence = g_bodyCaptureSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    g_bodyCaptureAttemptCount.fetch_add(1, std::memory_order_relaxed);
    const auto result = callback(epoch, sequence, GetCurrentThreadId());
    g_bodyCaptureLastResult.store(result, std::memory_order_release);
    if (result == static_cast<std::uint32_t>(SkyrimTogetherVR::TickBridge::DispatchResult::Success))
        g_bodyCaptureSuccessCount.fetch_add(1, std::memory_order_relaxed);
}

std::filesystem::path GetHandoffPath()
{
    return SkyrimTogetherVR::Handoff::GetFile("SkyrimTogetherVR.higgs");
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

bool IsHiggsInstalled()
{
    static const bool installed = []()
    {
        auto pluginPath = GetHandoffPath().parent_path().parent_path() / "SKSE" / "Plugins";
        return HasPluginFile(pluginPath, {L"higgs_vr.dll", L"higgs.dll"});
    }();

    return installed;
}

std::uintptr_t ToAddress(const void* apObject) noexcept
{
    return reinterpret_cast<std::uintptr_t>(apObject);
}

std::uint32_t ReadFormId(const void* apForm) noexcept
{
    if (!apForm)
        return 0;

    const auto* pBytes = reinterpret_cast<const std::uint8_t*>(apForm);
    return *reinterpret_cast<const std::uint32_t*>(pBytes + kTesFormFormIdOffset);
}

void PushEvent(HiggsEvent aEvent)
{
    aEvent.Sequence = ++g_eventSequence;

    std::lock_guard lock(g_eventLock);
    g_recentEvents.push_back(aEvent);
    while (g_recentEvents.size() > kMaxRecentEvents)
        g_recentEvents.pop_front();
}

void PushReferenceEvent(const char* apType, bool aIsLeft, TESObjectREFR* apReference)
{
    HiggsEvent event{};
    event.Type = apType;
    event.IsLeft = aIsLeft;
    event.HasHand = true;
    event.ObjectAddress = ToAddress(apReference);
    event.FormId = ReadFormId(apReference);
    PushEvent(event);
}

void PushFormEvent(const char* apType, bool aIsLeft, TESForm* apForm)
{
    HiggsEvent event{};
    event.Type = apType;
    event.IsLeft = aIsLeft;
    event.HasHand = true;
    event.ObjectAddress = ToAddress(apForm);
    event.FormId = ReadFormId(apForm);
    PushEvent(event);
}

void OnPulled(bool aIsLeft, TESObjectREFR* apReference)
{
    PushReferenceEvent("pulled", aIsLeft, apReference);
}

void OnGrabbed(bool aIsLeft, TESObjectREFR* apReference)
{
    PushReferenceEvent("grabbed", aIsLeft, apReference);
}

void OnDropped(bool aIsLeft, TESObjectREFR* apReference)
{
    PushReferenceEvent("dropped", aIsLeft, apReference);
}

void OnStashed(bool aIsLeft, TESForm* apForm)
{
    PushFormEvent("stashed", aIsLeft, apForm);
}

void OnConsumed(bool aIsLeft, TESForm* apForm)
{
    PushFormEvent("consumed", aIsLeft, apForm);
}

void OnCollision(bool aIsLeft, float aMass, float aSeparatingVelocity)
{
    HiggsEvent event{};
    event.Type = "collision";
    event.IsLeft = aIsLeft;
    event.HasHand = true;
    event.Mass = aMass;
    event.SeparatingVelocity = aSeparatingVelocity;
    PushEvent(event);
}

void OnStartTwoHanding()
{
    HiggsEvent event{};
    event.Type = "startTwoHanding";
    PushEvent(event);
}

void OnStopTwoHanding()
{
    HiggsEvent event{};
    event.Type = "stopTwoHanding";
    PushEvent(event);
}

HiggsHandSnapshot CaptureHandSnapshot(HiggsPluginAPI::IHiggsInterface001* apHiggs, bool aIsLeft)
{
    HiggsHandSnapshot hand{};
    if (!apHiggs)
        return hand;

    auto* const pGrabbed = apHiggs->GetGrabbedObject(aIsLeft);
    const bool holding = apHiggs->IsHoldingObject(aIsLeft);

    hand.Valid = true;
    hand.HoldingObject = holding;
    hand.CanGrabObject = apHiggs->CanGrabObject(aIsLeft);
    hand.HandInGrabbableState = apHiggs->IsHandInGrabbableState(aIsLeft);
    hand.Disabled = apHiggs->IsDisabled(aIsLeft);
    hand.WeaponCollisionDisabled = apHiggs->IsWeaponCollisionDisabled(aIsLeft);
    hand.GrabbedObjectAddress = ToAddress(pGrabbed);
    hand.GrabbedObjectFormId = ReadFormId(pGrabbed);
    apHiggs->GetFingerValues(aIsLeft, hand.Fingers);

    hand.GrabTransformValid = holding;
    if (holding)
        hand.GrabTransform = apHiggs->GetGrabTransform(aIsLeft);

    return hand;
}

void CaptureHiggsSnapshot()
{
    auto* const pHiggs = g_higgs.load(std::memory_order_acquire);
    if (!pHiggs)
        return;

    HiggsSnapshot snapshot{};
    snapshot.Sequence = ++g_snapshotSequence;
    snapshot.BuildNumber = pHiggs->GetBuildNumber();
    snapshot.TwoHanding = pHiggs->IsTwoHanding();
    snapshot.Left = CaptureHandSnapshot(pHiggs, true);
    snapshot.Right = CaptureHandSnapshot(pHiggs, false);

    {
        std::lock_guard lock(g_snapshotLock);
        g_latestSnapshot = snapshot;
    }
    g_snapshotAvailable.store(true, std::memory_order_release);
}

void OnPostVrikPostHiggs()
{
    CaptureHiggsSnapshot();
    PublishPostHiggsBodyPose();
}

void RegisterCallbacks(HiggsPluginAPI::IHiggsInterface001* apHiggs)
{
    if (!apHiggs || g_callbacksRegistered.exchange(true))
        return;

    apHiggs->AddPulledCallback(OnPulled);
    apHiggs->AddGrabbedCallback(OnGrabbed);
    apHiggs->AddDroppedCallback(OnDropped);
    apHiggs->AddStashedCallback(OnStashed);
    apHiggs->AddConsumedCallback(OnConsumed);
    apHiggs->AddCollisionCallback(OnCollision);
    apHiggs->AddStartTwoHandingCallback(OnStartTwoHanding);
    apHiggs->AddStopTwoHandingCallback(OnStopTwoHanding);
    apHiggs->AddPostVrikPostHiggsCallback(OnPostVrikPostHiggs);
    // Live HIGGS state is valid only from its post-update callback.
}

void WritePoint(std::ofstream& aFile, const char* apName, const NiPoint3& acPoint)
{
    aFile << apName << "=" << acPoint.x << "," << acPoint.y << "," << acPoint.z << "\n";
}

void WriteTransform(std::ofstream& aFile, const char* apPrefix, const NiTransform& acTransform)
{
    aFile << apPrefix << ".axisX=" << acTransform.rotate.entries[0][0] << "," << acTransform.rotate.entries[0][1] << "," << acTransform.rotate.entries[0][2] << "\n";
    aFile << apPrefix << ".axisY=" << acTransform.rotate.entries[1][0] << "," << acTransform.rotate.entries[1][1] << "," << acTransform.rotate.entries[1][2] << "\n";
    aFile << apPrefix << ".axisZ=" << acTransform.rotate.entries[2][0] << "," << acTransform.rotate.entries[2][1] << "," << acTransform.rotate.entries[2][2] << "\n";
    WritePoint(aFile, (std::string(apPrefix) + ".translate").c_str(), acTransform.translate);
    aFile << apPrefix << ".scale=" << acTransform.scale << "\n";
}

void WriteFingerValues(std::ofstream& aFile, const char* apPrefix, const HiggsHandSnapshot& acHand)
{
    aFile << apPrefix << ".fingers.valid=1\n";
    aFile << apPrefix << ".fingers.thumb=" << acHand.Fingers[0] << "\n";
    aFile << apPrefix << ".fingers.index=" << acHand.Fingers[1] << "\n";
    aFile << apPrefix << ".fingers.middle=" << acHand.Fingers[2] << "\n";
    aFile << apPrefix << ".fingers.ring=" << acHand.Fingers[3] << "\n";
    aFile << apPrefix << ".fingers.pinky=" << acHand.Fingers[4] << "\n";
}

void WriteHandState(std::ofstream& aFile, const char* apPrefix, const HiggsHandSnapshot* apHand)
{
    if (!apHand || !apHand->Valid)
    {
        aFile << apPrefix << ".valid=0\n";
        return;
    }

    aFile << apPrefix << ".valid=1\n";
    aFile << apPrefix << ".holdingObject=" << (apHand->HoldingObject ? "1" : "0") << "\n";
    aFile << apPrefix << ".canGrabObject=" << (apHand->CanGrabObject ? "1" : "0") << "\n";
    aFile << apPrefix << ".handInGrabbableState=" << (apHand->HandInGrabbableState ? "1" : "0") << "\n";
    aFile << apPrefix << ".disabled=" << (apHand->Disabled ? "1" : "0") << "\n";
    aFile << apPrefix << ".weaponCollisionDisabled=" << (apHand->WeaponCollisionDisabled ? "1" : "0") << "\n";
    aFile << apPrefix << ".grabbedObjectAddress=" << apHand->GrabbedObjectAddress << "\n";
    aFile << apPrefix << ".grabbedObjectFormId=" << apHand->GrabbedObjectFormId << "\n";
    WriteFingerValues(aFile, apPrefix, *apHand);

    aFile << apPrefix << ".grabTransform.valid=" << (apHand->GrabTransformValid ? "1" : "0") << "\n";
    if (apHand->GrabTransformValid)
        WriteTransform(aFile, (std::string(apPrefix) + ".grabTransform").c_str(), apHand->GrabTransform);
}

std::deque<HiggsEvent> CopyRecentEvents()
{
    std::lock_guard lock(g_eventLock);
    return g_recentEvents;
}

HiggsSnapshot CopyLatestSnapshot(bool& aAvailable)
{
    aAvailable = g_snapshotAvailable.load(std::memory_order_acquire);
    if (!aAvailable)
        return {};

    std::lock_guard lock(g_snapshotLock);
    return g_latestSnapshot;
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

    auto* const pHiggs = g_higgs.load(std::memory_order_acquire);
    bool snapshotAvailable = false;
    const auto snapshot = CopyLatestSnapshot(snapshotAvailable);

    file << "bridge.loaded=" << (aLoaded ? "1" : "0") << "\n";
    file << "bridge.sequence=" << aSequence << "\n";
    file << "bridge.epoch=" << g_bridgeEpoch.load(std::memory_order_acquire) << "\n";
    file << "higgs.detected=" << (IsHiggsInstalled() || pHiggs ? "1" : "0") << "\n";
    file << "higgs.interfaceAvailable=" << (pHiggs ? "1" : "0") << "\n";
    file << "higgs.callbacksRegistered=" << (g_callbacksRegistered.load(std::memory_order_acquire) ? "1" : "0") << "\n";
    file << "higgs.eventSequence=" << g_eventSequence.load(std::memory_order_acquire) << "\n";
    file << "higgs.snapshotAvailable=" << (snapshotAvailable ? "1" : "0") << "\n";
    file << "higgs.snapshotSequence=" << snapshot.Sequence << "\n";
    file << "bodyCapture.endpointFaulted=" << (g_endpointFaulted.load(std::memory_order_acquire) ? "1" : "0") << "\n";
    file << "bodyCapture.attemptCount=" << g_bodyCaptureAttemptCount.load(std::memory_order_acquire) << "\n";
    file << "bodyCapture.successCount=" << g_bodyCaptureSuccessCount.load(std::memory_order_acquire) << "\n";
    file << "bodyCapture.lastResult=" << g_bodyCaptureLastResult.load(std::memory_order_acquire) << "\n";

    if (aLoaded && pHiggs && snapshotAvailable)
    {
        file << "higgs.buildNumber=" << snapshot.BuildNumber << "\n";
        file << "higgs.twoHanding=" << (snapshot.TwoHanding ? "1" : "0") << "\n";
        WriteHandState(file, "left", &snapshot.Left);
        WriteHandState(file, "right", &snapshot.Right);
    }
    else
    {
        file << "higgs.buildNumber=0\n";
        file << "higgs.twoHanding=0\n";
        WriteHandState(file, "left", nullptr);
        WriteHandState(file, "right", nullptr);
    }

    const auto events = CopyRecentEvents();
    file << "recentEventCount=" << events.size() << "\n";
    std::size_t index = 0;
    for (const auto& event : events)
    {
        const auto prefix = std::string("recentEvent.") + std::to_string(index);
        file << prefix << ".sequence=" << event.Sequence << "\n";
        file << prefix << ".type=" << event.Type << "\n";
        file << prefix << ".hasHand=" << (event.HasHand ? "1" : "0") << "\n";
        file << prefix << ".hand=" << (event.IsLeft ? "left" : "right") << "\n";
        file << prefix << ".objectAddress=" << event.ObjectAddress << "\n";
        file << prefix << ".formId=" << event.FormId << "\n";
        file << prefix << ".mass=" << event.Mass << "\n";
        file << prefix << ".separatingVelocity=" << event.SeparatingVelocity << "\n";
        ++index;
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

bool RequestHiggsInterface()
{
    if (!g_messaging || g_pluginHandle == kPluginHandleInvalid)
        return false;

    HiggsMessage message{};
    if (!g_messaging->Dispatch(g_pluginHandle, kHiggsMessageGetInterface, &message, sizeof(HiggsMessage*), "HIGGS"))
        return false;

    if (!message.GetApiFunction)
        return false;

    auto* const pHiggs = static_cast<HiggsPluginAPI::IHiggsInterface001*>(message.GetApiFunction(1));
    g_higgs.store(pHiggs, std::memory_order_release);
    RegisterCallbacks(pHiggs);
    return pHiggs != nullptr;
}

void OnSkseMessage(SKSEMessagingInterface::Message* apMessage)
{
    if (!apMessage)
        return;

    if (apMessage->type == kSkseMessagePostLoad || apMessage->type == kSkseMessagePostPostLoad)
    {
        MapEndpoint();
        RequestHiggsInterface();
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
    apInfo->name = "SkyrimTogetherVRHiggsBridge";
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

    MapEndpoint();
    g_messaging->RegisterListener(g_pluginHandle, "SKSE", reinterpret_cast<void*>(OnSkseMessage));
    StartWriter();
    return true;
}
