#pragma once

#include <array>
#include <string_view>

#include <vr_common/VRAnimationGraphProtocol.h>

namespace SkyrimTogetherVR::GameplayAdapter::HumanoidAnimationGraph
{
inline constexpr std::array<std::string_view, AnimationGraphProtocol::kBooleanCount> kBooleanNames{
    "bEquipOk", "bMotionDriven", "IsBeastRace", "IsSneaking", "IsBleedingOut", "IsCastingDual",
    "Is1HM", "IsCastingRight", "IsCastingLeft", "IsBlockHit", "IsPlayer", "IsNPC", "bIsSynced",
    "bVoiceReady", "bWantCastLeft", "bWantCastRight", "bWantCastVoice", "b1HM_MLh_attack", "b1HMCombat",
    "bAnimationDriven", "bCastReady", "IsAttacking", "bAllowRotation", "bMagicDraw", "bMLh_Ready",
    "bMRh_Ready", "bInMoveState", "bSprintOK", "bIdlePlaying", "bIsDialogueExpressive", "bAnimObjectLoaded",
    "bEquipUnequip", "bAttached", "bIsH2HSolo", "bHeadTracking", "bIsRiding", "bTalkable",
    "bRitualSpellActive", "bInJumpState", "bHeadTrackSpine", "bLeftHandAttack", "bIsInMT",
    "bHumanoidFootIKEnable", "bHumanoidFootIKDisable", "bStaggerPlayerOverride", "bNoStagger",
    "bIsStaffLeftCasting", "bPerkShieldCharge", "bPerkQuickShot", "IsBlocking", "IsBashing", "IsStaggering",
    "IsRecoiling", "IsEquipping", "IsUnequipping", "isInFurniture", "bNeutralState", "bBowDrawn",
    "PitchOverride", "NotCasting"};

inline constexpr std::array<std::string_view, AnimationGraphProtocol::kFloatCount> kFloatNames{
    "TurnDelta", "Direction", "SpeedSampled", "weapAdj", "Speed", "CastBlend", "PitchOffset",
    "SpeedDamped", "Pitch", "VelocityZ", "1stPRot", "1stPRotDamped", "CastBlendDamped"};

inline constexpr std::array<std::string_view, AnimationGraphProtocol::kIntegerCount> kIntegerNames{
    "iRightHandEquipped", "iLeftHandEquipped", "i1HMState", "iState", "iLeftHandType", "iRightHandType",
    "iSyncIdleLocomotion", "iSyncForwardState", "iSyncTurnState", "iIsInSneak", "iWantBlock",
    "iRegularAttack", "testint", "currentDefaultState"};
} // namespace SkyrimTogetherVR::GameplayAdapter::HumanoidAnimationGraph
