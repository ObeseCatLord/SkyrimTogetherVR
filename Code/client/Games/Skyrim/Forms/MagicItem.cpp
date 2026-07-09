#include "MagicItem.h"
#include <Games/TES.h>

bool MagicItem::IsWardSpell() const noexcept
{
    BGSKeyword* pMagicWard = Cast<BGSKeyword>(TESForm::GetById(0x1ea69));

    if (GetKeywordFormData().ContainsKeywordData(pMagicWard))
        return true;

    // Spells typically don't have keywords, so we must check their effects
    for (const EffectItem* pEffect : GetEffectsData())
    {
        auto* pEffectSetting = pEffect ? pEffect->GetEffectSettingData() : nullptr;
        if (pEffectSetting && pEffectSetting->GetKeywordFormData().ContainsKeywordData(pMagicWard))
            return true;
    }

    return false;
}

bool MagicItem::IsInvisibilitySpell() const noexcept
{
    // PR #796 and the "SkyPatcher Keyword Framework" mod both hoist
    // some keywords from the effects up to the spell, because some 
    // spell records are buggy; this fixes things like syncing more ward spells.
    // But the fix can break other things. This is the first one found,
    // Bow of Shadows Invisibility (formID 0xFEXXX805), which doesn't have an 
    // Invisibility keyword. Apparently deliberately, it isn't really a spell but a 
    // condition (readying the bow) that triggers an effect. If it is treated as a 
    // spell, syncing doesn't work. There may be others, if so, there will be a 
    // more general fix but this is the pattern, just needs an array / loop.
    static bool runonce = true; 
    static uint32_t bosFormID = 0;      // Can be an array if more are found.
    if (runonce)
    {
        runonce = false;  
        Mod* pBOSMod = nullptr;
        MagicItem* pSpell  = nullptr;
        if (   (pBOSMod = ModManager::Get()->GetByName("ccbgssse038-bowofshadows.esl")) 
            && (pSpell  = Cast<MagicItem>(TESForm::GetById(pBOSMod->GetFormId(0x805)))))
            bosFormID = pSpell->GetFormIdData();
    }

    if (GetFormIdData() == bosFormID)
        return false;

    BGSKeyword* pMagicInvisibility = Cast<BGSKeyword>(TESForm::GetById(0x1ea6f));

    if (GetKeywordFormData().ContainsKeywordData(pMagicInvisibility))
        return true;

    for (const EffectItem* pEffect : GetEffectsData())
    {
        auto* pEffectSetting = pEffect ? pEffect->GetEffectSettingData() : nullptr;
        if (pEffectSetting && pEffectSetting->GetKeywordFormData().ContainsKeywordData(pMagicInvisibility))
            return true;
    }
    return false;
}

bool MagicItem::IsHealingSpell() const noexcept
{
    BGSKeyword* pMagicRestoreHealth = Cast<BGSKeyword>(TESForm::GetById(0x1ceb0));
    
    if (GetKeywordFormData().ContainsKeywordData(pMagicRestoreHealth))
        return true;

    for (const EffectItem* pEffect : GetEffectsData())
    {
        auto* pEffectSetting = pEffect ? pEffect->GetEffectSettingData() : nullptr;
        if (pEffectSetting && pEffectSetting->GetKeywordFormData().ContainsKeywordData(pMagicRestoreHealth))
            return true;
    }
    return false;
}

bool MagicItem::IsBuffSpell() const noexcept
{
    switch (GetFormIdData())
    {
    case 0x4dee8: // Courage
    case 0x4deec: // Rally
    case 0x7e8dd: // Call to Arms
        return true;
    default:
        return false;
    }
}

bool MagicItem::IsBoundWeaponSpell() noexcept
{
    for (EffectItem* pEffect : GetEffectsData())
    {
        auto* pEffectSetting = pEffect ? pEffect->GetEffectSettingData() : nullptr;
        if (pEffectSetting && pEffectSetting->GetArchetypeData() == EffectArchetypes::ArchetypeID::kBoundWeapon)
            return true;
    }

    return false;
}

EffectItem* MagicItem::GetEffect(const uint32_t aEffectId) noexcept
{
    for (EffectItem* pEffect : GetEffectsData())
    {
        auto* pEffectSetting = pEffect ? pEffect->GetEffectSettingData() : nullptr;
        if (pEffectSetting && pEffectSetting->GetFormIdData() == aEffectId)
            return pEffect;
    }

    return nullptr;
}
