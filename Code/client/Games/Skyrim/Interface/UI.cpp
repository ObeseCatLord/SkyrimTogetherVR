#include <Games/Skyrim/Interface/IMenu.h>
#include <Games/Skyrim/Interface/MenuPausePolicy.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Games/Skyrim/VR/VRHookPolicy.h>
#include <Games/Skyrim/VR/VRMemorySafety.h>
#include <Misc/BSFixedString.h>
#include <TiltedOnlinePCH.h>
#include <VRTickBridge.h>
#include "immersive_launcher/stubs/DllBlocklist.h"

#include <World.h>

#include <atomic>
#include <limits>
#include <mutex>

static bool g_RequestUnpauseAll{false};

namespace
{
static_assert(IMenu::kPausesGame == SkyrimTogetherVR::MenuPausePolicy::kPausesGame);
static_assert(IMenu::kFreezeFrameBackground == SkyrimTogetherVR::MenuPausePolicy::kFreezeFrameBackground);
static_assert(IMenu::kFreezeFramePause == SkyrimTogetherVR::MenuPausePolicy::kFreezeFramePause);

#if TP_SKYRIM_VR
using MenuCreator = UI::TCreate*;
static_assert(std::is_same_v<MenuCreator, IMenu* (*)()>);

static std::array<std::atomic<MenuCreator>, SkyrimTogetherVR::MenuPausePolicy::kAllowList.size()> s_originalMenuCreators{};

struct ManagedMenuState
{
    IMenu* Instance{};
    std::uint32_t OriginalManagedFlags{};
    bool Modified{};
    bool DisconnectedBypassLogged{};
};

static std::array<ManagedMenuState, SkyrimTogetherVR::MenuPausePolicy::kAllowList.size()> s_managedMenus{};
// Creator wrappers and periodic scans can arrive on different threads. Keep
// each menu's instance, original flags, and modification state coherent.
static std::mutex s_managedMenusMutex{};
static std::atomic_bool s_soulsReBypassLogged{false};
static std::atomic_bool s_unsafeStateBypassLogged{false};
static std::atomic_bool s_disconnectedBypassLogged{false};

enum class VrMenuPausePolicyRuntimeState : std::uint8_t
{
    Unsafe,
    SoulsRe,
    Disconnected,
    Connected,
};

static std::atomic<VrMenuPausePolicyRuntimeState> s_runtimeState{VrMenuPausePolicyRuntimeState::Unsafe};

void LogVrMenuPausePolicyBypassOnce(std::atomic_bool& arLogged, const char* apReason)
{
    if (!arLogged.exchange(true, std::memory_order_relaxed))
        spdlog::info("VR menu pause policy bypassed: {}", apReason);
}

[[nodiscard]] bool IsVrUiOwnerThread() noexcept
{
    const auto ownerThreadId = SkyrimTogetherVR::TickBridge::GetActivationThreadId();
    return ownerThreadId != 0 && ownerThreadId == GetCurrentThreadId();
}

void RefreshVrMenuPausePolicyRuntimeState()
{
    if (!World::Exists())
    {
        s_runtimeState.store(VrMenuPausePolicyRuntimeState::Unsafe, std::memory_order_release);
        return;
    }

    if (stubs::g_IsSoulsREActive)
    {
        s_runtimeState.store(VrMenuPausePolicyRuntimeState::SoulsRe, std::memory_order_release);
        return;
    }

    const auto runtimeState = World::Get().GetTransport().IsConnected() ? VrMenuPausePolicyRuntimeState::Connected : VrMenuPausePolicyRuntimeState::Disconnected;
    if (runtimeState == VrMenuPausePolicyRuntimeState::Connected)
        s_disconnectedBypassLogged.store(false, std::memory_order_relaxed);
    s_runtimeState.store(runtimeState, std::memory_order_release);
}

[[nodiscard]] bool HasVrMemoryProtection(const void* apAddress, const std::size_t aSize, const bool aWritable, const bool aExecutable) noexcept
{
    if (!apAddress || aSize == 0)
        return false;

    const auto start = reinterpret_cast<std::uintptr_t>(apAddress);
    if (aSize - 1 > std::numeric_limits<std::uintptr_t>::max() - start)
        return false;

    const auto last = start + aSize - 1;
    auto current = start;
    while (current <= last)
    {
        MEMORY_BASIC_INFORMATION page{};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &page, sizeof(page)) != sizeof(page) || page.State != MEM_COMMIT ||
            (page.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        const auto protection = page.Protect & 0xFFu;
        const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                              protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool executable = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (!readable || (aWritable && !writable) || (aExecutable && !executable))
            return false;

        const auto pageBase = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        if (page.RegionSize > std::numeric_limits<std::uintptr_t>::max() - pageBase)
            return false;
        const auto pageEnd = pageBase + page.RegionSize;
        if (current < pageBase || current >= pageEnd)
            return false;
        if (last < pageEnd)
            return true;
        current = pageEnd;
    }
    return true;
}

[[nodiscard]] bool IsExactReadableMenuName(const char* apName, const std::string_view aExpected) noexcept
{
    return apName && SkyrimTogetherVR::IsReadableVrMemory(apName, aExpected.size() + 1) && std::char_traits<char>::compare(apName, aExpected.data(), aExpected.size()) == 0 &&
           apName[aExpected.size()] == '\0';
}

void ApplyVrMenuPausePolicy(
    const std::size_t aIndex, IMenu* apMenu, const SkyrimTogetherVR::MenuPausePolicy::MutationContext aContext)
{
    const bool isVrUiOwnerThread = IsVrUiOwnerThread();
    if ((aContext == SkyrimTogetherVR::MenuPausePolicy::MutationContext::PeriodicScan && !isVrUiOwnerThread) || !apMenu ||
        !SkyrimTogetherVR::IsReadableVrMemory(apMenu, IMenu::CommonLibIMenuOffsets::MenuFlags + sizeof(std::uint32_t)))
    {
        LogVrMenuPausePolicyBypassOnce(s_unsafeStateBypassLogged, "unsafe runtime state");
        return;
    }

    const auto flags = apMenu->GetMenuFlagsData();
    if (!SkyrimTogetherVR::MenuPausePolicy::CanMutateFlags(aContext, isVrUiOwnerThread, (flags & IMenu::kOnStack) != 0))
        return;

    const auto runtimeState = s_runtimeState.load(std::memory_order_acquire);
    if (runtimeState == VrMenuPausePolicyRuntimeState::Unsafe)
    {
        LogVrMenuPausePolicyBypassOnce(s_unsafeStateBypassLogged, "unsafe runtime state");
        return;
    }

    if (runtimeState == VrMenuPausePolicyRuntimeState::SoulsRe)
    {
        LogVrMenuPausePolicyBypassOnce(s_soulsReBypassLogged, "SkyrimSoulsRE active");
        return;
    }

    const bool transportConnected = runtimeState == VrMenuPausePolicyRuntimeState::Connected;
    std::scoped_lock lock(s_managedMenusMutex);
    auto& state = s_managedMenus[aIndex];
    if (state.Instance != apMenu)
        state = {apMenu, flags & SkyrimTogetherVR::MenuPausePolicy::kClearedFlags, false};

    auto newFlags = flags;
    const auto action = SkyrimTogetherVR::MenuPausePolicy::DecideAction(
        SkyrimTogetherVR::MenuPausePolicy::kAllowList[aIndex], transportConnected, false, true, (flags & IMenu::kOnStack) != 0, state.Modified);
    if (action == SkyrimTogetherVR::MenuPausePolicy::Action::Unpause)
    {
        if (!state.Modified)
            state.OriginalManagedFlags = flags & SkyrimTogetherVR::MenuPausePolicy::kClearedFlags;
        newFlags = SkyrimTogetherVR::MenuPausePolicy::UnpausedFlags(flags);
    }
    else if (action == SkyrimTogetherVR::MenuPausePolicy::Action::Restore)
    {
        newFlags = SkyrimTogetherVR::MenuPausePolicy::RestoredFlags(flags, state.OriginalManagedFlags);
    }
    else
    {
        // A live menu must remain untouched until close; otherwise Skyrim's
        // numPausesGame queue bookkeeping would be desynchronized.
        if (!transportConnected && !state.Modified && !state.DisconnectedBypassLogged &&
            !s_disconnectedBypassLogged.exchange(true, std::memory_order_relaxed))
        {
            spdlog::info("VR menu pause policy bypassed: transport disconnected {}", SkyrimTogetherVR::MenuPausePolicy::kAllowList[aIndex]);
            state.DisconnectedBypassLogged = true;
        }
        return;
    }

    if (newFlags == flags)
    {
        if (!transportConnected)
            state.Modified = false;
        return;
    }

    apMenu->SetMenuFlagsData(newFlags);
    state.Modified = transportConnected;
    spdlog::info("VR menu pause policy {} {}", transportConnected ? "unpaused" : "restored", SkyrimTogetherVR::MenuPausePolicy::kAllowList[aIndex]);
}

template <std::size_t Index> IMenu* CreateVrParityMenu()
{
    const auto original = s_originalMenuCreators[Index].load(std::memory_order_acquire);
    if (!original)
        return nullptr;

    auto* menu = original();
    ApplyVrMenuPausePolicy(Index, menu, SkyrimTogetherVR::MenuPausePolicy::MutationContext::Creator);
    return menu;
}

static constexpr std::array<MenuCreator, SkyrimTogetherVR::MenuPausePolicy::kAllowList.size()> kVrParityCreators = {
    &CreateVrParityMenu<0>, &CreateVrParityMenu<1>, &CreateVrParityMenu<2>, &CreateVrParityMenu<3>, &CreateVrParityMenu<4>,
    &CreateVrParityMenu<5>, &CreateVrParityMenu<6>, &CreateVrParityMenu<7>, &CreateVrParityMenu<8>,
};

void TryInstallVrMenuPausePolicy(UI* apUI)
{
    if (!IsVrUiOwnerThread())
    {
        LogVrMenuPausePolicyBypassOnce(s_unsafeStateBypassLogged, "unsafe runtime state");
        return;
    }

    RefreshVrMenuPausePolicyRuntimeState();
    const auto runtimeState = s_runtimeState.load(std::memory_order_acquire);
    if (runtimeState == VrMenuPausePolicyRuntimeState::SoulsRe)
    {
        LogVrMenuPausePolicyBypassOnce(s_soulsReBypassLogged, "SkyrimSoulsRE active");
        return;
    }

    if (runtimeState == VrMenuPausePolicyRuntimeState::Unsafe || !apUI)
    {
        LogVrMenuPausePolicyBypassOnce(s_unsafeStateBypassLogged, "unsafe runtime state");
        return;
    }

    using MenuTable = creation::BSTHashMap<BSFixedString, UI::UIMenuEntry>;
    using MenuTableEntry = typename MenuTable::entry_type;
    static_assert(offsetof(MenuTable, m_entries) == 0x28);

    if (!SkyrimTogetherVR::IsReadableVrMemory(apUI, UI::CommonLibUIOffsets::MenuMap + sizeof(MenuTable)))
    {
        LogVrMenuPausePolicyBypassOnce(s_unsafeStateBypassLogged, "unsafe runtime state");
        return;
    }

    auto& menuTable = apUI->GetMenuMapData();
    if (menuTable.m_size == 0 || menuTable.m_size > 4096 || menuTable.m_freeCount > menuTable.m_size || !menuTable.m_entries ||
        !SkyrimTogetherVR::IsReadableVrMemory(menuTable.m_entries, sizeof(MenuTableEntry) * menuTable.m_size))
    {
        LogVrMenuPausePolicyBypassOnce(s_unsafeStateBypassLogged, "unsafe runtime state");
        return;
    }

    for (auto& entry : menuTable)
    {
        if (!entry.key.data || !entry.value.create || !HasVrMemoryProtection(&entry.value.create, sizeof(entry.value.create), true, false))
            continue;

        for (std::size_t index = 0; index < SkyrimTogetherVR::MenuPausePolicy::kAllowList.size(); ++index)
        {
            const auto menuName = SkyrimTogetherVR::MenuPausePolicy::kAllowList[index];
            if (s_originalMenuCreators[index].load(std::memory_order_relaxed) || !IsExactReadableMenuName(entry.key.data, menuName) ||
                !HasVrMemoryProtection(reinterpret_cast<const void*>(entry.value.create), 1, false, true))
                continue;

            // Publish the original before exposing its wrapper to creator
            // threads through Skyrim's menu table.
            s_originalMenuCreators[index].store(entry.value.create, std::memory_order_release);
            entry.value.create = kVrParityCreators[index];
            spdlog::info("VR menu pause policy registered {}", SkyrimTogetherVR::MenuPausePolicy::kAllowList[index]);
        }

        for (std::size_t index = 0; index < SkyrimTogetherVR::MenuPausePolicy::kAllowList.size(); ++index)
        {
            if (IsExactReadableMenuName(entry.key.data, SkyrimTogetherVR::MenuPausePolicy::kAllowList[index]))
            {
                ApplyVrMenuPausePolicy(index, entry.value.spMenu, SkyrimTogetherVR::MenuPausePolicy::MutationContext::PeriodicScan);
                break;
            }
        }
    }
}
#endif
} // namespace

#if TP_SKYRIM_VR
void SkyrimTogetherVR::MenuPausePolicy::PublishTransportConnectionState(const bool aConnected) noexcept
{
    if (stubs::g_IsSoulsREActive)
    {
        s_runtimeState.store(VrMenuPausePolicyRuntimeState::SoulsRe, std::memory_order_release);
        return;
    }

    const auto runtimeState = aConnected ? VrMenuPausePolicyRuntimeState::Connected : VrMenuPausePolicyRuntimeState::Disconnected;
    if (aConnected)
        s_disconnectedBypassLogged.store(false, std::memory_order_relaxed);
    s_runtimeState.store(runtimeState, std::memory_order_release);
}
#endif

#if TP_SKYRIM_VR
[[maybe_unused]] static constexpr auto kUIActiveMenuQueueSwapCallAddend = 0x67B;
#else
static constexpr auto kUIActiveMenuQueueSwapCallAddend = 0x682;
#endif

UI* UI::Get()
{
#if TP_SKYRIM_VR
    POINTER_SKYRIMSE(UI*, s_instance, 514178);
#else
    POINTER_SKYRIMSE(UI*, s_instance, 400327);
#endif

    auto* ui = *s_instance.Get();
#if TP_SKYRIM_VR
    TryInstallVrMenuPausePolicy(ui);
#endif
    return ui;
}

SkyrimTogetherVR::MenuOpenState UI::GetMenuOpen(const BSFixedString& acName) const noexcept
{
    if (acName.data == nullptr)
        return SkyrimTogetherVR::MenuOpenState::Unavailable;

#if TP_SKYRIM_VR
    using MenuTable = creation::BSTHashMap<BSFixedString, UIMenuEntry>;
    using MenuTableEntry = typename MenuTable::entry_type;
    static_assert(CommonLibUIOffsets::MenuMap == 0x128);
    static_assert(IMenu::CommonLibIMenuOffsets::MenuFlags == 0x1C);
    static_assert(IMenu::CommonLibIMenuOffsets::InputContext == 0x20);
    static_assert(IMenu::kOnStack == 0x40);
    static_assert(sizeof(UIMenuEntry) == 0x10);
    static_assert(sizeof(MenuTableEntry) == 0x20);
    static_assert(sizeof(MenuTable) == 0x30);
    static_assert(offsetof(MenuTable, m_size) == 0xC);
    static_assert(offsetof(MenuTable, m_freeCount) == 0x10);
    static_assert(offsetof(MenuTable, m_freeOffset) == 0x14);
    static_assert(offsetof(MenuTable, m_eolPtr) == 0x18);
    static_assert(offsetof(MenuTable, m_entries) == 0x28);
    static_assert(offsetof(MenuTableEntry, key) == 0x0);
    static_assert(offsetof(MenuTableEntry, value) == 0x8);
    static_assert(offsetof(MenuTableEntry, next) == 0x18);

    constexpr auto kReadableMenuSize = IMenu::CommonLibIMenuOffsets::MenuFlags + sizeof(uint32_t);
    if (!SkyrimTogetherVR::IsReadableVrMemory(this, CommonLibUIOffsets::MenuMap + sizeof(MenuTable)))
        return SkyrimTogetherVR::MenuOpenState::Unavailable;

    return SkyrimTogetherVR::ProbeVrMenuOpen(
        GetMenuMapData(), acName.data, kReadableMenuSize, IMenu::kOnStack, [](const void* apAddress, std::size_t aSize)
        { return SkyrimTogetherVR::IsReadableVrMemory(apAddress, aSize); }, [](const IMenu* apMenu) { return apMenu->GetMenuFlagsData(); });
#else
    TP_THIS_FUNCTION(TMenuSystem_IsOpen, bool, const UI, const BSFixedString&);
    POINTER_SKYRIMSE(TMenuSystem_IsOpen, s_isMenuOpen, 82074);

    return TiltedPhoques::ThisCall(s_isMenuOpen.Get(), this, acName) ? SkyrimTogetherVR::MenuOpenState::Open : SkyrimTogetherVR::MenuOpenState::Closed;
#endif
}

void UI::CloseAllMenus()
{
    TP_THIS_FUNCTION(TUI_CloseAll, void, const UI);
    POINTER_SKYRIMSE(TUI_CloseAll, s_CloseAll, 82088);

    TiltedPhoques::ThisCall(s_CloseAll.Get(), this);
}

BSFixedString* UI::LookupMenuNameByInstance(IMenu* apMenu)
{
    for (auto& it : GetMenuMapData())
    {
        if (it.value.spMenu == apMenu)
            return &it.key;
    }
    return nullptr;
}

IMenu* UI::FindMenuByName(const BSFixedString& acName)
{
    for (const auto& it : GetMenuMapData())
    {
        if (it.key == acName)
            return it.value.spMenu;
    }
    return nullptr;
}

void UI::DebugLogAllMenus()
{
    for (auto& e : GetMenuStackData())
    {
        spdlog::info("Menu {}", e->GetMenuFlagsData());
    }
}

#if !TP_SKYRIM_VR && TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE, TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED)
static void UnfreezeMenu(IMenu* apMenu)
{
    const auto flags = apMenu->GetMenuFlagsData();
    apMenu->SetMenuFlagsData(SkyrimTogetherVR::MenuPausePolicy::UnpausedFlags(flags));
}

static void* (*UI_AddToActiveQueue)(UI*, IMenu*, void*);

static void* UI_AddToActiveQueue_Hook(UI* apSelf, IMenu* apMenu, void* apFoundItem /*In reality a reference*/)
{
    // if the menu is empty we let the real function handle it.
    if (!apMenu || !World::Get().GetTransport().IsConnected() || stubs::g_IsSoulsREActive)
        return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);

#if 0
        if (auto* pName = apSelf->LookupMenuNameByInstance(apEntry))
        {
            spdlog::info("Menu requested {}", pName->AsAscii());
        }
#endif

    // NOTE(Force): could also compare by RTTI later on...
    for (const auto item : SkyrimTogetherVR::MenuPausePolicy::kAllowList)
    {
        if (const BSFixedString menuName(item.data()); auto* pMenu = apSelf->FindMenuByName(menuName))
        {
            if (pMenu == apMenu)
                UnfreezeMenu(apMenu);
        }
    }

    return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
}
#endif

using TCallback = void(void*, const BSFixedString*, uint32_t, void*);
static TCallback* UIMessageQueue__AddMessage_Real;

// Useful for debugging UI related issues.
void UIMessageQueue__AddMessage(void* a1, const BSFixedString* a2, UIMessage::UI_MESSAGE_TYPE a3, void* a4)
{
    spdlog::info(
        "Adding Message {} with prio {} from 0x{:X}", a2->AsAscii(), static_cast<std::underlying_type_t<UIMessage::UI_MESSAGE_TYPE>>(a3),
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    UIMessageQueue__AddMessage_Real(a1, a2, a3, a4);
}

static TiltedPhoques::Initializer s_s(
    []()
    {
#if !TP_SKYRIM_VR && TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE, TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED)
        // pray that this doesnt fail!
        VersionDbPtr<uint8_t> ProcessHook(82082);
        TiltedPhoques::SwapCall(ProcessHook.Get() + kUIActiveMenuQueueSwapCallAddend, UI_AddToActiveQueue, &UI_AddToActiveQueue_Hook);
#endif

#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE, TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE_VR_RESOLVED)
        // Ignore startup movie
        // TODO: Move me later.
        VersionDbPtr<uint8_t> MainInit(36548);
        TiltedPhoques::Put<uint8_t>(MainInit.Get() + 0xFE, 0xEB);
#endif

#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS, TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS_VR_RESOLVED)
        // Credits to Skyrim Souls RE for this fix.
        // Allows the favorites menu to be numbered during connect.
        VersionDbPtr<uint8_t> FavoritesCanProcess(51538);
        TiltedPhoques::Put<uint16_t>(FavoritesCanProcess.Get() + 0x15, 0x9090);
#endif

#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE, TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED)
    // Some experiments:
    // POINTER_SKYRIMSE(TCallback, s_start, 13631);
    // UIMessageQueue__AddMessage_Real = s_start.Get();
    // TP_HOOK(&UIMessageQueue__AddMessage_Real, UIMessageQueue__AddMessage);

    // This kills the loading spinner
    // TiltedPhoques::Put<uint8_t>(0x1405D51C1, 0xEB);
    // TiltedPhoques::Nop(0x1405D51A2, 5);

    // use 8 threads by default!
    // TiltedPhoques::Put<uint8_t>(0x141E45770, 8);
#endif
    });
