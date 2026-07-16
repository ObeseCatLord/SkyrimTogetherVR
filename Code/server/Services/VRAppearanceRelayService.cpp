#include <Services/VRAppearanceRelayService.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/PlayerCellChangedEvent.h>
#include <Events/CharacterSpawnedEvent.h>
#include <Components.h>
#include <Game/Player.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyVRAppearance.h>
#include <Messages/RequestVRAppearance.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRAppearance.h>

namespace
{
bool IsNewerSequence(const std::uint32_t aCandidate, const std::uint32_t aCurrent) noexcept
{
    return static_cast<std::int32_t>(aCandidate - aCurrent) > 0;
}

constexpr auto kAppearanceCapability =
    SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay);
} // namespace

VRAppearanceRelayService::VRAppearanceRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrAppearanceConnection(aDispatcher.sink<PacketEvent<RequestVRAppearance>>().connect<&VRAppearanceRelayService::OnVRAppearance>(this))
    , m_playerJoinConnection(aDispatcher.sink<PlayerJoinEvent>().connect<&VRAppearanceRelayService::OnPlayerJoin>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRAppearanceRelayService::OnPlayerLeave>(this))
    , m_playerCellChangedConnection(aDispatcher.sink<PlayerCellChangedEvent>().connect<&VRAppearanceRelayService::OnPlayerCellChanged>(this))
    , m_characterSpawnedConnection(aDispatcher.sink<CharacterSpawnedEvent>().connect<&VRAppearanceRelayService::OnCharacterSpawned>(this))
{
}

void VRAppearanceRelayService::OnVRAppearance(const PacketEvent<RequestVRAppearance>& acMessage) noexcept
{
    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR appearance relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay) ||
        !AcceptAppearance(acMessage.pPlayer->GetId(), acMessage.Packet.Appearance))
        return;

    NotifyVRAppearance notify{};
    notify.PlayerId = acMessage.pPlayer->GetId();
    notify.Appearance = acMessage.Packet.Appearance;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character)
        return;
    if (!GameServer::Get()->SendToPlayersWithCapabilitiesInRange(notify, *character, kAppearanceCapability, acMessage.pPlayer))
        spdlog::warn("VR appearance relay dropped because sender has no routable character");
}

void VRAppearanceRelayService::OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) noexcept
{
    const auto* ownerComponent = m_world.try_get<OwnerComponent>(acEvent.Entity);
    auto* owner = ownerComponent ? ownerComponent->GetOwner() : nullptr;
    if (!owner || !SkyrimTogether::Protocol::HasCapability(
                      owner->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay))
        return;

    const auto appearance = m_latestAppearance.find(owner->GetId());
    if (appearance != m_latestAppearance.end()) {
        NotifyVRAppearance notify{};
        notify.PlayerId = owner->GetId();
        notify.Appearance = appearance->second;
        if (!GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
                notify, acEvent.Entity, kAppearanceCapability, owner))
            spdlog::warn("VR appearance replay dropped for character {:X}", static_cast<std::uint32_t>(acEvent.Entity));
    }

    ReplayRelevantAppearances(*owner);
}

void VRAppearanceRelayService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    if (!acEvent.pPlayer ||
        !SkyrimTogether::Protocol::HasCapability(
            acEvent.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay))
        return;

    ReplayRelevantAppearances(*acEvent.pPlayer);
}

void VRAppearanceRelayService::ReplayRelevantAppearances(Player& aPlayer) noexcept
{
    if (!SkyrimTogether::Protocol::HasCapability(
            aPlayer.GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay) ||
        !aPlayer.GetCellComponent())
        return;

    for (const auto& [playerId, appearance] : m_latestAppearance) {
        if (playerId == aPlayer.GetId())
            continue;
        const auto* source = m_world.GetPlayerManager().GetById(playerId);
        if (!source)
            continue;
        if (!SkyrimTogether::Protocol::HasCapability(
                source->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay) ||
            !source->GetCellComponent())
            continue;
        bool isDragon = false;
        const auto sourceCharacter = source->GetCharacter();
        if (sourceCharacter && m_world.valid(*sourceCharacter))
        {
            if (const auto* character = m_world.try_get<CharacterComponent>(*sourceCharacter))
                isDragon = character->IsDragon();
        }
        if (!source->GetCellComponent().IsInRange(aPlayer.GetCellComponent(), isDragon))
            continue;

        NotifyVRAppearance notify{};
        notify.PlayerId = playerId;
        notify.Appearance = appearance;
        aPlayer.Send(notify);
    }
}

void VRAppearanceRelayService::ReplayOwnAppearance(Player& aPlayer) noexcept
{
    const auto appearance = m_latestAppearance.find(aPlayer.GetId());
    if (appearance == m_latestAppearance.end() || !aPlayer.GetCellComponent())
        return;

    bool isDragon = false;
    if (const auto character = aPlayer.GetCharacter(); character && m_world.valid(*character))
    {
        if (const auto* component = m_world.try_get<CharacterComponent>(*character))
            isDragon = component->IsDragon();
    }

    NotifyVRAppearance notify{};
    notify.PlayerId = aPlayer.GetId();
    notify.Appearance = appearance->second;
    for (auto* player : m_world.GetPlayerManager())
    {
        if (player == &aPlayer ||
            !SkyrimTogether::Protocol::HasCapability(
                player->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay) ||
            !player->GetCellComponent())
            continue;
        if (aPlayer.GetCellComponent().IsInRange(player->GetCellComponent(), isDragon))
            player->Send(notify);
    }
}

void VRAppearanceRelayService::OnPlayerCellChanged(const PlayerCellChangedEvent& acEvent) noexcept
{
    if (!acEvent.pPlayer ||
        !SkyrimTogether::Protocol::HasCapability(
            acEvent.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRAppearanceRelay))
        return;

    ReplayRelevantAppearances(*acEvent.pPlayer);
    ReplayOwnAppearance(*acEvent.pPlayer);
}

void VRAppearanceRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_latestAppearance.erase(acEvent.pPlayer->GetId());
}

bool VRAppearanceRelayService::AcceptAppearance(
    const std::uint32_t aPlayerId, const VRAppearance& acAppearance) noexcept
{
    if (!acAppearance.IsValid())
        return false;

    const auto it = m_latestAppearance.find(aPlayerId);
    if (it != m_latestAppearance.end() && !IsNewerSequence(acAppearance.Sequence, it->second.Sequence))
        return false;

    m_latestAppearance[aPlayerId] = acAppearance;
    return true;
}
