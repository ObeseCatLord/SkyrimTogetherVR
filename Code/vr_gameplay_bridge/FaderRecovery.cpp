#include "pch.h"

#include "FaderRecovery.h"

#include "FaderRecoveryPolicy.h"

#include <chrono>
#include <cstring>

namespace SkyrimTogetherVR::GameplayAdapter::FaderRecovery
{
namespace
{
using Clock = std::chrono::steady_clock;

constexpr auto kTransitionQuietPeriod = std::chrono::milliseconds(3000);

std::atomic<std::uint64_t> g_lifecycleGeneration{};
std::atomic<std::uint64_t> g_menuGeneration{};
std::atomic_bool g_loadingMenuOpen{};
std::atomic_bool g_menuSinkInstalled{};
std::atomic_bool g_menuSinkFailed{};

class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* aEvent, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!aEvent)
            return RE::BSEventNotifyControl::kContinue;

        g_menuGeneration.fetch_add(1, std::memory_order_release);
        const auto* name = aEvent->menuName.c_str();
        if (name && std::strcmp(name, RE::LoadingMenu::MENU_NAME.data()) == 0)
        {
            g_loadingMenuOpen.store(aEvent->opening, std::memory_order_release);
            // Both edges invalidate the hard-timeout clock. In particular, a
            // long load must not make its post-load fade immediately stale.
            g_lifecycleGeneration.fetch_add(1, std::memory_order_release);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

MenuSink g_menuSink;

struct MenuSnapshot
{
    bool UiAvailable{};
    bool ExactHudAndFader{};
    bool FaderOnStack{};
    bool FaderActive{};
};

[[nodiscard]] MenuSnapshot SnapshotMenus() noexcept
{
    MenuSnapshot snapshot{};
    auto* ui = RE::UI::GetSingleton();
    if (!ui)
        return snapshot;

    const auto hudMenu = ui->GetMenu(RE::HUDMenu::MENU_NAME);
    const auto faderMenu = ui->GetMenu(RE::FaderMenu::MENU_NAME);
    if (!hudMenu || !faderMenu)
    {
        snapshot.UiAvailable = true;
        return snapshot;
    }

    bool hudSeen = false;
    bool faderSeen = false;
    bool unexpectedMenu = false;
    for (const auto& menuHandle : ui->menuStack)
    {
        const auto* menu = menuHandle.get();
        if (!menu || !menu->OnStack() || menu->Modal() || menu->ApplicationMenu())
            return snapshot;

        if (menu == hudMenu.get())
        {
            if (hudSeen)
                return snapshot;
            hudSeen = true;
            continue;
        }
        if (menu == faderMenu.get())
        {
            if (faderSeen)
                return snapshot;
            faderSeen = true;
            snapshot.FaderOnStack = true;
            snapshot.FaderActive = static_cast<const RE::FaderMenu*>(menu)->GetRuntimeData().isActive;
            continue;
        }

        // This rejects Main, RaceSex, Loading, MessageBox, and every other
        // unrecognised presentation state without a generic menu-hide path.
        unexpectedMenu = true;
    }

    snapshot.ExactHudAndFader = hudSeen && faderSeen && !unexpectedMenu;
    snapshot.UiAvailable = true;
    return snapshot;
}

[[nodiscard]] FaderRecoveryPolicy::PlayerContext SnapshotPlayerContext() noexcept
{
    FaderRecoveryPolicy::PlayerContext context{};
    const auto* player = RE::PlayerCharacter::GetSingleton();
    const auto* base = player ? player->GetActorBase() : nullptr;
    const auto* cell = player ? player->GetParentCell() : nullptr;
    if (!player || !base || !cell)
        return context;

    const auto position = player->GetPosition();
    context.Player = reinterpret_cast<std::uintptr_t>(player);
    context.Base = reinterpret_cast<std::uintptr_t>(base);
    context.Cell = reinterpret_cast<std::uintptr_t>(cell);
    context.PlayerFormId = player->GetFormID();
    context.BaseFormId = base->GetFormID();
    context.CellFormId = cell->GetFormID();
    context.PositionX = position.x;
    context.PositionY = position.y;
    context.PositionZ = position.z;
    return context;
}

[[nodiscard]] std::uint64_t ToMilliseconds(const Clock::time_point aTime) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(aTime.time_since_epoch()).count());
}

void InstallMenuSink(RE::UI& aUi) noexcept
{
    if (g_menuSinkFailed.load(std::memory_order_acquire))
        return;

    bool expected = false;
    if (!g_menuSinkInstalled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    try
    {
        aUi.AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
    }
    catch (...)
    {
        g_menuSinkInstalled.store(false, std::memory_order_release);
        g_menuSinkFailed.store(true, std::memory_order_release);
        SKSE::log::warn("SkyrimTogetherVR Fader recovery disabled: menu event sink installation failed");
    }
}
} // namespace

void NotifyLifecycleTransition() noexcept
{
    g_lifecycleGeneration.fetch_add(1, std::memory_order_release);
}

void ProcessOnCommandPumpOwner(const std::uint64_t aServerInstanceNonce, const std::uint64_t aConnectionGeneration) noexcept
{
    try
    {
        const auto now = Clock::now();
        const auto nowMs = ToMilliseconds(now);
        auto* ui = RE::UI::GetSingleton();
        if (ui)
            InstallMenuSink(*ui);

        static FaderRecoveryPolicy::StateMachine state;
        static std::uint64_t observedLifecycleGeneration{};
        static Clock::time_point transitionQuietUntil{};

        const auto lifecycleGeneration = g_lifecycleGeneration.load(std::memory_order_acquire);
        if (lifecycleGeneration != observedLifecycleGeneration)
        {
            observedLifecycleGeneration = lifecycleGeneration;
            transitionQuietUntil = now + kTransitionQuietPeriod;
        }

        const auto menus = SnapshotMenus();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        const auto observation = FaderRecoveryPolicy::Observation{
            .ServerInstanceNonce = aServerInstanceNonce,
            .ConnectionGeneration = aConnectionGeneration,
            .UiAvailable = menus.UiAvailable,
            .ExactHudAndFader = menus.ExactHudAndFader,
            .FaderOnStack = menus.FaderOnStack,
            .FaderActive = menus.FaderActive,
            .TransitionActive = g_loadingMenuOpen.load(std::memory_order_acquire) || now < transitionQuietUntil,
            .CanQueueHide = queue != nullptr,
            .Context = SnapshotPlayerContext(),
            .LifecycleGeneration = lifecycleGeneration,
            .MenuGeneration = g_menuGeneration.load(std::memory_order_acquire),
        };

        switch (state.Observe(observation, nowMs))
        {
        case FaderRecoveryPolicy::Action::Candidate:
        {
            static std::uint64_t loggedCandidateGeneration{};
            if (loggedCandidateGeneration != state.Generation())
            {
                loggedCandidateGeneration = state.Generation();
                SKSE::log::info("SkyrimTogetherVR Fader recovery candidate: generation={} active={}", state.Generation(), observation.FaderActive);
            }
            break;
        }
        case FaderRecoveryPolicy::Action::Hide:
            if (queue)
            {
                queue->AddMessage(RE::FaderMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                SKSE::log::info("SkyrimTogetherVR Fader recovery hide issued: generation={}", state.Generation());
            }
            break;
        case FaderRecoveryPolicy::Action::Verified: SKSE::log::info("SkyrimTogetherVR Fader recovery verified closed: generation={}", state.Generation()); break;
        case FaderRecoveryPolicy::Action::Suppressed:
            SKSE::log::warn("SkyrimTogetherVR Fader recovery suppressed after one unverified hide: generation={}", state.Generation());
            break;
        case FaderRecoveryPolicy::Action::None: break;
        }
    }
    catch (...)
    {
        // UI state is a presentation aid. A failed observation must never make
        // the command pump or gameplay bridge unsafe.
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::FaderRecovery
