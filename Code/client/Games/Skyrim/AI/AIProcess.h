#pragma once

#include <RuntimeLayout.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct TESForm;
struct MiddleProcess;
struct TESAmmo;

struct AIProcess
{
    using CommonLibAIProcessOffsets = Skyrim::RuntimeLayout::AIProcessCommonLibNgOffsets;
    using LocalAIProcessOffsets = Skyrim::RuntimeLayout::AIProcessLocalShimOffsets;

    bool SetCurrentAmmo(TESAmmo* apAmmo) noexcept;

    void KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude);

    [[nodiscard]] MiddleProcess* GetMiddleProcessData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<MiddleProcess*>(this, CommonLibAIProcessOffsets::MiddleProcess);
#else
        return middleProcess;
#endif
    }

    void* unk0;
    MiddleProcess* middleProcess;
    void* unk8;
    void* packageLock;
    struct TESPackage* package;
    uint32_t unk10[7];
    uint32_t unk34[8];
    float unk54;
    uint32_t unk58[4];
    TESForm* equippedObject[2];

    uint8_t pad88[0x137 - 0x88];

    int8_t movementType;
};

struct HighProcessData
{
    uint8_t pad0[0x218];
    char* strVoiceSubtitle;
    GameArray<std::tuple<uint32_t, void*>> KnowledgeArray; // BSTuple, std::tuple is prolly wrong
};

static_assert(offsetof(AIProcess, movementType) == 0x137);
static_assert(offsetof(AIProcess, middleProcess) == AIProcess::LocalAIProcessOffsets::MiddleProcess);
