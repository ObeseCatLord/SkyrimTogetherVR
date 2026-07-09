
#include "Services/InputService.h"
#include "Systems/RenderSystemD3D11.h"

#include "World.h"

#include "BSGraphics/BSGraphicsRenderer.h"
#include "BSRandom/BSRandom.h"
#include <Games/Skyrim/VR/VRHookPolicy.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

#ifndef TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
#define TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS 0
#endif

// shared resource by launcher
extern HICON g_SharedWindowIcon;

namespace BSGraphics
{
namespace
{

static RenderSystemD3D11* g_sRs = nullptr;
static WNDPROC RealWndProc = nullptr;
static RendererWindow* g_RenderWindow = nullptr;

static constexpr char kTogetherWindowName[]{"Skyrim Together"};

} // namespace
RendererWindow* GetMainWindow()
{
    return g_RenderWindow;
}

bool RendererWindow::IsForeground()
{
    return GetForegroundWindow() == hWnd;
}

void (*Renderer_Init)(Renderer*, BSGraphics::RendererInitOSData*, const BSGraphics::ApplicationWindowProperties*, BSGraphics::RendererInitReturn*) = nullptr;

// WNDPROC seems to be part of the renderer
LRESULT CALLBACK Hook_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (InputService::WndProc(hwnd, uMsg, wParam, lParam) != 0)
        return 0;

    return RealWndProc(hwnd, uMsg, wParam, lParam);
}

void Hook_Renderer_Init(Renderer* self, BSGraphics::RendererInitOSData* aOSData, const BSGraphics::ApplicationWindowProperties* aFBData, BSGraphics::RendererInitReturn* aOut)
{
#if TP_SKYRIM_VR && !TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS
    spdlog::info("SkyrimTogetherVR renderer init hook reached: self={}, osData={}, fbData={}", fmt::ptr(self), fmt::ptr(aOSData), fmt::ptr(aFBData));
    Renderer_Init(self, aOSData, aFBData, aOut);
    spdlog::info("SkyrimTogetherVR renderer init completed");
    return;
#else
    // we feed this a shared icon as the resource directory of our former launcher data is already overwritten with the
    // game.
    aOSData->hIcon = g_SharedWindowIcon;
    // Append our window name.
    aOSData->pClassName = kTogetherWindowName;

    RealWndProc = aOSData->pWndProc;
    aOSData->pWndProc = Hook_WndProc;

    Renderer_Init(self, aOSData, aFBData, aOut);

    g_sRs = &World::Get().ctx().at<RenderSystemD3D11>();
    // This how the game does it too
    auto& renderer = self->GetRendererData();
    g_RenderWindow = renderer.GetRenderWindowData();

    g_sRs->OnDeviceCreation(
        renderer.GetRenderWindowData()->pSwapChain, renderer.GetForwarderData(), renderer.GetContextData());
#endif
}

void (*StopTimer)(int) = nullptr;

// Insert us at the End
void Hook_StopTimer(int type)
{
    if (g_sRs)
        g_sRs->OnRender();

    StopTimer(type);
}

void InstallVrRenderBringupHooks()
{
    const VersionDbPtr<void> renderInit(77226);

    Renderer_Init = static_cast<decltype(Renderer_Init)>(renderInit.GetPtr());
    spdlog::info("Installing SkyrimTogetherVR renderer bring-up hook: rendererInit={}", fmt::ptr(Renderer_Init));
    if (!Renderer_Init)
    {
        spdlog::error("SkyrimTogetherVR renderer bring-up hook skipped because renderer init address did not resolve");
        return;
    }

    TP_HOOK_IMMEDIATE(&Renderer_Init, &Hook_Renderer_Init);
}

static TiltedPhoques::Initializer s_viewportHooks(
    []()
    {
#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE, TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE_VR_RESOLVED)
        const VersionDbPtr<void> initWindowLoc(77226);
        // patch dwStyle in BSGraphics::InitWindows
        TiltedPhoques::Put(mem::pointer(initWindowLoc.GetPtr()) + 0x174 + 1, WS_OVERLAPPEDWINDOW);
#endif

#if TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE, TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE_VR_RESOLVED)
        const VersionDbPtr<void> windowLoc(68781);
        // TODO: move me to input patches.
        // don't let the game steal the media keys in windowed mode
        TiltedPhoques::Put(
            mem::pointer(windowLoc.GetPtr()) + 0x55 + 2,
            /*strip DISCL_EXCLUSIVE bits and append DISCL_NONEXCLUSIVE*/ 3);
#endif

#if TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER, TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER_VR_RESOLVED)
        const VersionDbPtr<void> timerLoc(77246);

        TiltedPhoques::SwapCall(mem::pointer(timerLoc.GetPtr()) + 9, StopTimer, &Hook_StopTimer);

        // Once we find a proper way to locate it for different versions, go back to swapcall
        // TiltedPhoques::SwapCall(mem::pointer(initLoc.GetPtr()) + 0xD1A, Renderer_Init, &Hook_Renderer_Init);
#endif
        InstallVrRenderBringupHooks();
    });
} // namespace BSGraphics
