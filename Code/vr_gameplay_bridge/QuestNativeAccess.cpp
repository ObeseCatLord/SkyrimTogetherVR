#include "pch.h"

#include "QuestNativeAccess.h"

#include <RE/T/TESQuest.h>
#include <REL/Relocation.h>

#include <memory>

namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
{
bool SetStage(RE::TESQuest& a_quest, const std::uint16_t a_stage) noexcept
{
    // Skyrim's x64 __fastcall ABI passes TESQuest* in RCX and the uint16_t
    // stage in RDX.
    // REL::VariantID selects SE/AE IDs and the exact Skyrim VR RVA above.
    using SetStageFn = bool(__fastcall*)(RE::TESQuest*, std::uint16_t);
    static REL::Relocation<SetStageFn> setStage{
        REL::VariantID(kSetStageSeId, kSetStageAeId, kSetStageVrRva)};
    return setStage(std::addressof(a_quest), a_stage);
}

void SetActive(RE::TESQuest& a_quest, const bool a_active) noexcept
{
    if (a_active)
        a_quest.data.flags.set(RE::QuestFlag::kActive);
    else
        a_quest.data.flags.reset(RE::QuestFlag::kActive);
    a_quest.AddChange(RE::TESQuest::ChangeFlags::kQuestFlags);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::QuestNativeAccess
