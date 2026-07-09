#include <Forms/TESObjectCELL.h>
#include <TESObjectREFR.h>

Vector<TESObjectREFR*> TESObjectCELL::GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept
{
    Vector<TESObjectREFR*> references{};

    const auto* pReferenceData = GetReferenceData();
    if (!pReferenceData || !pReferenceData->GetReferenceArrayData())
        return references;

    for (uint32_t i = 0; i < pReferenceData->GetCapacityData(); i++)
    {
        TESObjectREFR* pRef = pReferenceData->GetReferenceAtData(i);
        if (!pRef)
            continue;

        for (FormType formType : aFormTypes)
        {
            const auto* pBaseForm = pRef->GetBaseFormData();
            if (pBaseForm && pBaseForm->GetFormTypeData() == formType)
                references.push_back(pRef);
        }
    }

    return references;
}

void TESObjectCELL::GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept
{
    TP_THIS_FUNCTION(TGetCOCPlacementInfo, void, TESObjectCELL, NiPoint3*, NiPoint3*, bool);
    POINTER_SKYRIMSE(TGetCOCPlacementInfo, s_getCOCPlacementInfo, 19075);
    TiltedPhoques::ThisCall(s_getCOCPlacementInfo, this, aOutPos, aOutRot, aAllowCellLoad);
}
