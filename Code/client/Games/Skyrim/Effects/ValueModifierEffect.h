#pragma once

#include <Effects/ActiveEffect.h>
#include <cstddef>

struct Actor;

struct ValueModifierEffect : public ActiveEffect
{
    using CommonLibValueModifierEffectOffsets = Skyrim::RuntimeLayout::ValueModifierEffectCommonLibNgOffsets;
    using LocalValueModifierEffectOffsets = Skyrim::RuntimeLayout::ValueModifierEffectLocalShimOffsets;

    virtual void sub_19();
    virtual void sub_1A();
    virtual void sub_1B();
    virtual void sub_1C();
    virtual void sub_1D();
    virtual void sub_1E();
    virtual void sub_1F();
    virtual void ApplyActorEffect(Actor* actor, float effectValue, unsigned int unk1);

    [[nodiscard]] uint32_t GetActorValueIndexData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibValueModifierEffectOffsets::ActorValue);
#else
        return actorValueIndex;
#endif
    }

    [[nodiscard]] float GetValueData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<float>(this, CommonLibValueModifierEffectOffsets::Value);
#else
        return value;
#endif
    }

    void SetValueData(float aValue) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<float>(this, CommonLibValueModifierEffectOffsets::Value, aValue);
#else
        value = aValue;
#endif
    }

    uint32_t actorValueIndex;
    float value;
};
static_assert(ValueModifierEffect::CommonLibValueModifierEffectOffsets::ActorValue == 0x90);
static_assert(ValueModifierEffect::CommonLibValueModifierEffectOffsets::Value == 0x94);
static_assert(ValueModifierEffect::CommonLibValueModifierEffectOffsets::Size == 0x98);
static_assert(offsetof(ValueModifierEffect, actorValueIndex) == ValueModifierEffect::LocalValueModifierEffectOffsets::ActorValue);
static_assert(offsetof(ValueModifierEffect, value) == ValueModifierEffect::LocalValueModifierEffectOffsets::Value);
static_assert(sizeof(ValueModifierEffect) == ValueModifierEffect::LocalValueModifierEffectOffsets::Size);
