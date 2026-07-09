#include <imgui.h>
#include <inttypes.h>
#include <Games/TES.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

#include <Services/DebugService.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Effects/ActiveEffect.h>

void DebugService::DrawFormDebugView()
{
    static TESForm* pFetchForm = nullptr;
    static Actor* pRefr = nullptr;

    ImGui::Begin("Form");

    ImGui::InputScalar("Form ID", ImGuiDataType_U32, &m_formId, 0, 0, "%" PRIx32, ImGuiInputTextFlags_CharsHexadecimal);

    if (ImGui::Button("Look up"))
    {
        if (m_formId)
        {
            pFetchForm = TESForm::GetById(m_formId);
            if (pFetchForm)
                pRefr = Cast<Actor>(pFetchForm);
        }
    }

    if (pFetchForm)
    {
        ImGui::InputScalar("Memory address", ImGuiDataType_U64, (void*)&pFetchForm, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    }

    if (pRefr)
    {
        if (auto* pParentCell = pRefr->GetParentCell())
        {
            const uint32_t cellId = pParentCell->GetFormIdData();
            ImGui::InputScalar("GetParentCell", ImGuiDataType_U32, (void*)&cellId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
        }

        if (auto* pParentCell = pRefr->GetParentCellData())
        {
            const uint32_t cellId = pParentCell->GetFormIdData();
            ImGui::InputScalar("GetParentCellData", ImGuiDataType_U32, (void*)&cellId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
        }

        /*
        char name[256];
        sprintf_s(name, std::size(name), "%s (%x)", pRefr->GetBaseFormData()->GetName(), pRefr->GetFormIdData());
        ImGui::InputText("Name", name, std::size(name), ImGuiInputTextFlags_ReadOnly);

        auto* pCurrentProcess = pRefr->GetCurrentProcessData();
        auto* pMiddleProcess = pCurrentProcess ? pCurrentProcess->GetMiddleProcessData() : nullptr;
        auto* pActiveEffects = pMiddleProcess ? pMiddleProcess->GetActiveEffectsData() : nullptr;
        if (!pActiveEffects)
            return;

        for (ActiveEffect* pEffect : *pActiveEffects)
        {
            if (!pEffect)
                continue;

            auto* pSpell = pEffect->GetSpellData();
            if (!pSpell)
                continue;

            if (!ImGui::CollapsingHeader(pSpell->GetFullNameData().GetFullNameStringData(), ImGuiTreeNodeFlags_DefaultOpen))
                continue;

            float elapsedSeconds = pEffect->GetElapsedSecondsData();
            float duration = pEffect->GetDurationData();
            float magnitude = pEffect->GetMagnitudeData();
            int flags = static_cast<int>(pEffect->GetFlagsData());
            ImGui::InputFloat("Elapsed seconds", &elapsedSeconds, 0, 0, "%.1f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat("Duration", &duration, 0, 0, "%.1f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat("Magnitude", &magnitude, 0, 0, "%.1f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputInt("Flags", &flags, 0, 0, ImGuiInputTextFlags_ReadOnly);

            if (ImGui::Button("Elapse time"))
                m_world.GetRunner().Queue([pEffect]() { pEffect->SetElapsedSecondsData(pEffect->GetDurationData() - 3.f); });
        }
        */
    }

    ImGui::End();
}
