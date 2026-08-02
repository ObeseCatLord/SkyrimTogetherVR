#include <Structs/CachedString.h>
#include <TiltedCore/Serialization.hpp>
#include "StringCache.h"

using TiltedPhoques::Serialization;

CachedString& CachedString::operator=(const TiltedPhoques::String& acRhs)
{
    String::operator=(acRhs);
    IsDecodedValid = true;
    return *this;
}

void CachedString::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto cId = StringCache::Get()[*this];

    Serialization::WriteBool(aWriter, cId.has_value());
    if (cId)
    {
        Serialization::WriteVarInt(aWriter, *cId);
    }
    else
    {
        Serialization::WriteString(aWriter, *this);

        StringCache::Get().AddWanted(*this);
    }
}

void CachedString::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    clear();
    IsDecodedValid = true;
    try
    {
        const auto cHasId = Serialization::ReadBool(aReader);
        if (cHasId)
        {
            const auto cId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
            const auto cValue = StringCache::Get()[cId];
            if (!cValue || cValue->size() > kMaximumWireBytes)
            {
                IsDecodedValid = false;
                return;
            }
            *this = *cValue;
        }
        else
        {
            auto value = Serialization::ReadString(aReader);
            if (value.size() > kMaximumWireBytes)
            {
                IsDecodedValid = false;
                return;
            }
            *this = value;
            StringCache::Get().AddWanted(*this);
        }
    }
    catch (...)
    {
        clear();
        IsDecodedValid = false;
    }
}
