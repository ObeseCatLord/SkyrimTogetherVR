#include <Services/DebugService.h>

#include <imgui.h>
#include <inttypes.h>

#include <TimeManager.h>

void DebugService::DrawCalendarView()
{
    ImGui::Begin("Calendar");

    auto* pGameTime = TimeData::Get();

    auto year = pGameTime->GetGameYearData()->GetValueData();
    ImGui::InputFloat("Year", &year, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto month = pGameTime->GetGameMonthData()->GetValueData();
    ImGui::InputFloat("Month", &month, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto day = pGameTime->GetGameDayData()->GetValueData();
    ImGui::InputFloat("Day", &day, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto hour = pGameTime->GetGameHourData()->GetValueData();
    ImGui::InputFloat("Hour", &hour, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto passed = pGameTime->GetGameDaysPassedData()->GetValueData();
    ImGui::InputFloat("Passed", &passed, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto scale = pGameTime->GetTimeScaleData()->GetValueData();
    ImGui::InputFloat("Scale", &scale, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    ImGui::End();
}
