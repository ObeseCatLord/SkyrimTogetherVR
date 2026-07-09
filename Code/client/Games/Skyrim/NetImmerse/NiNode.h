#pragma once

#include <cstddef>

#include <NetImmerse/NiAVObject.h>
#include <NetImmerse/NiPointer.h>
#include <NetImmerse/NiTObjectArray.h>

struct NiNode : NiAVObject
{
    virtual ~NiNode();

    [[nodiscard]] NiTObjectArray<NiPointer<NiAVObject>>& GetChildrenData() noexcept { return children; }
    [[nodiscard]] const NiTObjectArray<NiPointer<NiAVObject>>& GetChildrenData() const noexcept { return children; }

    NiTObjectArray<NiPointer<NiAVObject>> children;
};

#if TP_SKYRIM_VR
static_assert(offsetof(NiNode, children) == 0x138);
static_assert(sizeof(NiNode) == 0x150);
#else
static_assert(offsetof(NiNode, children) == 0x110);
static_assert(sizeof(NiNode) == 0x128);
#endif
