#pragma once

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct GRefCountImplCore
{
    virtual ~GRefCountImplCore() = default;

    static void CheckInvalidDelete(GRefCountImplCore*) {}

    [[nodiscard]] constexpr uint32_t GetRefCount() const noexcept { return m_refCount; }

protected:
    uint32_t m_refCount{1};
    std::uint32_t _pad0C{0};
};

struct GRefCountImpl : GRefCountImplCore
{
    virtual ~GRefCountImpl() = default; // 00
};
static_assert(sizeof(GRefCountImpl) == 0x10);

template <class Base, std::uint32_t StatType> struct GRefCountBaseStatImpl : Base
{
    // GFC_MEMORY_REDEFINE_NEW_IMPL(Base, GFC_REFCOUNTALLOC_CHECK_DELETE, StatType);
};

template <class T, std::uint32_t STAT> struct GRefCountBase : GRefCountBaseStatImpl<GRefCountImpl, STAT>
{
    enum
    {
        kStatType = STAT
    };
};

struct GStatGroups
{
    // _Mem for GMemoryStat.
    // _Tks for GTimerStat.
    // _Cnt for GCounterStat.
    enum GStatGroup : uint32_t
    {
        kGStatGroup_Default = 0,

        kGStatGroup_Core = 16,
        kGStatGroup_Renderer = 1 << 6,
        kGStatGroup_RenderGen = 2 << 6,

        kGStatGroup_GFxFontCache = 3 << 6,
        kGStatGroup_GFxMovieData = 4 << 6,
        kGStatGroup_GFxMovieView = 5 << 6,
        kGStatGroup_GFxRenderCache = 6 << 6,
        kGStatGroup_GFxPlayer = 7 << 6,
        kGStatGroup_GFxIME = 8 << 6,

        // General memory
        kGStat_Mem = kGStatGroup_Default + 1,
        kGStat_Default_Mem,
        kGStat_Image_Mem,
        kGStat_Sound_Mem,
        kGStat_String_Mem,
        kGStat_Video_Mem,

        // Memory allocated for debugging purposes.
        kGStat_Debug_Mem,
        kGStat_DebugHUD_Mem,
        kGStat_DebugTracker_Mem,
        kGStat_StatBag_Mem,

        // Core entries
        kGStatHeap_Start = kGStatGroup_Core,
        // 16 slots for HeapSummary

        // How many entries we support by default
        kGStat_MaxId = 64 << 6, // 64 * 64 = 4096
        kGStat_EntryCount = 512
    };
};

struct FxDelegateHandler : GRefCountBase<FxDelegateHandler, GStatGroups::kGStat_Default_Mem>
{
    using CallbackFn = void(const void*);

    class CallbackProcessor
    {
    public:
        virtual ~CallbackProcessor() = default;
        virtual void Process(const void* apMethodName, CallbackFn* apMethod) = 0;
    };
    static_assert(sizeof(CallbackProcessor) == 0x8);

    ~FxDelegateHandler() override = default; // 00

    // add
    virtual void Accept(CallbackProcessor* a_cbReg) = 0; // 01
};

enum class UI_MESSAGE_RESULTS
{
    kHandled = 0,
    kIgnore = 1,
    kPassOn = 2
};

struct UIMessage;

struct IMenu : FxDelegateHandler
{
    using CommonLibIMenuOffsets = Skyrim::RuntimeLayout::IMenuCommonLibNgOffsets;
    using LocalIMenuOffsets = Skyrim::RuntimeLayout::IMenuLocalShimOffsets;

    virtual ~IMenu() = default;

    void Accept(CallbackProcessor* apProcessor) override;

    // add
    virtual void PostCreate() = 0;
    virtual void Unk_03(void) = 0;
    virtual UI_MESSAGE_RESULTS ProcessMessage(UIMessage& aMessage) = 0;
    virtual void AdvanceMovie(float aInterval, uint32_t aCurrentTime) = 0;
    virtual void PostDisplay() = 0;
    virtual void PreDisplay() = 0;
    virtual void RefreshPlatform() = 0;

    enum UI_MENU_FLAGS : uint32_t
    {
        // Multiples of two
        kNone = 0x0,
        kPausesGame = 0x1,
        kAlwaysOpen = 0x2,
        kUsesCursor = 0x4,
        kUsesMenuContext = 0x8,
        kModal = 0x10,
        kFreezeFrameBackground = 0x20,
        kOnStack = 0x40,
        kDisablePauseMenu = 0x80,
        kRequiresUpdate = 0x100,
        kTopmostRenderedMenu = 0x200,
        kUpdateUsesCursor = 0x400,
        kAllowSaving = 0x800,
        kRendersOffscreenTargets = 0x1000,
        kInventoryItemMenu = 0x2000,
        kDontHideCursorWhenTopmost = 0x4000,
        kCustomRendering = 0x8000,
        kAssignCursorToRenderer = 0x10000,
        kApplicationMenu = 0x20000,
        kHasButtonBar = 0x40000,
        kIsTopButtonBar = 0x80000,
        kAdvancesUnderPauseMenu = 0x100000,
        kRendersUnderPauseMenu = 0x200000,
        kUsesBlurredBackground = 0x400000,
        kCompanionAppAllowed = 0x800000,
        kFreezeFramePause = 0x1000000,
        kSkipRenderDuringFreezeFrameScreenshot = 0x2000000,
        kLargeScaleformRenderCacheMode = 0x4000000,
        kUsesMovementToDirection = 0x8000000,
    };

    void SetFlag(uint32_t auiFlag);
    void ClearFlag(uint32_t auiFlag);

    [[nodiscard]] uint32_t GetMenuFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibIMenuOffsets::MenuFlags);
#else
        return uiMenuFlags;
#endif
    }

    void SetMenuFlagsData(uint32_t aFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibIMenuOffsets::MenuFlags, aFlags);
#else
        uiMenuFlags = aFlags;
#endif
    }

    bool PausesGame() const { return GetMenuFlagsData() & kPausesGame; }

    bool FreezesBackground() const { return GetMenuFlagsData() & kFreezeFrameBackground; }

    bool FreezesFramePause() const { return GetMenuFlagsData() & kFreezeFramePause; }

    void* uiMovie{nullptr};
    int8_t depthPriority{3};
    uint8_t pad19{0};
    uint16_t pad20{0};
    uint32_t uiMenuFlags;
    uint32_t eInputContext;
    std::uint32_t pad24{0};
    void* fxDelegate{nullptr};

#if TP_SKYRIM_VR
    int32_t unk30{-1};
    int32_t unk34{1};
    uint64_t unk38{0};
#endif
};

static_assert(offsetof(IMenu, IMenu::uiMenuFlags) == IMenu::LocalIMenuOffsets::MenuFlags);
static_assert(offsetof(IMenu, IMenu::eInputContext) == IMenu::LocalIMenuOffsets::InputContext);
static_assert(offsetof(IMenu, IMenu::fxDelegate) == IMenu::LocalIMenuOffsets::FxDelegate);
#if TP_SKYRIM_VR
static_assert(offsetof(IMenu, IMenu::unk30) == IMenu::LocalIMenuOffsets::VrUnk30);
static_assert(offsetof(IMenu, IMenu::unk34) == IMenu::LocalIMenuOffsets::VrUnk34);
static_assert(offsetof(IMenu, IMenu::unk38) == IMenu::LocalIMenuOffsets::VrUnk38);
static_assert(sizeof(IMenu) == IMenu::LocalIMenuOffsets::VrSize);
#else
static_assert(sizeof(IMenu) == IMenu::LocalIMenuOffsets::SeSize);
#endif
