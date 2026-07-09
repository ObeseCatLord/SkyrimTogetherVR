#pragma once

#include <cstdint>

#include <Misc/IMovementState.h>

struct ActorState : IMovementState
{
    virtual ~ActorState();

    uint32_t flags1;
    uint32_t flags2;

    [[nodiscard]] uint32_t GetFlags1Data() const noexcept { return flags1; }

    [[nodiscard]] uint32_t GetFlags2Data() const noexcept { return flags2; }

    void SetFlagsData(uint32_t aFlags1, uint32_t aFlags2) noexcept
    {
        flags1 = aFlags1;
        flags2 = aFlags2;
    }

    bool IsWeaponDrawn() const noexcept { return (GetFlags2Data() >> 5 & 7) >= 3; }

    bool IsWeaponFullyDrawn() const noexcept { return (GetFlags2Data() >> 5 & 7) == 3; }

    bool IsBleedingOut() const noexcept { return (GetFlags1Data() & 0x1E00000) == 0x1000000 || (GetFlags1Data() & 0x1E00000) == 0xE00000; }

    bool SetWeaponDrawn(bool aDraw) noexcept;
};
