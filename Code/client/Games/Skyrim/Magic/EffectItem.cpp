#include "EffectItem.h"

#include <Games/Magic/MagicSystem.h>
#include <Forms/EffectSetting.h>

bool EffectItem::IsHealingEffect() const noexcept
{
    return GetEffectSettingData()->GetArchetypeData() == EffectArchetypes::ArchetypeID::kValueModifier && GetEffectItemData().fMagnitude > 0.0f;
}

bool EffectItem::IsSummonEffect() const noexcept
{
    return GetEffectSettingData()->GetArchetypeData() == EffectArchetypes::ArchetypeID::kSummonCreature;
}

bool EffectItem::IsSlowEffect() const noexcept
{
    return GetEffectSettingData()->GetArchetypeData() == EffectArchetypes::ArchetypeID::kSlowTime;
}

bool EffectItem::IsInivisibilityEffect() const noexcept
{
    return GetEffectSettingData()->GetArchetypeData() == EffectArchetypes::ArchetypeID::kInvisibility;
}

bool EffectItem::IsWerewolfEffect() const noexcept
{
    return GetEffectSettingData()->GetArchetypeData() == EffectArchetypes::ArchetypeID::kWerewolf;
}

bool EffectItem::IsVampireLordEffect() const noexcept
{
    return GetEffectSettingData()->GetArchetypeData() == EffectArchetypes::ArchetypeID::kVampireLord;
}

bool EffectItem::IsNightVisionEffect() const noexcept
{
    BGSKeyword* pMagicNightEye = Cast<BGSKeyword>(TESForm::GetById(0xad7c6));
    auto* pEffectSettingData = GetEffectSettingData();
    const auto& keywordForm = pEffectSettingData->GetKeywordFormData();
    if (keywordForm.GetKeywordCountData() == 0)
        spdlog::debug(__FUNCTION__ ": correcting BGSKeywordForm::Contains() bug for zero-keyword {:x}, {}", pEffectSettingData->GetFormIdData(),
                      pEffectSettingData->GetFullNameData().GetFullNameStringData());
    return keywordForm.ContainsKeywordData(pMagicNightEye);
}
