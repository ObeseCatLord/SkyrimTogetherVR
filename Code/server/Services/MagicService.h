#pragma once

#include <Events/PacketEvent.h>
#include <Messages/RemoveSpellRequest.h>

#include <cstdint>
#include <unordered_map>

struct World;
struct SpellCastRequest;
struct InterruptCastRequest;
struct AddTargetRequest;
struct CharacterRemoveEvent;

/**
 * @brief Relays spell casting and magic effects.
 */
struct MagicService
{
    MagicService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~MagicService() noexcept = default;

    TP_NOCOPYMOVE(MagicService);

protected:
    /**
     * @brief Relays spell cast messages to other clients.
     */
    void OnSpellCastRequest(const PacketEvent<SpellCastRequest>& acMessage) noexcept;
    /**
     * @brief Relays spell interrupt messages to other clients.
     */
    void OnInterruptCastRequest(const PacketEvent<InterruptCastRequest>& acMessage) noexcept;
    /**
     * @brief Relays magic effect messages to other clients.
     */
    void OnAddTargetRequest(const PacketEvent<AddTargetRequest>& acMessage) noexcept;
    /**
    * @brief Relays spell removal messages to other clients.
    */
    void OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) noexcept;
    void OnCharacterRemove(const CharacterRemoveEvent& acEvent) noexcept;


private:
    [[nodiscard]] std::uint32_t NextMagicEventId(std::uint32_t aActorId) noexcept;

    World& m_world;
    std::unordered_map<std::uint32_t, std::uint32_t> m_magicEventIds{};

    entt::scoped_connection m_spellCastConnection;
    entt::scoped_connection m_interruptCastConnection;
    entt::scoped_connection m_addTargetConnection;
    entt::scoped_connection m_removeSpellConnection;
    entt::scoped_connection m_characterRemoveConnection;
};
