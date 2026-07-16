#include <Services/VRGrabRelayService.h>

#include <cstddef>

#include <GameServer.h>
#include <Game/Player.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyVRGrabEvent.h>
#include <Messages/RequestVRGrabEvent.h>
#include <Structs/GameplayCapabilities.h>
#include <Structs/VRGrabEvent.h>

namespace
{
constexpr uint64_t kMinGrabRelayIntervalMs = 45;
constexpr uint64_t kObjectAuthorityLeaseDurationMs = 1000;
constexpr size_t kMaxObjectAuthorityLeases = 1024;

struct ObjectAuthorityLease
{
    uint32_t OwnerPlayerId{0};
    uint64_t ExpiryTick{0};
};

TiltedPhoques::Map<GameId, ObjectAuthorityLease> s_objectAuthorityLeases{};

bool IsNewerSequence(uint32_t aCandidate, uint32_t aCurrent) noexcept
{
    return static_cast<int32_t>(aCandidate - aCurrent) > 0;
}

bool HasGrabObject(const VRGrabEvent& acGrab) noexcept
{
    return static_cast<bool>(acGrab.ObjectId);
}

bool IsLeaseExpired(const ObjectAuthorityLease& acLease, uint64_t aTick) noexcept
{
    return aTick >= acLease.ExpiryTick;
}
}

bool VRObjectAuthority::AcquireOrRenew(const GameId& acObjectId, uint32_t aPlayerId, uint64_t aTick) noexcept
{
    if (!acObjectId || aPlayerId == 0)
        return false;

    const auto it = s_objectAuthorityLeases.find(acObjectId);
    if (it != s_objectAuthorityLeases.end())
    {
        if (!IsLeaseExpired(it->second, aTick) && it->second.OwnerPlayerId != aPlayerId)
            return false;

        it->second = {aPlayerId, aTick + kObjectAuthorityLeaseDurationMs};
        return true;
    }

    if (s_objectAuthorityLeases.size() >= kMaxObjectAuthorityLeases)
        return false;

    s_objectAuthorityLeases.emplace(acObjectId, ObjectAuthorityLease{aPlayerId, aTick + kObjectAuthorityLeaseDurationMs});
    return true;
}

bool VRObjectAuthority::Release(const GameId& acObjectId, uint32_t aPlayerId) noexcept
{
    const auto it = s_objectAuthorityLeases.find(acObjectId);
    if (it == s_objectAuthorityLeases.end() || it->second.OwnerPlayerId != aPlayerId)
        return false;

    s_objectAuthorityLeases.erase(it);
    return true;
}

void VRObjectAuthority::ReleasePlayer(uint32_t aPlayerId) noexcept
{
    auto it = s_objectAuthorityLeases.begin();
    while (it != s_objectAuthorityLeases.end())
    {
        if (it->second.OwnerPlayerId == aPlayerId)
            it = s_objectAuthorityLeases.erase(it);
        else
            ++it;
    }
}

void VRObjectAuthority::Expire(uint64_t aTick) noexcept
{
    auto it = s_objectAuthorityLeases.begin();
    while (it != s_objectAuthorityLeases.end())
    {
        if (IsLeaseExpired(it->second, aTick))
            it = s_objectAuthorityLeases.erase(it);
        else
            ++it;
    }
}

void VRObjectAuthority::Reset() noexcept
{
    s_objectAuthorityLeases.clear();
}

VRGrabRelayService::VRGrabRelayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_vrGrabEventConnection(aDispatcher.sink<PacketEvent<RequestVRGrabEvent>>().connect<&VRGrabRelayService::OnVRGrabEvent>(this))
    , m_playerLeaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&VRGrabRelayService::OnPlayerLeave>(this))
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&VRGrabRelayService::OnUpdate>(this))
{
    VRObjectAuthority::Reset();
}

VRGrabRelayService::~VRGrabRelayService() noexcept
{
    VRObjectAuthority::Reset();
}

void VRGrabRelayService::OnVRGrabEvent(const PacketEvent<RequestVRGrabEvent>& acMessage) noexcept
{
    TP_UNUSED(m_world);

    if (!acMessage.pPlayer)
    {
        static bool s_loggedMissingPlayer = false;
        if (!s_loggedMissingPlayer)
        {
            spdlog::warn("Ignoring VR grab relay packet without a player");
            s_loggedMissingPlayer = true;
        }
        return;
    }

    if (!SkyrimTogether::Protocol::HasCapability(
            acMessage.pPlayer->GetGameplayCapabilities(), SkyrimTogether::Protocol::GameplayCapability::VRGrabRelay))
        return;

    const auto playerId = acMessage.pPlayer->GetId();
    if (!ShouldRelayGrab(playerId, acMessage.Packet))
        return;

    NotifyVRGrabEvent notify{};
    notify.PlayerId = playerId;
    notify.Grab = acMessage.Packet.Grab;

    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !GameServer::Get()->SendToPlayersWithCapabilitiesInRange(
            notify, *character,
            SkyrimTogether::Protocol::ToMask(SkyrimTogether::Protocol::GameplayCapability::VRGrabRelay),
            acMessage.pPlayer))
        spdlog::warn("VR relay dropped because sender has no routable character");
}

void VRGrabRelayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
    {
        m_playerGrabRelayState.erase(acEvent.pPlayer->GetId());
        VRObjectAuthority::ReleasePlayer(acEvent.pPlayer->GetId());
    }
}

void VRGrabRelayService::OnUpdate(const UpdateEvent&) noexcept
{
    VRObjectAuthority::Expire(GameServer::Get()->GetTick());
}

bool VRGrabRelayService::ShouldRelayGrab(uint32_t aPlayerId, const RequestVRGrabEvent& acRequest) noexcept
{
    const auto& grab = acRequest.Grab;
    if (grab.Sequence == 0 || !HasGrabObject(grab))
        return false;

    auto& state = m_playerGrabRelayState[aPlayerId];
    if (state.HasSequence && !IsNewerSequence(grab.Sequence, state.LastSequence))
        return false;

    const auto now = GameServer::Get()->GetTick();
    if (state.LastRelayTick != 0 && now >= state.LastRelayTick && now - state.LastRelayTick < kMinGrabRelayIntervalMs)
        return false;

    const bool hasAuthority = grab.Grabbed
        ? VRObjectAuthority::AcquireOrRenew(grab.ObjectId, aPlayerId, now)
        : VRObjectAuthority::Release(grab.ObjectId, aPlayerId);
    if (!hasAuthority)
        return false;

    state.LastSequence = grab.Sequence;
    state.LastRelayTick = now;
    state.HasSequence = true;
    return true;
}
