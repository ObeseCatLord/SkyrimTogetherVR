#include <Services/DebugService.h>

#include <imgui.h>
#include <inttypes.h>
#include <Games/TES.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

void DebugService::DrawPlayerDebugView()
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
    {
        return;
    }

    ImGui::Begin("Player");

    auto pLeftWeapon = pPlayer->GetEquippedWeapon(0);
    auto pRightWeapon = pPlayer->GetEquippedWeapon(1);

    uint32_t leftId = pLeftWeapon ? pLeftWeapon->GetFormIdData() : 0;
    uint32_t rightId = pRightWeapon ? pRightWeapon->GetFormIdData() : 0;

    ImGui::InputScalar("Left Item", ImGuiDataType_U32, (void*)&leftId, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("Right Item", ImGuiDataType_U32, (void*)&rightId, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);

    const auto* pLeftSpell = pPlayer->GetSelectedSpellData(0);
    const auto* pRightSpell = pPlayer->GetSelectedSpellData(1);
    leftId = pLeftSpell ? pLeftSpell->GetFormIdData() : 0;
    rightId = pRightSpell ? pRightSpell->GetFormIdData() : 0;

    ImGui::InputScalar("Right Magic", ImGuiDataType_U32, (void*)&rightId, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("Left Magic", ImGuiDataType_U32, (void*)&leftId, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);

    auto* leftHandCaster = pPlayer->GetMagicCaster(MagicSystem::CastingSource::LEFT_HAND);
    auto* rightHandCaster = pPlayer->GetMagicCaster(MagicSystem::CastingSource::RIGHT_HAND);
    auto* otherHandCaster = pPlayer->GetMagicCaster(MagicSystem::CastingSource::OTHER);
    auto* instantHandCaster = pPlayer->GetMagicCaster(MagicSystem::CastingSource::INSTANT);

    ImGui::InputScalar("leftHandCaster", ImGuiDataType_U64, (void*)&leftHandCaster, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("rightHandCaster", ImGuiDataType_U64, (void*)&rightHandCaster, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("otherHandCaster", ImGuiDataType_U64, (void*)&otherHandCaster, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("instantHandCaster", ImGuiDataType_U64, (void*)&instantHandCaster, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);

    uint64_t leftHandCasterSpell = reinterpret_cast<uint64_t>(leftHandCaster ? leftHandCaster->GetCurrentSpellData() : nullptr);
    uint64_t rightHandCasterSpell = reinterpret_cast<uint64_t>(rightHandCaster ? rightHandCaster->GetCurrentSpellData() : nullptr);
    uint64_t otherHandCasterSpell = reinterpret_cast<uint64_t>(otherHandCaster ? otherHandCaster->GetCurrentSpellData() : nullptr);
    uint64_t instantHandCasterSpell = reinterpret_cast<uint64_t>(instantHandCaster ? instantHandCaster->GetCurrentSpellData() : nullptr);

    ImGui::InputScalar("leftHandCasterSpell", ImGuiDataType_U64, (void*)&leftHandCasterSpell, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("rightHandCasterSpell", ImGuiDataType_U64, (void*)&rightHandCasterSpell, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("otherHandCasterSpell", ImGuiDataType_U64, (void*)&otherHandCasterSpell, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    ImGui::InputScalar("instantHandCasterSpell", ImGuiDataType_U64, (void*)&instantHandCasterSpell, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);

    const auto* pSelectedPower = pPlayer->GetSelectedPowerOrShoutData();
    uint32_t shoutId = pSelectedPower ? pSelectedPower->GetFormIdData() : 0;

    ImGui::InputScalar("Shout", ImGuiDataType_U32, (void*)&shoutId, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);

    if (auto pWorldSpace = pPlayer->GetWorldSpace())
    {
        auto worldFormId = pWorldSpace->GetFormIdData();
        ImGui::InputScalar("Worldspace", ImGuiDataType_U32, (void*)&worldFormId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
    }
    if (auto pCell = pPlayer->GetParentCell())
    {
        auto cellFormId = pCell->GetFormIdData();
        ImGui::InputScalar("Cell Id", ImGuiDataType_U32, (void*)&cellFormId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
    }
    if (const auto playerParentCell = pPlayer->GetParentCellData())
    {
        auto playerParentCellId = playerParentCell->GetFormIdData();
        ImGui::InputScalar(
            "Player parent cell", ImGuiDataType_U32, (void*)&playerParentCellId, nullptr, nullptr, "%" PRIx32,
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
    }
    if (auto* pTES = TES::Get())
    {
        int32_t playerGrid[2] = {pTES->GetCurrentGridXData(), pTES->GetCurrentGridYData()};
        int32_t centerGrid[2] = {pTES->GetCenterGridXData(), pTES->GetCenterGridYData()};

        ImGui::InputInt2("Player grid", playerGrid, ImGuiInputTextFlags_ReadOnly);
        ImGui::InputInt2("Center grid", centerGrid, ImGuiInputTextFlags_ReadOnly);
    }

    auto& modSystem = m_world.GetModSystem();

    if (ImGui::CollapsingHeader("Worn armor"))
    {
        Inventory wornArmor{};
        wornArmor = pPlayer->GetWornArmor();
        for (const auto& armor : wornArmor.Entries)
        {
            const uint32_t armorId = armor.BaseId.BaseId;
            ImGui::InputScalar("Item id", ImGuiDataType_U32, (void*)&armorId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
        }
    }

    ImGui::End();
}
