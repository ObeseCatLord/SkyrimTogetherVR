#pragma once

struct Player;

/**
 * Dispatched after the server has committed a player's new interest cell.
 */
struct PlayerCellChangedEvent
{
    Player* pPlayer;
};
