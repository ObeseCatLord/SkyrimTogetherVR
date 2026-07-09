#pragma once

#include <cstddef>
#include <cstdint>

#include <NetImmerse/NiObjectNET.h>

struct BSFixedString;
struct NiAlphaProperty;
struct NiUpdateData;
struct PerformOpFunc;
struct TESObjectREFR;

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

struct NiTransform
{
    float rotate[3][3];
    NiPoint3 translate;
    float scale;
};

static_assert(sizeof(NiTransform) == 0x34);

struct NiBound
{
    NiPoint3 center;
    float radius;
};

static_assert(sizeof(NiBound) == 0x10);

struct NiAVObject : NiObjectNET
{
    virtual ~NiAVObject();

    // Skyrim VR inserts one virtual before the normal NiAVObject add-on slots.
    // These ABI placeholders preserve vtable shape only. Do not call them from
    // VR code until their signatures and semantics have been validated.
#if TP_SKYRIM_VR
    virtual void Unk_VRFunc();
#endif
    virtual void PerformOp(PerformOpFunc& aFunc);
    virtual void AttachProperty(NiAlphaProperty* apProperty);
    virtual void SetMaterialNeedsUpdate(bool aNeedsUpdate);
    virtual void SetDefaultMaterialNeedsUpdateFlag(bool aFlag);

    // GetObjectByName is the only NiAVObject add-on virtual currently used by the VR
    // avatar validation path.
    virtual NiAVObject* GetObjectByName(const BSFixedString& acName);

    // CommonLibSSE-NG reference:
    // - `world` is at 0x7C for SE and VR.
    // - Skyrim VR extends the tail from 0x110 to 0x138 and moves `userData` to 0x110.
    NiAVObject* parent;        // 030
    uint32_t parentIndex;      // 038
    uint32_t unk03C;           // 03C
    void* collisionObject;     // 040
    NiTransform local;         // 048
    NiTransform world;         // 07C
    NiTransform previousWorld; // 0B0
    NiBound worldBound;        // 0E4

#if TP_SKYRIM_VR
    float unkF4;                      // 0F4
    float unkF8;                      // 0F8
    float unkFC;                      // 0FC
    float fadeAmount;                 // 100
    uint32_t lastUpdatedFrameCounter; // 104
    float unk108;                     // 108
    uint32_t flags;                   // 10C
    TESObjectREFR* userData;          // 110
    uint32_t pad118;                  // 118
    uint32_t unk11C;                  // 11C
    uint8_t unk120[8];                // 120
    uint64_t unk128;                  // 128
    uint32_t unk130;                  // 130
    uint32_t unk134;                  // 134
#else
    uint32_t flags;                   // 0F4
    TESObjectREFR* userData;          // 0F8
    float fadeAmount;                 // 100
    uint32_t lastUpdatedFrameCounter; // 104
    uint8_t unk108;                   // 108
    uint8_t flags02;                  // 109
    uint16_t unk10A;                  // 10A
    uint32_t pad10C;                  // 10C
#endif
};

#if TP_SKYRIM_VR
static_assert(offsetof(NiAVObject, world) == 0x7C);
static_assert(offsetof(NiAVObject, userData) == 0x110);
static_assert(offsetof(NiAVObject, unk11C) == 0x11C);
static_assert(offsetof(NiAVObject, unk128) == 0x128);
static_assert(sizeof(NiAVObject) == 0x138);
#else
static_assert(offsetof(NiAVObject, world) == 0x7C);
static_assert(offsetof(NiAVObject, userData) == 0xF8);
static_assert(sizeof(NiAVObject) == 0x110);
#endif
