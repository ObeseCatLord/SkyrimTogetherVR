#pragma once

#include <cstddef>
#include <cstdint>
#include <Structs/GameId.h>

using TiltedPhoques::Vector;

struct QuestLog
{
    static constexpr std::size_t kMaximumWireEntries = 0xFFFF;

    struct Entry
    {
        GameId Id;
        uint16_t Stage;

        bool operator==(const Entry& acRhs) const noexcept { return Id == acRhs.Id && Stage == acRhs.Stage; }

        bool operator!=(const Entry& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    Vector<Entry> Entries{};
    bool IsDecodedValid{true};

    QuestLog() = default;
    ~QuestLog() = default;

    bool operator==(const QuestLog& acRhs) const noexcept;
    bool operator!=(const QuestLog& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
