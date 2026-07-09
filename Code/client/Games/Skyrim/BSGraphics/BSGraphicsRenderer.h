#pragma once

#include <RuntimeLayout.h>

#include <cstddef>
#include <cstdint>
#include <d3d11.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

namespace BSGraphics
{
struct __declspec(align(8)) BSCriticalSection
{
    _RTL_CRITICAL_SECTION CriticalSection;
    unsigned int ulThreadOwner;
    unsigned int ulPrevOwner;
    unsigned int uiLockCount;
};

struct RenderTarget
{
    ID3D11Texture2D* pTexture;
    ID3D11Texture2D* pCopyTexture;
    ID3D11RenderTargetView* pRTView;
    ID3D11ShaderResourceView* pSRView;
    ID3D11ShaderResourceView* pCopySRView;
    ID3D11UnorderedAccessView* pUAView;
};

struct RendererWindow
{
    HWND hWnd;
    int iWindowX;
    int iWindowY;
    int uiWindowWidth;
    int uiWindowHeight;
    IDXGISwapChain* pSwapChain;
    BSGraphics::RenderTarget SwapChainRenderTarget;

    bool IsForeground();
};

RendererWindow* GetMainWindow();

struct DepthStencilTarget
{
    ID3D11Texture2D* pTexture;
    ID3D11DepthStencilView* pDSView[4];
    ID3D11DepthStencilView* pDSViewReadOnlyDepth[4];
    ID3D11DepthStencilView* pDSViewReadOnlyStencil[4];
    ID3D11DepthStencilView* pDSViewReadOnlyDepthStencil[4];
    ID3D11ShaderResourceView* pSRViewDepth;
    ID3D11ShaderResourceView* pSRViewStencil;
};

struct CubeMapRenderTarget
{
    ID3D11Texture2D* pTexture;
    ID3D11RenderTargetView* pRTView[6];
    ID3D11ShaderResourceView* pSRView;
};

struct RendererData
{
    using CommonLibRendererDataOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererDataCommonLibNgOffsets;
    using LocalRendererDataOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererDataLocalShimOffsets;

    [[nodiscard]] ID3D11Device* GetForwarderData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<ID3D11Device*>(this, CommonLibRendererDataOffsets::Forwarder);
#else
        return pForwarder;
#endif
    }

    [[nodiscard]] ID3D11DeviceContext* GetContextData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<ID3D11DeviceContext*>(this, CommonLibRendererDataOffsets::Context);
#else
        return pContext;
#endif
    }

    [[nodiscard]] RendererWindow* GetRenderWindowData(std::uint32_t aIndex = 0) noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<RendererWindow>(this, CommonLibRendererDataOffsets::RenderWindows) + aIndex;
#else
        return &RenderWindowA[aIndex];
#endif
    }

    [[nodiscard]] const RendererWindow* GetRenderWindowData(std::uint32_t aIndex = 0) const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<RendererWindow>(this, CommonLibRendererDataOffsets::RenderWindows) + aIndex;
#else
        return &RenderWindowA[aIndex];
#endif
    }

    [[nodiscard]] RenderTarget* GetRenderTargetsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<RenderTarget>(this, CommonLibRendererDataOffsets::RenderTargets);
#else
        return pRenderTargetsA;
#endif
    }

    [[nodiscard]] DepthStencilTarget* GetDepthStencilTargetsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<DepthStencilTarget>(
            this, CommonLibRendererDataOffsets::DepthStencilTargets);
#else
        return pDepthStencilTargetsA;
#endif
    }

    [[nodiscard]] CubeMapRenderTarget* GetCubeMapRenderTargetsData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ptr<CubeMapRenderTarget>(
            this, CommonLibRendererDataOffsets::CubeMapRenderTargets);
#else
        return pCubeMapRenderTargetsA;
#endif
    }

    uint32_t uiAdapter;            // 0x0000
    uint64_t DesiredRefreshRate;   // 0x0004
    uint64_t ActualRefreshRate;    // 0x000C
    uint32_t ScaleMode;            // 0x0014
    uint32_t ScanlineMode;         // 0x0018
    int32_t bFullScreen;           // 0x001C
    bool bAppFullScreen;           // 0x0020
    bool bBorderlessWindow;        // 0x0021
    bool bVSync;                   // 0x0022
    bool bInitialized;             // 0x0023
    bool bRequestWindowSizeChange; // 0x0024
    uint32_t uiNewWidth;           // 0x0028
    uint32_t uiNewHeight;          // 0x002C
    uint32_t uiPresentInterval;    // 0x0030
    ID3D11Device* pForwarder;      // 0x0038
    ID3D11DeviceContext* pContext; // 0x0040

    BSGraphics::RendererWindow RenderWindowA[32]; // V
    BSGraphics::RenderTarget pRenderTargetsA[114];
    BSGraphics::DepthStencilTarget pDepthStencilTargetsA[12];
    BSGraphics::CubeMapRenderTarget pCubeMapRenderTargetsA[1];
};

static_assert(offsetof(RendererData, pForwarder) == RendererData::LocalRendererDataOffsets::Forwarder);
static_assert(offsetof(RendererData, pContext) == RendererData::LocalRendererDataOffsets::Context);
static_assert(offsetof(RendererData, RenderWindowA) == RendererData::LocalRendererDataOffsets::RenderWindows);
static_assert(offsetof(RendererData, pRenderTargetsA) == RendererData::LocalRendererDataOffsets::RenderTargets);
static_assert(offsetof(RendererData, pDepthStencilTargetsA) == RendererData::LocalRendererDataOffsets::DepthStencilTargets);
static_assert(offsetof(RendererData, pCubeMapRenderTargetsA) == RendererData::LocalRendererDataOffsets::CubeMapRenderTargets);

struct Renderer
{
    using CommonLibRendererOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererCommonLibNgOffsets;
    using LocalRendererOffsets = Skyrim::RuntimeLayout::BSGraphicsRendererLocalShimOffsets;

    [[nodiscard]] RendererData& GetRendererData() noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<RendererData>(this, CommonLibRendererOffsets::Data);
#else
        return Data;
#endif
    }

    [[nodiscard]] const RendererData& GetRendererData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Ref<RendererData>(this, CommonLibRendererOffsets::Data);
#else
        return Data;
#endif
    }

    bool bSkipNextPresent;
    void (*ResetRenderTargets)();
    BSGraphics::RendererData Data;
};
static_assert(offsetof(Renderer, Data) == Renderer::LocalRendererOffsets::Data);

struct RendererInitReturn
{
    HWND hwnd;
};

// former ViewportConfig
struct RendererInitOSData
{
    HWND hWnd;
    HINSTANCE hInstance;
    WNDPROC pWndProc;
    HICON hIcon;
    const char* pClassName;
    uint32_t uiAdapter;
    int bCreateSwapChainRenderTarget;
};

// former WindowConfig
struct ApplicationWindowProperties
{
    uint32_t uiWidth;  // V
    uint32_t uiHeight; // V
    int iX;
    int iY;
    uint32_t uiRefreshRate;
    bool bFullScreen;
    bool bBorderlessWindow;
    bool bVSync;
    uint32_t uiPresentInterval;
};

} // namespace BSGraphics
