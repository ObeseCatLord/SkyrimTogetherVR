#include <Games/Skyrim/Interface/IMenu.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Games/Skyrim/VR/VRHookPolicy.h>
#include <Games/Skyrim/VR/VRMemorySafety.h>
#include <Misc/BSFixedString.h>
#include <TiltedOnlinePCH.h>
#include "immersive_launcher/stubs/DllBlocklist.h"

#include <World.h>

static bool g_RequestUnpauseAll{false};

#if TP_SKYRIM_VR
static constexpr auto kUIActiveMenuQueueSwapCallAddend = 0x67B;
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

    return *s_instance.Get();
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

static void UnfreezeMenu(IMenu* apEntry)
{
    if (apEntry->PausesGame())
        apEntry->ClearFlag(IMenu::kPausesGame);

    if (apEntry->FreezesBackground())
        apEntry->ClearFlag(IMenu::kFreezeFrameBackground);

    if (apEntry->FreezesFramePause())
        apEntry->ClearFlag(IMenu::kFreezeFramePause);
}

static constexpr const char* kAllowList[] = {
    "TweenMenu",     "MagicMenu",     "StatsMenu",     "InventoryMenu", "MessageBoxMenu",
    "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console"
    //"MapMenu", // MapMenu is disabled till we find a proper fix for first person.
    //"Journal Menu", // Journal menu, aka pause menu, is disabled until we find a fix for manual save crashing while unpaused.
};

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
    for (const char* item : kAllowList)
    {
        if (auto* pMenu = apSelf->FindMenuByName(item))
        {
            if (pMenu == apMenu)
                UnfreezeMenu(apMenu);
        }
    }

    return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
}

using TCallback = void(void*, const BSFixedString*, uint32_t, void*);
static TCallback* UIMessageQueue__AddMessage_Real;

// Useful for debugging UI related issues.
void UIMessageQueue__AddMessage(void* a1, const BSFixedString* a2, UIMessage::UI_MESSAGE_TYPE a3, void* a4)
{
    spdlog::info("Adding Message {} with prio {} from {}", a2->AsAscii(), a3, fmt::ptr(_ReturnAddress()));
    UIMessageQueue__AddMessage_Real(a1, a2, a3, a4);
}

static TiltedPhoques::Initializer s_s(
    []()
    {
#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE, TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED)
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
