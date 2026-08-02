#pragma once

#include <cstddef>
#include "Faction.h"

using TiltedPhoques::Vector;

struct Factions
{
    static constexpr std::size_t kMaximumWireEntries = 0x1FF;

    Factions() = default;
    ~Factions() = default;

    bool operator==(const Factions& acRhs) const noexcept;
    bool operator!=(const Factions& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    Vector<Faction> NpcFactions;
    Vector<Faction> ExtraFactions;
    bool IsDecodedValid{true};
};
