#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <GameServer.h>
#include <Services/ServerListService.h>

#include <console/Setting.h>
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

extern Console::Setting<uint32_t> uMaxPlayerCount;

static constexpr char kAnnouncementUrl[] = "https://skyrim-reborn-list.skyrim-together.com/announce";
static constexpr auto kAnnouncementConnectTimeout = std::chrono::seconds(3);
static constexpr auto kAnnouncementTotalTimeout = std::chrono::seconds(5);
static constexpr long kAnnouncementPollIntervalMilliseconds = 100;
static constexpr size_t kMaximumErrorResponseBytes = 4 * 1024;

static Console::Setting bAnnounceServer{"LiveServices:bAnnounceServer", "Whether to list the server on the public server list", false};

namespace
{
std::once_flag s_curlGlobalInit;
std::atomic_bool s_curlInitialized{false};

bool EnsureCurlGlobalInit() noexcept
{
    std::call_once(s_curlGlobalInit, [] {
        s_curlInitialized.store(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, std::memory_order_release);
    });

    return s_curlInitialized.load(std::memory_order_acquire);
}

struct CurlHandleDeleter
{
    void operator()(CURL* apHandle) const noexcept
    {
        if (apHandle)
            curl_easy_cleanup(apHandle);
    }
};

struct CurlMultiDeleter
{
    void operator()(CURLM* apMulti) const noexcept
    {
        if (apMulti)
            curl_multi_cleanup(apMulti);
    }
};

using CurlHandle = std::unique_ptr<CURL, CurlHandleDeleter>;
using CurlMulti = std::unique_ptr<CURLM, CurlMultiDeleter>;

class CurlMultiMembership
{
public:
    CurlMultiMembership(CURLM* apMulti, CURL* apHandle) noexcept
        : m_pMulti(apMulti)
        , m_pHandle(apHandle)
    {
    }

    ~CurlMultiMembership() noexcept
    {
        if (m_added)
            curl_multi_remove_handle(m_pMulti, m_pHandle);
    }

    TP_NOCOPYMOVE(CurlMultiMembership);

    CURLMcode Add() noexcept
    {
        const auto result = curl_multi_add_handle(m_pMulti, m_pHandle);
        m_added = result == CURLM_OK;
        return result;
    }

private:
    CURLM* m_pMulti;
    CURL* m_pHandle;
    bool m_added = false;
};

struct CurlTransferContext
{
    const std::atomic_bool& m_stopRequested;
    std::string m_errorResponse;
};

size_t WriteResponse(char* apData, size_t aSize, size_t aCount, void* apUserData) noexcept
{
    if (aSize != 0 && aCount > std::numeric_limits<size_t>::max() / aSize)
        return 0;

    try
    {
        const auto byteCount = aSize * aCount;
        auto& context = *static_cast<CurlTransferContext*>(apUserData);
        const auto remaining = kMaximumErrorResponseBytes - std::min(kMaximumErrorResponseBytes, context.m_errorResponse.size());
        context.m_errorResponse.append(apData, std::min(remaining, byteCount));
        return byteCount;
    }
    catch (...)
    {
        return 0;
    }
}

int CheckForStop(void* apUserData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
{
    const auto& stopRequested = *static_cast<const std::atomic_bool*>(apUserData);
    return stopRequested.load(std::memory_order_acquire) ? 1 : 0;
}

bool AppendFormField(CURL* apHandle, std::string_view acName, std::string_view acValue, std::string& arBody)
{
    if (acValue.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;

    char* pEscapedValue = curl_easy_escape(apHandle, acValue.data(), static_cast<int>(acValue.size()));
    if (!pEscapedValue)
        return false;

    const auto escapedValue = std::unique_ptr<char, decltype(&curl_free)>{pEscapedValue, &curl_free};
    if (!arBody.empty())
        arBody.push_back('&');

    arBody.append(acName);
    arBody.push_back('=');
    arBody.append(escapedValue.get());
    return true;
}

bool IsCurlOptionFailure(CURLcode aCode, const char* acOption) noexcept
{
    if (aCode == CURLE_OK)
        return false;

    spdlog::error("Server list could not configure {}: {}", acOption, curl_easy_strerror(aCode));
    return true;
}
}

ServerListService::ServerListService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&ServerListService::OnUpdate>(this))
    , m_playerJoinConnection(aDispatcher.sink<PlayerJoinEvent>().connect<&ServerListService::OnPlayerJoin>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&ServerListService::OnPlayerLeave>(this))
    , m_nextAnnounce(std::chrono::seconds(0))
    , m_announcementState(std::make_shared<AnnouncementState>())
{
    if (!bAnnounceServer)
        spdlog::warn("bAnnounceServer is set to false. The server will not show up as a public server. "
                     "If you are just playing with friends, this is probably what you want.");
}

ServerListService::~ServerListService() noexcept
{
    m_updateConnection.release();
    m_playerJoinConnection.release();
    m_playerLeaveConnection.release();

    // c-ares keeps DNS non-blocking; this flag is checked between every
    // multi-poll interval and by libcurl's progress callback.
    const auto state = m_announcementState;
    state->m_stopRequested.store(true, std::memory_order_release);

    if (m_announcementThread.joinable())
        m_announcementThread.join();
}

void ServerListService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (ProcessAnnouncementResult())
        return;

    if (m_nextAnnounce < std::chrono::steady_clock::now())
    {
        Announce();

        m_nextAnnounce = (std::chrono::steady_clock::now() + std::chrono::minutes(1));
    }
}

void ServerListService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    if (ProcessAnnouncementResult())
        return;

    Announce();

    m_nextAnnounce = (std::chrono::steady_clock::now() + std::chrono::minutes(1));
}

void ServerListService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (ProcessAnnouncementResult())
        return;

    Announce();

    m_nextAnnounce = (std::chrono::steady_clock::now() + std::chrono::minutes(1));
}

void ServerListService::Announce() noexcept
{
    const auto state = m_announcementState;
    if (state->m_stopRequested.load(std::memory_order_acquire) ||
        state->m_inFlight.exchange(true, std::memory_order_acq_rel))
        return;

    if (m_announcementThread.joinable())
        m_announcementThread.join();

    if (state->m_stopRequested.load(std::memory_order_acquire))
    {
        state->m_inFlight.store(false, std::memory_order_release);
        return;
    }

    if (!EnsureCurlGlobalInit())
    {
        spdlog::error("Server list could not initialize libcurl");
        state->m_inFlight.store(false, std::memory_order_release);
        return;
    }

    auto* pServer = GameServer::Get();
    if (!pServer)
    {
        state->m_inFlight.store(false, std::memory_order_release);
        return;
    }

    const auto playerCount = static_cast<uint16_t>(m_world.GetPlayerManager().Count());
    const auto isPublic = static_cast<bool>(bAnnounceServer);
    const auto isPasswordProtected = pServer->IsPasswordProtected();

    if (isPublic)
        m_flags |= kIsPublic;
    if (isPasswordProtected)
        m_flags |= kHasPassword;

    const auto& info = pServer->GetInfo();
    AnnouncementRequest request{
        .m_name = info.name,
        .m_desc = info.desc,
        .m_iconUrl = info.icon_url,
        .m_port = pServer->GetPort(),
        .m_tickRate = info.tick_rate,
        .m_playerCount = playerCount,
        .m_playerMaxCount = uMaxPlayerCount.value_as<uint16_t>(),
        .m_tagList = info.tagList,
        .m_isPublic = isPublic,
        .m_isPasswordProtected = isPasswordProtected,
        .m_flags = m_flags,
    };

    try
    {
        m_announcementThread = std::thread([state, request = std::move(request)]() mutable {
            bool serverRejected = false;
            try
            {
                serverRejected = PostAnnouncement(*state, request);
            }
            catch (const std::exception& exception)
            {
                spdlog::error("Server list announcement failed: {}", exception.what());
            }
            catch (...)
            {
                spdlog::error("Server list announcement failed with an unknown exception");
            }

            if (serverRejected && !state->m_stopRequested.load(std::memory_order_acquire))
                state->m_serverRejected.store(true, std::memory_order_release);

            state->m_inFlight.store(false, std::memory_order_release);
        });
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Server list could not start the announcement worker: {}", exception.what());
        state->m_inFlight.store(false, std::memory_order_release);
    }
}

bool ServerListService::ProcessAnnouncementResult() noexcept
{
    if (!m_announcementState->m_serverRejected.exchange(false, std::memory_order_acq_rel))
        return false;

    if (auto* pServer = GameServer::Get())
        pServer->Kill();

    return true;
}

bool ServerListService::PostAnnouncement(const AnnouncementState& acState, const AnnouncementRequest& acRequest)
{
    CurlMulti multi{curl_multi_init()};
    CurlHandle handle{curl_easy_init()};
    if (!handle || !multi)
    {
        spdlog::error("Server list could not create a libcurl transfer");
        return false;
    }

    const std::string version{BUILD_COMMIT};
    std::string body;
    if (!AppendFormField(handle.get(), "name", {acRequest.m_name.c_str(), acRequest.m_name.size()}, body) ||
        !AppendFormField(handle.get(), "desc", {acRequest.m_desc.c_str(), acRequest.m_desc.size()}, body) ||
        !AppendFormField(handle.get(), "icon_url", {acRequest.m_iconUrl.c_str(), acRequest.m_iconUrl.size()}, body) ||
        !AppendFormField(handle.get(), "version", version, body) ||
        !AppendFormField(handle.get(), "port", std::to_string(acRequest.m_port), body) ||
        !AppendFormField(handle.get(), "tick", std::to_string(acRequest.m_tickRate), body) ||
        !AppendFormField(handle.get(), "player_count", std::to_string(acRequest.m_playerCount), body) ||
        !AppendFormField(handle.get(), "max_player_count", std::to_string(acRequest.m_playerMaxCount), body) ||
        !AppendFormField(handle.get(), "tags", {acRequest.m_tagList.c_str(), acRequest.m_tagList.size()}, body) ||
        !AppendFormField(handle.get(), "public", acRequest.m_isPublic ? "true" : "false", body) ||
        !AppendFormField(handle.get(), "pass", acRequest.m_isPasswordProtected ? "true" : "false", body) ||
        !AppendFormField(handle.get(), "flags", std::to_string(acRequest.m_flags), body))
    {
        spdlog::error("Server list could not encode the announcement request");
        return false;
    }

    CurlTransferContext transferContext{acState.m_stopRequested};
    // CURLOPT_TIMEOUT_MS covers the full libcurl transfer, including the
    // c-ares asynchronous DNS stage selected in xmake.lua.
    const auto totalTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(kAnnouncementTotalTimeout).count();
    const auto connectTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(kAnnouncementConnectTimeout).count();
    curl_slist* pHeaders = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
    const auto headers = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>{pHeaders, &curl_slist_free_all};
    if (!headers)
    {
        spdlog::error("Server list could not allocate HTTP headers");
        return false;
    }

    if (IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_URL, kAnnouncementUrl), "URL") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_POST, 1L), "POST") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get()), "headers") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.data()), "request body") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                                             static_cast<curl_off_t>(body.size())), "request body size") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS,
                                             static_cast<long>(connectTimeout)), "connection timeout") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(totalTimeout)),
                            "total timeout") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L), "signal handling") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 0L), "certificate verification") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 0L), "host verification") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &WriteResponse), "write callback") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &transferContext), "write context") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L), "progress callback") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, &CheckForStop), "progress callback") ||
        IsCurlOptionFailure(curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &acState.m_stopRequested), "progress context"))
    {
        return false;
    }

    CurlMultiMembership membership{multi.get(), handle.get()};
    if (const auto addResult = membership.Add(); addResult != CURLM_OK)
    {
        spdlog::error("Server list could not start the libcurl transfer: {}", curl_multi_strerror(addResult));
        return false;
    }

    CURLcode transferResult = CURLE_OK;
    bool completed = false;
    while (!acState.m_stopRequested.load(std::memory_order_acquire) && !completed)
    {
        int runningHandles = 0;
        CURLMcode multiResult = CURLM_OK;
        do
        {
            multiResult = curl_multi_perform(multi.get(), &runningHandles);
        } while (multiResult == CURLM_CALL_MULTI_PERFORM);

        if (multiResult != CURLM_OK)
        {
            spdlog::error("Server list transfer failed: {}", curl_multi_strerror(multiResult));
            return false;
        }

        int messageCount = 0;
        while (auto* pMessage = curl_multi_info_read(multi.get(), &messageCount))
        {
            if (pMessage->msg == CURLMSG_DONE && pMessage->easy_handle == handle.get())
            {
                transferResult = pMessage->data.result;
                completed = true;
                break;
            }
        }

        if (!completed && runningHandles == 0)
        {
            spdlog::error("Server list transfer stopped without a completion result");
            return false;
        }

        if (!completed)
        {
            int readyHandles = 0;
            multiResult = curl_multi_poll(multi.get(), nullptr, 0, kAnnouncementPollIntervalMilliseconds, &readyHandles);
            if (multiResult != CURLM_OK)
            {
                spdlog::error("Server list transfer polling failed: {}", curl_multi_strerror(multiResult));
                return false;
            }
        }
    }

    if (acState.m_stopRequested.load(std::memory_order_acquire))
        return false;

    if (transferResult != CURLE_OK)
    {
        spdlog::error("Server could not reach the server list: {}", curl_easy_strerror(transferResult));
        return false;
    }

    long responseCode = 0;
    if (const auto infoResult = curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &responseCode); infoResult != CURLE_OK)
    {
        spdlog::error("Server list could not read the HTTP response code: {}", curl_easy_strerror(infoResult));
        return false;
    }

    if (responseCode == 403)
        return true;

    if (responseCode != 200)
        spdlog::error("Server list error {}: {}", responseCode, transferContext.m_errorResponse);

    return false;
}
