#include <TiltedOnlinePCH.h>

#include <SaveLoad.h>
#include <Games/Overrides.h>

#include <Forms/TESForm.h>

#include <World.h>

#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>

using TiltedPhoques::Serialization;
using TiltedPhoques::ViewBuffer;

BGSSaveFormBuffer::BGSSaveFormBuffer()
{
    TP_THIS_FUNCTION(CtorT, BGSSaveFormBuffer*, BGSSaveFormBuffer);

    POINTER_SKYRIMSE(CtorT, ctor, 36035);

    TiltedPhoques::ThisCall(ctor, this);

    SetPositionData(0);
}

void BGSSaveFormBuffer::WriteId(uint32_t aId) noexcept
{
    uint32_t modId = 0;
    uint32_t baseId = 0;

    World::Get().GetModSystem().GetServerModId(aId, modId, baseId);

    const auto position = GetPositionData();
    auto pWriteLocation = reinterpret_cast<uint8_t*>(GetBufferData() + position);

    ViewBuffer view(pWriteLocation, GetCapacityData() - position);
    Buffer::Writer writer(&view);

    Serialization::WriteVarInt(writer, modId);
    Serialization::WriteVarInt(writer, baseId);

    AdvancePositionData(writer.Size() & 0xFFFFFFFF);
}

BGSLoadFormBuffer::BGSLoadFormBuffer(const uint32_t aChangeFlags)
{
    TP_THIS_FUNCTION(CtorT, BGSLoadFormBuffer*, BGSLoadFormBuffer);

    POINTER_SKYRIMSE(CtorT, ctor, 35993);

    TiltedPhoques::ThisCall(ctor, this);

    SetChangeFlagsData(aChangeFlags);
    SetLoadVersionData(0x40);
    SetPositionData(0);
    SetOldChangeFlagsData(0);
    SetUnk1CData(-1);
}

TP_THIS_FUNCTION(TBGSLoadFormBuffer_ReadFormId, bool, BGSLoadFormBuffer, uint32_t&);
TP_THIS_FUNCTION(TBGSSaveFormBuffer_WriteFormId, void, BGSSaveFormBuffer, TESForm*);
TP_THIS_FUNCTION(TBGSSaveFormBuffer_WriteId, void, BGSSaveFormBuffer, uint64_t);

static TBGSSaveFormBuffer_WriteFormId* RealBGSSaveFormBuffer_WriteFormId = nullptr;
static TBGSLoadFormBuffer_ReadFormId* RealBGSLoadFormBuffer_ReadFormId = nullptr;
static TBGSSaveFormBuffer_WriteId* RealBGSSaveFormBuffer_WriteId = nullptr;

void TP_MAKE_THISCALL(BGSSaveFormBuffer_WriteFormId, BGSSaveFormBuffer, TESForm* apForm)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        TiltedPhoques::ThisCall(RealBGSSaveFormBuffer_WriteFormId, apThis, apForm);
        return;
    }

    apThis->WriteId(apForm ? apForm->GetFormIdData() : 0);
}

void TP_MAKE_THISCALL(BGSSaveFormBuffer_WriteId, BGSSaveFormBuffer, uint64_t aId)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        TiltedPhoques::ThisCall(RealBGSSaveFormBuffer_WriteId, apThis, aId);
        return;
    }

    apThis->WriteId(aId & 0xFFFFFFFF);
}

bool TP_MAKE_THISCALL(BGSLoadFormBuffer_LoadFormId, BGSLoadFormBuffer, uint32_t& aFormId)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        return TiltedPhoques::ThisCall(RealBGSLoadFormBuffer_ReadFormId, apThis, aFormId);
    }

    const auto position = apThis->GetPositionData();
    uint8_t* pReadLocation = (uint8_t*)(apThis->GetBufferData() + position);

    ViewBuffer buffer(pReadLocation, apThis->GetCapacityData() - position);
    ViewBuffer::Reader reader(&buffer);

    const uint32_t modId = Serialization::ReadVarInt(reader) & 0xFFFFFFFF;
    const uint32_t baseId = Serialization::ReadVarInt(reader) & 0xFFFFFFFF;

    aFormId = 0;

    if (modId != 0 || baseId != 0)
        aFormId = World::Get().GetModSystem().GetGameId(modId, baseId);

    apThis->AdvancePositionData(reader.Size() & 0xFFFFFFFF);

    return true;
}

static TiltedPhoques::Initializer s_saveLoadHooks(
    []()
    {
        POINTER_SKYRIMSE(TBGSLoadFormBuffer_ReadFormId, s_readFormId, 36000);
        POINTER_SKYRIMSE(TBGSSaveFormBuffer_WriteFormId, s_writeFormId, 36048);
        POINTER_SKYRIMSE(TBGSSaveFormBuffer_WriteId, s_writeId, 36047);

        RealBGSLoadFormBuffer_ReadFormId = s_readFormId.Get();
        RealBGSSaveFormBuffer_WriteFormId = s_writeFormId.Get();
        RealBGSSaveFormBuffer_WriteId = s_writeId.Get();

        TP_HOOK(&RealBGSLoadFormBuffer_ReadFormId, BGSLoadFormBuffer_LoadFormId);
        TP_HOOK(&RealBGSSaveFormBuffer_WriteFormId, BGSSaveFormBuffer_WriteFormId);
        TP_HOOK(&RealBGSSaveFormBuffer_WriteId, BGSSaveFormBuffer_WriteId);
    });
