#pragma once

#include <cstddef>

struct CachedString : TiltedPhoques::String
{
    static constexpr std::size_t kMaximumWireBytes = 4096;

    CachedString() = default;
    ~CachedString() = default;

    CachedString& operator=(const TiltedPhoques::String& acRhs);

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool IsDecodedValid{true};
};
