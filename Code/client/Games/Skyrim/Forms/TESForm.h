#pragma once

#include <Components/BaseFormComponent.h>
#include <Misc/BSFixedString.h>
#include <RuntimeLayout.h>
#include <cstddef>
#include <cstdint>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

enum class FormType : uint8_t
{
    Armor = 26,
    Book = 27,
    Container = 28,
    Door = 29,
    Ingredient = 30,
    Weapon = 41,
    Ammo = 42,
    Npc = 43,
    LeveledCharacter = 44,
    Alchemy = 46,
    LeveledItem = 53,
    Character = 62,
    QuestItem = 77,
    Count = 0x87
};

struct BGSSaveFormBuffer;
struct BGSLoadFormBuffer;

struct TESForm : BaseFormComponent
{
    using CommonLibFormOffsets = Skyrim::RuntimeLayout::TESFormCommonLibNgOffsets;
    using LocalFormOffsets = Skyrim::RuntimeLayout::TESFormLocalShimOffsets;

    struct ChangeFlags
    {
        uint32_t flags{};
        uint64_t unk4{};
        uint32_t unkC{};
    };

    enum FormFlags
    {
        DISABLED = 1 << 0xB,
        IGNORE_FRIENDLY_HITS = 1 << 0x14,
    };

    static TESForm* GetById(uint32_t aId);

    virtual void sub_4();
    virtual void sub_5();
    virtual void sub_6();
    virtual void sub_7();
    virtual void sub_8();
    virtual void sub_9();
    virtual bool MarkChanged(uint32_t aChangeFlag);
    virtual bool UnsetChanged(uint32_t aChangeFlag);
    virtual void sub_C();
    virtual void sub_D();
    virtual void Save(BGSSaveFormBuffer* apBuffer) const noexcept;
    virtual void Load(BGSLoadFormBuffer* apBuffer);
    virtual void sub_10();
    virtual void sub_11();
    virtual void sub_12();
    virtual void InitializeComponents(); // Called after Load
    virtual void sub_14();
    virtual FormType GetFormType();
    virtual void sub_16();
    virtual void sub_17();
    virtual void sub_18();
    virtual void sub_19();
    virtual void sub_1A();
    virtual void sub_1B();
    virtual void sub_1C();
    virtual void sub_1D();
    virtual void sub_1E();
    virtual void sub_1F();
    virtual void sub_20();
    virtual void sub_21();
    virtual void sub_22();
    virtual void sub_23();
    virtual void sub_24();
    virtual void sub_25();
    virtual void sub_26();
    virtual void sub_27();
    virtual void sub_28();
    virtual void sub_29();
    virtual void sub_2A();
    virtual void sub_2B();
    virtual void sub_2C();
    virtual void sub_2D();
    // Actual function name is `GetTextForParsedSubTag`; passing an empty tag gets the full name of this form
    virtual const char* GetName(const BSFixedString& acTag = {}) const noexcept;
    virtual void CopyFrom(TESForm* apForm);
    virtual void sub_30();
    virtual void sub_31();
    virtual const char* GetFormEditorID();
    virtual void sub_33();
    virtual void sub_34();
    virtual void sub_35();
    virtual void sub_36();
    virtual void ActivateReference();
    virtual void sub_38();
    virtual void unk_39();
    virtual void unk_3A();

    // void CopyFromEx(TESForm* rhs);
    void Save_Reversed(uint32_t aChangeFlags, Buffer::Writer& aWriter);
    void SetSkipSaveFlag(bool aSet) noexcept;
    uint32_t GetChangeFlags() const noexcept;

    [[nodiscard]] uint32_t GetFormFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibFormOffsets::Flags);
#else
        return flags;
#endif
    }

    void SetFormFlagsData(uint32_t aFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint32_t>(this, CommonLibFormOffsets::Flags, aFlags);
#else
        flags = aFlags;
#endif
    }

    [[nodiscard]] uint32_t GetFormIdData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint32_t>(this, CommonLibFormOffsets::FormID);
#else
        return formID;
#endif
    }

    [[nodiscard]] uint16_t GetInGameFormFlagsData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<uint16_t>(this, CommonLibFormOffsets::InGameFormFlags);
#else
        return unk10;
#endif
    }

    void SetInGameFormFlagsData(uint16_t aFlags) noexcept
    {
#if TP_SKYRIM_VR
        Skyrim::RuntimeLayout::Store<uint16_t>(this, CommonLibFormOffsets::InGameFormFlags, aFlags);
#else
        unk10 = aFlags;
#endif
    }

    [[nodiscard]] FormType GetFormTypeData() const noexcept
    {
#if TP_SKYRIM_VR
        return Skyrim::RuntimeLayout::Value<FormType>(this, CommonLibFormOffsets::FormType);
#else
        return formType;
#endif
    }

    bool GetIgnoreFriendlyHit() const noexcept { return (GetFormFlagsData() & IGNORE_FRIENDLY_HITS) != 0; }
    void SetIgnoreFriendlyHit(bool aSet) noexcept
    {
        auto formFlags = GetFormFlagsData();
        if (aSet)
            formFlags |= IGNORE_FRIENDLY_HITS;
        else
            formFlags &= ~IGNORE_FRIENDLY_HITS;
        SetFormFlagsData(formFlags);
    }

    bool IsDisabled() const noexcept { return (GetFormFlagsData() & DISABLED) != 0; }
    bool IsTemporary() const noexcept { return GetFormIdData() >= 0xFF000000; }
    bool IsConsumable() const noexcept { return GetFormTypeData() == FormType::Ingredient || GetFormTypeData() == FormType::Alchemy; }

    uintptr_t unk4;
    uint32_t flags;
    uint32_t formID;
    uint16_t unk10;
    FormType formType;
    uint8_t padForm;
};

static_assert(offsetof(TESForm, unk4) == TESForm::LocalFormOffsets::SourceFiles);
static_assert(offsetof(TESForm, flags) == TESForm::LocalFormOffsets::Flags);
static_assert(offsetof(TESForm, formID) == TESForm::LocalFormOffsets::FormID);
static_assert(offsetof(TESForm, unk10) == TESForm::LocalFormOffsets::InGameFormFlags);
static_assert(offsetof(TESForm, formType) == TESForm::LocalFormOffsets::FormType);
static_assert(sizeof(TESForm) == TESForm::LocalFormOffsets::Size);
