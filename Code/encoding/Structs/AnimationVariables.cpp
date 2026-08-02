#include <Structs/AnimationVariables.h>
#include <TiltedCore/Serialization.hpp>
#include <cmath>
#include <iostream>

bool AnimationVariables::operator==(const AnimationVariables& acRhs) const noexcept
{
    return Booleans == acRhs.Booleans && Integers == acRhs.Integers && Floats == acRhs.Floats;
}

bool AnimationVariables::operator!=(const AnimationVariables& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

// std::vector<bool> implementation is unspecified, but often packed reasonably.
// The spec does not guarantee contiguous memory, though, so somewhat laborious 
// translation needed. Should be better than winding down several layers to 
// TiltedPhoques::Serialization::WriteBool, though.
//
void AnimationVariables::VectorBool_to_String(const Vector<bool>& bools, TiltedPhoques::String& chars) const
{
    chars.assign((bools.size() + 7) >> 3, 0);

    auto citer = chars.begin();
    auto biter = bools.begin();
    for (uint32_t mask = 1; biter < bools.end(); mask = 1, citer++)
        for (; mask < 0x100 && biter < bools.end(); mask <<= 1)
            *citer |= *biter++ ? mask : 0;
}

// The Vector<bool> must be the correct size when called.
//
void AnimationVariables::String_to_VectorBool(const TiltedPhoques::String& chars, Vector<bool>& bools)
{
    bools.assign(bools.size(), false);

    auto citer = chars.begin();
    auto biter = bools.begin();
    for (uint32_t mask = 1; biter < bools.end(); mask = 1, citer++)
        for (; mask < 0x100 && biter < bools.end(); mask <<= 1)
            *biter++ = (*citer & mask) ? true : false;
}


void AnimationVariables::Load(std::istream& aInput)
{
    // Booleans are bitpacked and a bit different, not guaranteed contiguous.
    TiltedPhoques::String chars((Booleans.size() + 7) >> 3, 0);

    aInput.read(reinterpret_cast<char*>(chars.data()), chars.size());
    String_to_VectorBool(chars, Booleans);
    aInput.read(reinterpret_cast<char*>(Integers.data()), Integers.size() * sizeof(uint32_t));
    aInput.read(reinterpret_cast<char*>(Floats.data()), Floats.size() * sizeof(float));
}

void AnimationVariables::Save(std::ostream& aOutput) const
{
    // Booleans bitpacked and not guaranteed contiguous.
    TiltedPhoques::String chars;
    VectorBool_to_String(Booleans, chars);
 
    aOutput.write(reinterpret_cast<const char*>(chars.data()), chars.size());
    aOutput.write(reinterpret_cast<const char*>(Integers.data()), Integers.size() * sizeof(uint32_t));
    aOutput.write(reinterpret_cast<const char*>(Floats.data()), Floats.size() * sizeof(float));
}

// Wire format description.
//
// Sends 3 VarInts, the count of Booleans, Integers and Floats, in that order. Then sends a bitstream of the
// sum of those counts. For the Booleans, these represent the bit values for the Booleans. For the Integers and
// Floats, it represents a truth table for whether the value has changed. If values HAVE changed, they follow on 
// the stream.
//
//
void AnimationVariables::GenerateDiff(const AnimationVariables& aPrevious, TiltedPhoques::Buffer::Writer& aWriter) const
{
    const size_t sizeChangedVector = Booleans.size() + Integers.size() + Floats.size();
    auto changedVector = Booleans;
    changedVector.reserve(sizeChangedVector);

    for (size_t i = 0; i < Integers.size(); i++)
        changedVector.push_back(aPrevious.Integers.size() != Integers.size() || aPrevious.Integers[i] != Integers[i]);
    for (size_t i = 0; i < Floats.size(); i++)
        changedVector.push_back(aPrevious.Floats.size() != Floats.size() || aPrevious.Floats[i] != Floats[i]);

    // Now serialize: VarInts Booleans.size(), Integers.size(), Floats.size(),
    // then the change table bits, then changed Integers, then changed Floats.
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Booleans.size());
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Integers.size());
    TiltedPhoques::Serialization::WriteVarInt(aWriter, Floats.size());

    TiltedPhoques::String chars;
    VectorBool_to_String(changedVector, chars);
    TiltedPhoques::Serialization::WriteString(aWriter, chars);

    auto biter = changedVector.begin() + Booleans.size();
    for (size_t i = 0; i < Integers.size(); i++)
        if (*biter++)
            TiltedPhoques::Serialization::WriteVarInt(aWriter, Integers[i] & 0xFFFFFFFF);
    for (size_t i = 0; i < Floats.size(); i++)
        if (*biter++)
            TiltedPhoques::Serialization::WriteFloat(aWriter, Floats[i]);
}

// Reads 3 VarInts that represent the size of the Booleans, Integers and Floats.
// That's followed by a bitstream in a string of the Booleans values combined
// with a Changed? truth table for Integers and Floats.
// The Changed? table is scanned and for each true bit, the corresponsing Integer
// or Float is deserialized.
// 
bool AnimationVariables::ApplyDiff(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    try
    {
        const auto booleansSize = TiltedPhoques::Serialization::ReadVarInt(aReader);
        const auto integersSize = TiltedPhoques::Serialization::ReadVarInt(aReader);
        const auto floatsSize = TiltedPhoques::Serialization::ReadVarInt(aReader);
        if (booleansSize > kMaximumBooleanCount || integersSize > kMaximumIntegerCount ||
            floatsSize > kMaximumFloatCount || booleansSize > kMaximumTotalCount - integersSize ||
            booleansSize + integersSize > kMaximumTotalCount - floatsSize)
        {
            IsDecodedValid = false;
            return false;
        }

        const auto totalSize = static_cast<std::size_t>(booleansSize + integersSize + floatsSize);
        auto integers = Integers;
        auto floats = Floats;
        if (integers.size() != integersSize)
            integers.assign(static_cast<std::size_t>(integersSize), 0);
        if (floats.size() != floatsSize)
            floats.assign(static_cast<std::size_t>(floatsSize), 0.f);

        TiltedPhoques::Vector<bool> changedVector(totalSize);
        const auto chars = TiltedPhoques::Serialization::ReadString(aReader);
        if (chars.size() != (totalSize + 7) / 8)
        {
            IsDecodedValid = false;
            return false;
        }
        String_to_VectorBool(chars, changedVector);

        TiltedPhoques::Vector<bool> booleans(
            changedVector.begin(), changedVector.begin() + static_cast<std::ptrdiff_t>(booleansSize));
        auto biter = changedVector.begin() + static_cast<std::ptrdiff_t>(booleansSize);
        for (std::size_t i = 0; i < integersSize; i++)
            if (*biter++)
                integers[i] = TiltedPhoques::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        for (std::size_t i = 0; i < floatsSize; i++)
        {
            if (!*biter++)
                continue;
            const auto value = TiltedPhoques::Serialization::ReadFloat(aReader);
            if (!std::isfinite(value))
            {
                IsDecodedValid = false;
                return false;
            }
            floats[i] = value;
        }

        Booleans = std::move(booleans);
        Integers = std::move(integers);
        Floats = std::move(floats);
        IsDecodedValid = true;
        return true;
    }
    catch (...)
    {
        IsDecodedValid = false;
        return false;
    }
}
