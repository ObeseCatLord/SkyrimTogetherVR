#include <TiltedOnlinePCH.h>

#include <SaveLoad.h>

void BGSSaveLoadManager::Save(SaveData* apData)
{
    if (!apData)
        return;

    apData->SetFlagsData(apData->GetFlagsData() | 4);

    const char* cSaveName = "";
    if (const auto* pSaveName = apData->GetSaveNameData())
        cSaveName = pSaveName;

    (void)cSaveName;
}
