#include "CombatController.h"
#include "CombatTargetSelector.h"

void ArrayQuickSortRecursiveCombatTargets(GameArray<CombatTargetSelector*>* apArray, uint32_t aiLowIndex,
                                          uint32_t aiHighIndex)
{
    using TArrayQuickSort = void(GameArray<CombatTargetSelector*>* apArray, void* apFunction, uint32_t aiLowIndex, uint32_t aiHighIndex);
    POINTER_SKYRIMSE(TArrayQuickSort, arrayQuickSort, 33285);

    using TSortTargetSelectors = int64_t(int64_t, int64_t);
    POINTER_SKYRIMSE(TSortTargetSelectors, sortTargetSelectors, 33282);

    arrayQuickSort(apArray, sortTargetSelectors, aiLowIndex, aiHighIndex);
}

void CombatController::UpdateTarget()
{
    auto& targetSelectorData = GetTargetSelectorsData();

    for (auto* pTargetSelector : targetSelectorData)
    {
        if (!pTargetSelector->HasFlagsData(2))
            pTargetSelector->Update();
    }

    if (targetSelectorData.length > 1)
        ArrayQuickSortRecursiveCombatTargets(&targetSelectorData, 0, targetSelectorData.length - 1);

    SetActiveTargetSelectorData(nullptr);
    BSPointerHandle<Actor> newTarget{};

    if (targetSelectorData.length)
    {
        for (auto* pTargetSelector : targetSelectorData)
        {
            if (pTargetSelector->HasFlagsData(1) && !pTargetSelector->HasFlagsData(2))
            {
                newTarget = pTargetSelector->SelectTarget();
                if (newTarget)
                {
                    SetActiveTargetSelectorData(pTargetSelector);
                    break;
                }
            }
        }
    }

    // If CombatComponent is attached, don't try to fetch a new target.
    if (Actor* pAttacker = Cast<Actor>(TESObjectREFR::GetByHandle(GetAttackerHandleData())))
    {
        const auto view = World::Get().view<FormIdComponent, CombatComponent>();
        const auto attackerId = pAttacker->GetFormIdData();
        const auto it = std::find_if(view.begin(), view.end(), [view, attackerId](auto entity) { return view.get<FormIdComponent>(entity).Id == attackerId; });
        if (it != view.end())
            return;
    }

    if (newTarget.handle.iBits == GetTargetHandleData())
        return;

    Actor* pNewTarget = Cast<Actor>(TESObjectREFR::GetByHandle(newTarget.handle.iBits));
    SetTarget(pNewTarget);
}

TP_THIS_FUNCTION(TUpdateTarget, void, CombatController);
static TUpdateTarget* RealUpdateTarget = nullptr;

void TP_MAKE_THISCALL(HookUpdateTarget, CombatController)
{
    apThis->UpdateTarget();
}

void CombatController::SetTarget(Actor* apTarget)
{
    TP_THIS_FUNCTION(TSetTarget, void, CombatController, Actor*);
    POINTER_SKYRIMSE(TSetTarget, setTarget, 33235);
    TiltedPhoques::ThisCall(setTarget, this, apTarget);
}

static TiltedPhoques::Initializer s_combatControllerHooks(
    []()
    {
#if 0
        POINTER_SKYRIMSE(TUpdateTarget, s_updateTarget, 33236);

        RealUpdateTarget = s_updateTarget.Get();

        TP_HOOK(&RealUpdateTarget, HookUpdateTarget);
#endif
    });
