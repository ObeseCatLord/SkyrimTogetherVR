#pragma once

#include <Structs/GameId.h>
#include <Structs/LockData.h>
#include <Structs/Inventory.h>
#include <Structs/GridCellCoords.h>

struct ObjectData
{
    static constexpr uint8_t kMaximumOpenState = 4;

    ObjectData() = default;
    ~ObjectData() = default;

    bool operator==(const ObjectData& acRhs) const noexcept;
    bool operator!=(const ObjectData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    [[nodiscard]] constexpr bool HasValidCurrentOpenState() const noexcept
    {
        return HasCurrentOpenState ? CurrentOpenState > 0 && CurrentOpenState <= kMaximumOpenState :
                                     CurrentOpenState == 0;
    }

    uint32_t ServerId{};
    GameId Id{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CurrentCoords{};
    LockData CurrentLockData{};
    Inventory CurrentInventory{};
    // Optional authoritative current state for an open-close reference.
    // None (0) is not a meaningful canonical open state and is never present.
    bool HasCurrentOpenState{};
    uint8_t CurrentOpenState{};
    bool IsSenderFirst{};
    bool IsDecodedValid{true};
};
