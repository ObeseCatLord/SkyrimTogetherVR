#include <TiltedOnlinePCH.h>

#include <Games/TES.h>

INISettingCollection* INISettingCollection::Get() noexcept
{
#if TP_SKYRIM_VR
    POINTER_SKYRIMSE(INISettingCollection*, settingCollection, 524557);
#else
    POINTER_SKYRIMSE(INISettingCollection*, settingCollection, 411155);
#endif

    return *settingCollection.Get();
}

Setting* INISettingCollection::GetSetting(const char* acpName) noexcept
{
    Entry* pCurrent = &head;

    while (pCurrent)
    {
        if (_stricmp(acpName, pCurrent->setting->name) == 0)
            return pCurrent->setting;

        pCurrent = pCurrent->next;
    }

    return nullptr;
}
