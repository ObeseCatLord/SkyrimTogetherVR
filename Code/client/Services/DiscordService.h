#pragma once

#include <atomic>
#include <thread>

#include <discord.h>

class DiscordService final : public entt::registry
{
public:
    DiscordService(entt::dispatcher&);
    ~DiscordService();

    bool Init();

    // noop currently
    void Update();

    inline auto& GetUser() const noexcept { return m_userData; }

    // update the presence state
    // then request an update
    inline auto& GetPresence() { return m_ActivityState; }
    void UpdatePresence(bool newTimeStamp);

    void WndProcHandler(HWND, UINT, WPARAM, LPARAM);

private:
    void InitOverlay(struct IDXGISwapChain*);
    void OnLocationChangeEvent() noexcept;

    entt::scoped_connection m_cellChangeConnection;

    bool m_bCustomPresence = false;
    bool m_bOverlayEnabled = false;
    std::atomic_bool m_stopCallbacks{false};

    std::thread m_callbackThread;

    IDiscordCore* m_pCore = nullptr;
    IDiscordUserManager* m_pUserMgr = nullptr;
    IDiscordActivityManager* m_pActivity = nullptr;
    IDiscordApplicationManager* m_pAppMgr = nullptr;
    IDiscordOverlayManager* m_pOverlayMgr = nullptr;
    HMODULE m_pModule = nullptr;

    DiscordUser m_userData{};
    DiscordActivity m_ActivityState{};

    uint32_t m_lastLocationId = 0;
    uint32_t m_lastWorldspaceId = 0;

    static void OnUserUpdate(void* userp);

    static IDiscordUserEvents s_mUserEvents;
};
