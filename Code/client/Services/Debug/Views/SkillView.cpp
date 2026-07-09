#include <Services/DebugService.h>

#include <imgui.h>

#include <PlayerCharacter.h>

void DebugService::DrawSkillView()
{
    ImGui::Begin("Skills");

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const Skills* pSkills = pPlayer ? pPlayer->GetSkillsData() : nullptr;
    if (!pSkills)
    {
        ImGui::TextUnformatted("Skill data unavailable.");
        ImGui::End();
        return;
    }

    float globalXp = pSkills->GetGlobalXpData();
    float globalLevelThreshold = pSkills->GetGlobalLevelThresholdData();
    ImGui::InputFloat("Global XP", &globalXp, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Global level threshold", &globalLevelThreshold, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    for (int i = 0; i < Skills::kTotal; i++)
    {
        const char* skillString = Skills::GetSkillString((Skills::Skill)i);
        if (!ImGui::CollapsingHeader(skillString, ImGuiTreeNodeFlags_DefaultOpen))
            continue;

        const auto skill = static_cast<Skills::Skill>(i);
        Skills::SkillData pSkill = pSkills->GetSkillData(skill);
        ImGui::InputFloat("Level", &pSkill.level, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
        ImGui::InputFloat("XP", &pSkill.xp, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
        ImGui::InputFloat("Level threshold", &pSkill.levelThreshold, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

        uint32_t legendaryLevel = pSkills->GetLegendaryLevelData(skill);
        ImGui::InputScalar("Legendary level", ImGuiDataType_U32, (void*)&legendaryLevel, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}
