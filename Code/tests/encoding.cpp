#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <array>
#include <bit>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "StringCache.h"
#include "Messages/StringCacheUpdate.h"

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/Vector2_NetQuantize.h>
#include <Structs/VRActivationEvent.h>
#include <Structs/VRAppearance.h>
#include <Structs/VRCombatHitEvent.h>
#include <Structs/VREquipmentUpdate.h>
#include <Structs/VRGrabEvent.h>
#include <Structs/VRHiggsState.h>
#include <Structs/VRInteractionValidation.h>
#include <Structs/VRMagicEffectEvent.h>
#include <Structs/Movement.h>
#include <Structs/VRMovementUpdate.h>
#include <Structs/VRPoseUpdate.h>
#include <Structs/VRProjectileEvent.h>

#include "../vr_common/VRGameplayBridge.h"
#include "../vr_common/VRHandoffPath.h"

#include <TiltedCore/Math.hpp>
#include <TiltedCore/Platform.hpp>

using namespace TiltedPhoques;

namespace
{
VRPoseNodeData BuildPoseNode(float aBase)
{
    VRPoseNodeData node{};
    node.Valid = true;
    node.Position.x = aBase;
    node.Position.y = aBase + 10.0f;
    node.Position.z = -aBase;
    node.AxisX = {1.0f, 0.0f, 0.0f};
    node.AxisY = {0.0f, 1.0f, 0.0f};
    node.AxisZ = {0.0f, 0.0f, 1.0f};
    node.Scale = 1.0f;
    return node;
}

VRPoseNodeData BuildBodyRotationNode(float aBase)
{
    auto node = BuildPoseNode(aBase);
    node.Position = {};
    return node;
}

VRPoseUpdate BuildPoseUpdate()
{
    VRPoseUpdate pose{};
    pose.Sequence = 42;
    pose.Hmd = BuildPoseNode(100.0f);
    pose.LeftHand = BuildPoseNode(110.0f);
    pose.RightHand = BuildPoseNode(120.0f);
    pose.SpellOrigin = BuildPoseNode(130.0f);
    pose.SpellDestination = BuildPoseNode(140.0f);
    pose.ArrowOrigin = BuildPoseNode(150.0f);
    pose.ArrowDestination = BuildPoseNode(160.0f);
    pose.BowAim = BuildPoseNode(170.0f);
    pose.BowRotation = BuildPoseNode(180.0f);
    pose.LeftWeaponOffset = BuildPoseNode(190.0f);
    pose.RightWeaponOffset = BuildPoseNode(200.0f);
    pose.PrimaryMagicOffset = BuildPoseNode(210.0f);
    pose.PrimaryMagicAim = BuildPoseNode(220.0f);
    pose.SecondaryMagicOffset = BuildPoseNode(230.0f);
    pose.SecondaryMagicAim = BuildPoseNode(240.0f);
    pose.Body.FormatVersion = 3;
    pose.Body.Valid = true;
    pose.Body.CaptureSequence = 1;
    pose.Body.RootGeneration = 1;
    pose.Body.Pelvis = BuildPoseNode(250.0f);
    pose.Body.Spine0 = BuildBodyRotationNode(255.0f);
    pose.Body.Spine1 = BuildBodyRotationNode(260.0f);
    pose.Body.Spine2 = BuildBodyRotationNode(265.0f);
    pose.Body.Neck = BuildBodyRotationNode(270.0f);
    pose.Body.LeftClavicle = BuildBodyRotationNode(275.0f);
    pose.Body.LeftUpperArm = BuildBodyRotationNode(280.0f);
    pose.Body.LeftForearm = BuildBodyRotationNode(285.0f);
    pose.Body.RightClavicle = BuildBodyRotationNode(290.0f);
    pose.Body.RightUpperArm = BuildBodyRotationNode(295.0f);
    pose.Body.RightForearm = BuildBodyRotationNode(300.0f);
    pose.Body.LeftThigh = BuildBodyRotationNode(305.0f);
    pose.Body.LeftCalf = BuildBodyRotationNode(310.0f);
    pose.Body.LeftFoot = BuildBodyRotationNode(315.0f);
    pose.Body.RightThigh = BuildBodyRotationNode(320.0f);
    pose.Body.RightCalf = BuildBodyRotationNode(325.0f);
    pose.Body.RightFoot = BuildBodyRotationNode(330.0f);
    pose.Body.Joints.FormatVersion = 1;
    pose.Body.Joints.Valid = true;
    pose.Body.Joints.CaptureSequence = 1;
    pose.Body.Joints.RootGeneration = 1;
    pose.Body.Joints.NodeMask = (1u << 0) | (1u << 29);
    pose.Body.Joints.Rotations[0] = {};
    pose.Body.Joints.Rotations[29] = {};
    pose.Vrik.Detected = true;
    pose.Vrik.InterfaceAvailable = true;
    pose.Vrik.LeftFingers.Valid = true;
    pose.Vrik.LeftFingers.Thumb = 0.1f;
    pose.Vrik.LeftFingers.Index = 0.2f;
    pose.Vrik.LeftFingers.Middle = 0.3f;
    pose.Vrik.LeftFingers.Ring = 0.4f;
    pose.Vrik.LeftFingers.Pinky = 0.5f;
    pose.Vrik.RightFingers.Valid = true;
    pose.Vrik.RightFingers.Thumb = 0.6f;
    pose.Vrik.RightFingers.Index = 0.7f;
    pose.Vrik.RightFingers.Middle = 0.8f;
    pose.Vrik.RightFingers.Ring = 0.9f;
    pose.Vrik.RightFingers.Pinky = 1.0f;
    pose.Vrik.CameraOffsetsValid = true;
    pose.Vrik.CameraOffset = {1.0f, 2.0f, 3.0f};
    pose.Vrik.FinalCameraOffset = {4.0f, 5.0f, 6.0f};
    pose.Vrik.FinalSmoothingOffset = {7.0f, 8.0f, 9.0f};
    return pose;
}

VRPoseUpdate BuildPoseUpdateBodyV0()
{
    auto pose = BuildPoseUpdate();
    pose.Body = {};
    pose.Body.FormatVersion = 0;
    return pose;
}

VRPoseUpdate BuildPoseUpdateBodyV1Invalid()
{
    auto pose = BuildPoseUpdate();
    pose.Body = {};
    pose.Body.FormatVersion = 1;
    return pose;
}

VRPoseUpdate BuildPoseUpdateBodyV1()
{
    auto pose = BuildPoseUpdate();
    pose.Body.FormatVersion = 1;
    pose.Body.Spine0 = {};
    pose.Body.Spine1 = {};
    pose.Body.Spine2 = {};
    pose.Body.Neck = {};
    pose.Body.LeftClavicle = {};
    pose.Body.LeftUpperArm = {};
    pose.Body.LeftForearm = {};
    pose.Body.RightClavicle = {};
    pose.Body.RightUpperArm = {};
    pose.Body.RightForearm = {};
    pose.Body.Joints = {};
    return pose;
}

VRPoseUpdate BuildPoseUpdateBodyV2()
{
    auto pose = BuildPoseUpdate();
    pose.Body.FormatVersion = 2;
    pose.Body.Spine0 = {};
    pose.Body.Spine1 = {};
    pose.Body.Spine2 = {};
    pose.Body.Neck = {};
    pose.Body.LeftClavicle = {};
    pose.Body.LeftUpperArm = {};
    pose.Body.LeftForearm = {};
    pose.Body.RightClavicle = {};
    pose.Body.RightUpperArm = {};
    pose.Body.RightForearm = {};
    return pose;
}

GameId BuildGameId(uint32_t aModId, uint32_t aBaseId)
{
    GameId result{};
    result.ModId = aModId;
    result.BaseId = aBaseId;
    return result;
}

VRAppearance BuildMinimalVRAppearance()
{
    VRAppearance appearance{};
    appearance.Sequence = 1;
    appearance.RaceId = BuildGameId(1, 0x13746);
    appearance.Sex = 0;
    appearance.Weight = 50.0F;
    appearance.Level = 1;
    constexpr std::string_view name{"NPC"};
    appearance.NameLength = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), appearance.Name.begin());
    return appearance;
}

void SetTintTexturePath(VRAppearanceTint& aTint, const std::string_view acPath)
{
    aTint.TexturePath = {};
    aTint.TexturePathLength = static_cast<std::uint8_t>(acPath.size());
    std::copy(acPath.begin(), acPath.end(), aTint.TexturePath.begin());
}

void WriteVRAppearancePrefix(TiltedPhoques::Buffer::Writer& aWriter)
{
    aWriter.WriteBits(VRAppearance::kSchemaVersion, 8);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, 1);
    BuildGameId(1, 1).Serialize(aWriter);
    aWriter.WriteBits(0, 8);
    aWriter.WriteBits(0, 32);
    aWriter.WriteBits(1, 16);
    aWriter.WriteBits(0, 1);
    const GameId empty{};
    empty.Serialize(aWriter);
    empty.Serialize(aWriter);
    aWriter.WriteBits(0, 1);
    for (std::uint8_t index = 0; index < VRAppearance::kFaceMorphCount; ++index)
        aWriter.WriteBits(0, 32);
    for (std::uint8_t index = 0; index < VRAppearance::kFacePartCount; ++index)
        aWriter.WriteBits(0, 32);
}

VREquipmentUpdate BuildEquipmentUpdate()
{
    VREquipmentUpdate equipment{};
    equipment.Sequence = 11;
    equipment.WeaponDrawn = true;
    equipment.WeaponFullyDrawn = false;
    equipment.LeftWeapon = BuildGameId(1, 0x1234);
    equipment.RightWeapon = BuildGameId(2, 0x5678);
    equipment.Ammo = BuildGameId(3, 0x9ABC);
    equipment.LeftSpell = BuildGameId(4, 0x1111);
    equipment.RightSpell = BuildGameId(5, 0x2222);
    equipment.PowerOrShout = BuildGameId(6, 0x3333);
    return equipment;
}

VRMovementUpdate BuildMovementUpdate()
{
    VRMovementUpdate movement{};
    movement.Sequence = 23;
    movement.CellId = BuildGameId(7, 0x4444);
    movement.WorldSpaceId = BuildGameId(8, 0x5555);
    movement.Position = glm::vec3(1000.0f, -2000.0f, 300.0f);
    movement.Rotation.x = 12.5f;
    movement.Rotation.y = 175.0f;
    movement.Direction = 175.0f;
    return movement;
}

VRActivationEvent BuildActivationEvent()
{
    VRActivationEvent activation{};
    activation.Sequence = 31;
    activation.ObjectId = BuildGameId(9, 0xAAAA);
    activation.CellId = BuildGameId(10, 0xBBBB);
    activation.WorldSpaceId = BuildGameId(11, 0xCCCC);
    activation.Position = glm::vec3(-32.0f, 64.0f, 128.0f);
    activation.FormType = 29;
    activation.OpenState = 3;
    return activation;
}

VRMagicEffectEvent BuildMagicEffectEvent()
{
    VRMagicEffectEvent magic{};
    magic.Sequence = 37;
    magic.EffectId = BuildGameId(12, 0xDDDD);
    magic.CasterId = BuildGameId(13, 0xEEEE);
    magic.TargetId = BuildGameId(14, 0xFFFF);
    magic.CasterPosition = glm::vec3(10.0f, 20.0f, 30.0f);
    magic.TargetPosition = glm::vec3(-10.0f, -20.0f, -30.0f);
    magic.CasterFormType = 62;
    magic.TargetFormType = 62;
    magic.CasterIsPlayer = true;
    magic.TargetIsPlayer = false;
    return magic;
}

VRCombatHitEvent BuildCombatHitEvent()
{
    VRCombatHitEvent hit{};
    hit.Sequence = 43;
    hit.HitterId = BuildGameId(15, 0x1111);
    hit.HitteeId = BuildGameId(16, 0x2222);
    hit.SourceId = BuildGameId(17, 0x3333);
    hit.ProjectileId = BuildGameId(18, 0x4444);
    hit.HitterPosition = glm::vec3(200.0f, 300.0f, 400.0f);
    hit.HitteePosition = glm::vec3(-200.0f, -300.0f, -400.0f);
    hit.RawHitFlags = 0x59914001;
    hit.HitterFormType = 62;
    hit.HitteeFormType = 62;
    hit.PlanckHit = true;
    hit.HitterIsPlayer = true;
    hit.HitteeIsPlayer = false;
    return hit;
}

VRProjectileEvent BuildProjectileEvent()
{
    VRProjectileEvent projectile{};
    projectile.Sequence = 47;
    projectile.EventKind = VRProjectileEvent::Kind::kBowShot;
    projectile.SourceId = BuildGameId(17, 0x3333);
    projectile.WeaponId = BuildGameId(18, 0x4444);
    projectile.AmmoId = BuildGameId(19, 0x5555);
    projectile.SpellId = BuildGameId(20, 0x6666);
    projectile.Origin = glm::vec3(12.0f, 24.0f, 36.0f);
    projectile.Destination = glm::vec3(-12.0f, -24.0f, -36.0f);
    projectile.Power = 0.75f;
    projectile.OriginValid = true;
    projectile.DestinationValid = true;
    projectile.IsSunGazing = false;
    return projectile;
}

VRGrabEvent BuildGrabEvent()
{
    VRGrabEvent grab{};
    grab.Sequence = 53;
    grab.ObjectId = BuildGameId(21, 0x7777);
    grab.CellId = BuildGameId(22, 0x8888);
    grab.WorldSpaceId = BuildGameId(23, 0x9999);
    grab.Position = glm::vec3(512.0f, -256.0f, 128.0f);
    grab.FormType = 29;
    grab.Grabbed = true;
    return grab;
}

VRHiggsState BuildHiggsState()
{
    VRHiggsState state{};
    state.Sequence = 59;
    state.BridgeLoaded = true;
    state.Detected = true;
    state.InterfaceAvailable = true;
    state.CallbacksRegistered = true;
    state.SnapshotAvailable = true;
    state.SnapshotSequence = 3;
    state.TwoHanding = false;

    state.Left.Valid = true;
    state.Left.HoldingObject = true;
    state.Left.CanGrabObject = false;
    state.Left.HandInGrabbableState = true;
    state.Left.Disabled = false;
    state.Left.WeaponCollisionDisabled = false;
    state.Left.GrabbedObject = BuildGameId(24, 0xAAAA);
    state.Left.Fingers.Valid = true;
    state.Left.Fingers.Thumb = 0.1f;
    state.Left.Fingers.Index = 0.2f;
    state.Left.Fingers.Middle = 0.3f;
    state.Left.Fingers.Ring = 0.4f;
    state.Left.Fingers.Pinky = 0.5f;
    state.Left.GrabTransform.Valid = true;
    state.Left.GrabTransform.Translate = glm::vec3(12.0f, 24.0f, 36.0f);
    state.Left.GrabTransform.AxisX = {1.0f, 0.0f, 0.0f};
    state.Left.GrabTransform.AxisY = {0.0f, 1.0f, 0.0f};
    state.Left.GrabTransform.AxisZ = {0.0f, 0.0f, 1.0f};
    state.Left.GrabTransform.Scale = 1.0f;

    state.Right.Valid = true;
    state.Right.HoldingObject = false;
    state.Right.CanGrabObject = true;
    state.Right.HandInGrabbableState = true;
    state.Right.Disabled = false;
    state.Right.WeaponCollisionDisabled = true;
    state.Right.Fingers.Valid = true;
    state.Right.Fingers.Thumb = 0.6f;
    state.Right.Fingers.Index = 0.7f;
    state.Right.Fingers.Middle = 0.8f;
    state.Right.Fingers.Ring = 0.9f;
    state.Right.Fingers.Pinky = 1.0f;

    state.MutationSequence = 5;
    state.MutationEventCount = 3;
    state.MutationEvents[0].Sequence = 1;
    state.MutationEvents[0].EventKind = VRHiggsEventSnapshot::Kind::kPulled;
    state.MutationEvents[0].HasHand = true;
    state.MutationEvents[0].IsLeft = true;
    state.MutationEvents[0].ObjectId = BuildGameId(25, 0xAAAA);
    state.MutationEvents[0].Mass = 2.5f;
    state.MutationEvents[0].SeparatingVelocity = 3.25f;
    state.MutationEvents[1].Sequence = 3;
    state.MutationEvents[1].EventKind = VRHiggsEventSnapshot::Kind::kGrabbed;
    state.MutationEvents[1].HasHand = true;
    state.MutationEvents[1].IsLeft = true;
    state.MutationEvents[1].ObjectId = BuildGameId(25, 0xBBBB);
    state.MutationEvents[1].Mass = 5.5f;
    state.MutationEvents[1].SeparatingVelocity = 12.25f;
    state.MutationEvents[2].Sequence = 5;
    state.MutationEvents[2].EventKind = VRHiggsEventSnapshot::Kind::kDropped;
    state.MutationEvents[2].HasHand = true;
    state.MutationEvents[2].IsLeft = false;
    state.MutationEvents[2].ObjectId = BuildGameId(25, 0xCCCC);
    state.MutationEvents[2].Mass = 7.0f;
    state.MutationEvents[2].SeparatingVelocity = 4.0f;
    return state;
}
}

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);

    {
        AuthenticationRequest request;
        request.Token = "TesSt";

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->Token == request.Token);
    }

    {
        PartyAcceptInviteRequest request;
        request.InviterId = 123456;

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<PartyAcceptInviteRequest>(std::move(pMessage));
        REQUIRE(pRequest->InviterId == request.InviterId);
    }

    {
        NotifySetTimeResult response;
        response.Result = NotifySetTimeResult::SetTimeResult::kInvalidInput;

        Buffer::Writer writer(&buff);
        response.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == response.GetOpcode());

        auto pResponse = CastUnique<NotifySetTimeResult>(std::move(pMessage));
        REQUIRE(pResponse->Result == NotifySetTimeResult::SetTimeResult::kInvalidInput);
    }

    {
        RequestVRPoseUpdate request;
        request.Pose = BuildPoseUpdate();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRPoseUpdate>(std::move(pMessage));
        REQUIRE(pRequest->Pose == request.Pose);
    }

    {
        NotifyVRPoseUpdate notify;
        notify.PlayerId = 7;
        notify.Pose = BuildPoseUpdate();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRPoseUpdate>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Pose == notify.Pose);
    }

    {
        RequestVREquipmentUpdate request;
        request.Equipment = BuildEquipmentUpdate();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVREquipmentUpdate>(std::move(pMessage));
        REQUIRE(pRequest->Equipment == request.Equipment);
    }

    {
        NotifyVREquipmentUpdate notify;
        notify.PlayerId = 8;
        notify.Equipment = BuildEquipmentUpdate();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVREquipmentUpdate>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Equipment == notify.Equipment);
    }

    {
        RequestVRMovementUpdate request;
        request.Movement = BuildMovementUpdate();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRMovementUpdate>(std::move(pMessage));
        REQUIRE(pRequest->Movement == request.Movement);
    }

    {
        NotifyVRMovementUpdate notify;
        notify.PlayerId = 9;
        notify.Movement = BuildMovementUpdate();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRMovementUpdate>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Movement == notify.Movement);
    }

    {
        RequestVRActivationEvent request;
        request.Activation = BuildActivationEvent();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRActivationEvent>(std::move(pMessage));
        REQUIRE(pRequest->Activation == request.Activation);
    }

    {
        NotifyVRActivationEvent notify;
        notify.PlayerId = 10;
        notify.Activation = BuildActivationEvent();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRActivationEvent>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Activation == notify.Activation);
    }

    {
        RequestVRMagicEffectEvent request;
        request.MagicEffect = BuildMagicEffectEvent();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRMagicEffectEvent>(std::move(pMessage));
        REQUIRE(pRequest->MagicEffect == request.MagicEffect);
    }

    {
        NotifyVRMagicEffectEvent notify;
        notify.PlayerId = 11;
        notify.MagicEffect = BuildMagicEffectEvent();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRMagicEffectEvent>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->MagicEffect == notify.MagicEffect);
    }

    {
        RequestVRCombatHitEvent request;
        request.Hit = BuildCombatHitEvent();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRCombatHitEvent>(std::move(pMessage));
        REQUIRE(pRequest->Hit == request.Hit);
    }

    {
        NotifyVRCombatHitEvent notify;
        notify.PlayerId = 12;
        notify.Hit = BuildCombatHitEvent();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRCombatHitEvent>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Hit == notify.Hit);
    }

    {
        RequestVRProjectileEvent request;
        request.Projectile = BuildProjectileEvent();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRProjectileEvent>(std::move(pMessage));
        REQUIRE(pRequest->Projectile == request.Projectile);
    }

    {
        NotifyVRProjectileEvent notify;
        notify.PlayerId = 13;
        notify.Projectile = BuildProjectileEvent();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRProjectileEvent>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Projectile == notify.Projectile);
    }

    {
        RequestVRGrabEvent request;
        request.Grab = BuildGrabEvent();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRGrabEvent>(std::move(pMessage));
        REQUIRE(pRequest->Grab == request.Grab);
    }

    {
        NotifyVRGrabEvent notify;
        notify.PlayerId = 14;
        notify.Grab = BuildGrabEvent();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRGrabEvent>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->Grab == notify.Grab);
    }

    {
        RequestVRHiggsState request;
        request.State = BuildHiggsState();

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<RequestVRHiggsState>(std::move(pMessage));
        REQUIRE(pRequest->State == request.State);
    }

    {
        NotifyVRHiggsState notify;
        notify.PlayerId = 15;
        notify.State = BuildHiggsState();

        Buffer::Writer writer(&buff);
        notify.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == notify.GetOpcode());

        auto pNotify = CastUnique<NotifyVRHiggsState>(std::move(pMessage));
        REQUIRE(pNotify->PlayerId == notify.PlayerId);
        REQUIRE(pNotify->State == notify.State);
    }
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("GameId")
    {
        GameId sendObjects, recvObjects;
        sendObjects.ModId = 1456987;
        sendObjects.BaseId = 0x789654;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }

    }

    GIVEN("Vector3_NetQuantize")
    {
        Vector3_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 142.56f;
        sendObjects.y = 45687.7f;
        sendObjects.z = -142.56f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }

    }

    GIVEN("Vector2_NetQuantize")
    {
        Vector2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1000.89f;
        sendObjects.y = -485632.75f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VREquipmentUpdate")
    {
        VREquipmentUpdate sendObjects = BuildEquipmentUpdate();
        VREquipmentUpdate recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRMovementUpdate")
    {
        VRMovementUpdate sendObjects = BuildMovementUpdate();
        VRMovementUpdate recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRActivationEvent")
    {
        VRActivationEvent sendObjects = BuildActivationEvent();
        VRActivationEvent recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRMagicEffectEvent")
    {
        VRMagicEffectEvent sendObjects = BuildMagicEffectEvent();
        VRMagicEffectEvent recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRCombatHitEvent")
    {
        VRCombatHitEvent sendObjects = BuildCombatHitEvent();
        VRCombatHitEvent recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRProjectileEvent")
    {
        VRProjectileEvent sendObjects = BuildProjectileEvent();
        VRProjectileEvent recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRGrabEvent")
    {
        VRGrabEvent sendObjects = BuildGrabEvent();
        VRGrabEvent recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRHiggsState")
    {
        VRHiggsState sendObjects = BuildHiggsState();
        VRHiggsState recvObjects;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRHiggsState mutation replay bounds")
    {
        {
            auto sendObjects = BuildHiggsState();
            sendObjects.MutationEvents = {};
            sendObjects.MutationEventCount = 0;
            sendObjects.MutationSequence = 0;

            Buffer buff(1000);
            Buffer::Writer writer(&buff);
            sendObjects.Serialize(writer);
            Buffer::Reader reader(&buff);
            VRHiggsState recvObjects{};
            recvObjects.Deserialize(reader);

            REQUIRE(recvObjects.IsDecodedValid);
            REQUIRE(recvObjects.MutationEventCount == 0);
            REQUIRE(recvObjects.IsMutationReplayValid());
        }

        {
            auto sendObjects = BuildHiggsState();
            const auto event = sendObjects.MutationEvents[0];
            sendObjects.MutationEventCount = static_cast<uint8_t>(kMaximumHiggsMutationEvents);
            for (std::size_t index = 0; index < kMaximumHiggsMutationEvents; ++index)
            {
                sendObjects.MutationEvents[index] = event;
                sendObjects.MutationEvents[index].Sequence = static_cast<uint32_t>(index + 1);
            }
            sendObjects.MutationSequence = static_cast<uint32_t>(kMaximumHiggsMutationEvents);

            Buffer buff(4000);
            Buffer::Writer writer(&buff);
            sendObjects.Serialize(writer);
            Buffer::Reader reader(&buff);
            VRHiggsState recvObjects{};
            recvObjects.Deserialize(reader);

            REQUIRE(recvObjects.IsDecodedValid);
            REQUIRE(recvObjects.MutationEventCount == kMaximumHiggsMutationEvents);
            REQUIRE(recvObjects.IsMutationReplayValid());

            sendObjects.MutationEventCount = static_cast<uint8_t>(kMaximumHiggsMutationEvents + 1);
            sendObjects.MutationSequence = 0xDEADBEEF;
            Buffer clampedBuff(4000);
            Buffer::Writer clampedWriter(&clampedBuff);
            sendObjects.Serialize(clampedWriter);
            Buffer::Reader clampedReader(&clampedBuff);
            VRHiggsState clampedObjects{};
            clampedObjects.Deserialize(clampedReader);

            REQUIRE(clampedObjects.IsDecodedValid);
            REQUIRE(clampedObjects.MutationEventCount == kMaximumHiggsMutationEvents);
            REQUIRE(clampedObjects.MutationSequence == kMaximumHiggsMutationEvents);
            REQUIRE(clampedObjects.IsMutationReplayValid());
        }

        {
            auto sendObjects = BuildHiggsState();
            sendObjects.MutationEvents[0].Sequence = 0xFFFFFFFE;
            sendObjects.MutationEvents[1].Sequence = 0xFFFFFFFF;
            sendObjects.MutationEvents[2].Sequence = 1;
            sendObjects.MutationSequence = 1;

            Buffer buff(1000);
            Buffer::Writer writer(&buff);
            sendObjects.Serialize(writer);
            Buffer::Reader reader(&buff);
            VRHiggsState recvObjects{};
            recvObjects.Deserialize(reader);

            REQUIRE(recvObjects.IsDecodedValid);
            REQUIRE(recvObjects.IsMutationReplayValid());
        }

        // Write a deliberately malformed packet rather than going through
        // VRHiggsState::Serialize(), which clamps in-memory counts by design.
        auto writeHiggsPrefix = [](Buffer::Writer& arWriter, const VRHiggsState& acState,
                                   const uint32_t aMutationSequence) {
            Serialization::WriteVarInt(arWriter, acState.Sequence);
            Serialization::WriteVarInt(arWriter, aMutationSequence);
            Serialization::WriteBool(arWriter, acState.BridgeLoaded);
            Serialization::WriteBool(arWriter, acState.Detected);
            Serialization::WriteBool(arWriter, acState.InterfaceAvailable);
            Serialization::WriteBool(arWriter, acState.CallbacksRegistered);
            Serialization::WriteBool(arWriter, acState.SnapshotAvailable);
            Serialization::WriteVarInt(arWriter, acState.SnapshotSequence);
            Serialization::WriteBool(arWriter, acState.TwoHanding);
            acState.Left.Serialize(arWriter);
            acState.Right.Serialize(arWriter);
        };

        {
            const auto sendObjects = BuildHiggsState();
            Buffer buff(1000);
            Buffer::Writer writer(&buff);
            writeHiggsPrefix(writer, sendObjects, sendObjects.MutationSequence);
            Serialization::WriteVarInt(writer, kMaximumHiggsMutationEvents + 1);
            Buffer::Reader reader(&buff);
            VRHiggsState recvObjects{};
            recvObjects.Deserialize(reader);

            REQUIRE_FALSE(recvObjects.IsDecodedValid);
            REQUIRE_FALSE(recvObjects.IsMutationReplayValid());
        }

        {
            const auto sendObjects = BuildHiggsState();
            Buffer buff(1000);
            Buffer::Writer writer(&buff);
            writeHiggsPrefix(writer, sendObjects, sendObjects.MutationSequence - 1);
            Serialization::WriteVarInt(writer, sendObjects.MutationEventCount);
            for (std::size_t index = 0; index < sendObjects.MutationEventCount; ++index)
                sendObjects.MutationEvents[index].Serialize(writer);
            Buffer::Reader reader(&buff);
            VRHiggsState recvObjects{};
            recvObjects.Deserialize(reader);

            REQUIRE(recvObjects.IsDecodedValid);
            REQUIRE_FALSE(recvObjects.IsMutationReplayValid());
        }
    }

    GIVEN("VR interaction scalar bounds")
    {
        THEN("HIGGS mutation payloads use the bridge's finite one-million limit")
        {
            REQUIRE(SkyrimTogether::VR::IsHiggsMutationPayloadValid(
                SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude,
                -SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude));
            REQUIRE_FALSE(SkyrimTogether::VR::IsHiggsMutationPayloadValid(
                SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude + 1.0F, 0.0F));
            REQUIRE_FALSE(SkyrimTogether::VR::IsHiggsMutationPayloadValid(0.0F,
                -SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude - 1.0F));

            auto state = BuildHiggsState();
            state.MutationEvents[0].Mass = SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude + 1.0F;
            REQUIRE_FALSE(state.IsMutationReplayValid());
        }

        THEN("VRGrab position components use the same finite limit")
        {
            auto grab = BuildGrabEvent();
            grab.Position = glm::vec3{
                SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude,
                -SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude,
                0.0F,
            };
            REQUIRE(SkyrimTogether::VR::IsVRGrabPositionValid(grab.Position));

            grab.Position.z = SkyrimTogether::VR::kMaximumHiggsInteractionMagnitude + 1.0F;
            REQUIRE_FALSE(SkyrimTogether::VR::IsVRGrabPositionValid(grab.Position));
        }
    }

    GIVEN("Rotator2_NetQuantize")
    {
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1.89f;
        sendObjects.y = TiltedPhoques::Pi * 2.0f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize needing wrap")
    {
        // This test is a bit dangerous as floating errors can lead to sendObjects != recvObjects but the difference is minuscule so we don't care abut such cases
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = -1.87f;
        sendObjects.y = static_cast<float>(TiltedPhoques::Pi) * 18.0f + 3.6f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRPoseUpdate")
    {
        VRPoseUpdate sendObjects = BuildPoseUpdate();
        VRPoseUpdate recvObjects;

        REQUIRE(HasAnyVRPosePayload(sendObjects));
        REQUIRE(IsVRBodyPoseDataSafe(sendObjects.Body));
        REQUIRE(IsVRPoseUpdateSafe(sendObjects));

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("VRPoseUpdate fixed body formats")
    {
        const std::array<VRPoseUpdate, 3> variants{
            BuildPoseUpdateBodyV1(),
            BuildPoseUpdateBodyV2(),
            BuildPoseUpdate(),
        };

        THEN("format-1, format-2, and format-3 each round-trip at their fixed wire boundary")
        {
            for (const auto& send : variants) {
                VRPoseUpdate recv{};
                Buffer buff(4096);
                Buffer::Writer writer(&buff);
                send.Serialize(writer);
                Buffer::Reader reader(&buff);
                recv.Deserialize(reader);
                REQUIRE(IsSupportedVRBodyPoseFormatVersion(send.Body.FormatVersion));
                REQUIRE(IsVRBodyPoseDataSafe(send.Body));
                REQUIRE(send == recv);
            }
        }
    }

    GIVEN("VRPoseUpdate validation")
    {
        auto pose = BuildPoseUpdate();

        WHEN("only VRIK state is present")
        {
            VRPoseUpdate vrikOnly{};
            vrikOnly.Sequence = 1;
            vrikOnly.Vrik.Detected = true;
            vrikOnly.Vrik.InterfaceAvailable = true;

            THEN("the payload remains relayable and safe")
            {
                REQUIRE(HasAnyVRPosePayload(vrikOnly));
                REQUIRE(IsVRPoseUpdateSafe(vrikOnly));
            }
        }

        WHEN("a limb contains translation")
        {
            pose.Body.LeftCalf.Position.x = 1.0f;
            THEN("body validation rejects it")
            {
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a body basis is left handed")
        {
            pose.Body.RightFoot.AxisZ = {0.0f, 0.0f, -1.0f};
            THEN("body validation rejects it")
            {
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
            }
        }

        WHEN("body capture is explicitly stale")
        {
            pose.Body = {};
            pose.Body.FormatVersion = 2;
            THEN("the zero invalid state is accepted")
            {
                REQUIRE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("the body format is unknown")
        {
            pose.Body = {};
            pose.Body.FormatVersion = 4;
            THEN("the fixed-order schema fails closed")
            {
                REQUIRE_FALSE(IsSupportedVRBodyPoseFormatVersion(pose.Body.FormatVersion));
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a sparse joint rotation is not unit length")
        {
            pose.Body.Joints.Rotations[0].W = 0.0f;
            THEN("joint validation rejects the frame")
            {
                REQUIRE_FALSE(IsVRBodyJointPoseDataSafe(pose.Body.Joints));
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
            }
        }

        WHEN("the joint mask contains an unknown bit")
        {
            pose.Body.Joints.NodeMask |= 1u << 30;
            THEN("the body payload fails closed")
            {
                REQUIRE_FALSE(IsVRBodyJointPoseDataSafe(pose.Body.Joints));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a version-3 body has an explicitly absent joint extension")
        {
            pose.Body.Joints = {};
            THEN("the body remains relayable")
            {
                REQUIRE_FALSE(pose.Body.Joints.Valid);
                REQUIRE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a version-3 joint extension capture sequence differs from its body")
        {
            ++pose.Body.Joints.CaptureSequence;
            THEN("the incoherent version-3 frame is rejected")
            {
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a version-3 joint extension root generation differs from its body")
        {
            ++pose.Body.Joints.RootGeneration;
            THEN("the incoherent version-3 frame is rejected")
            {
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a version-1 body has no extended joints")
        {
            pose = BuildPoseUpdateBodyV1();
            THEN("the legacy body remains relayable")
            {
                REQUIRE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a version-2 body retains fingers but omits the format-3 upper body")
        {
            pose = BuildPoseUpdateBodyV2();
            THEN("the older fixed layout remains safe")
            {
                REQUIRE(pose.Body.FormatVersion == 2);
                REQUIRE_FALSE(pose.Body.Spine0.Valid);
                REQUIRE(pose.Body.Joints.Valid);
                REQUIRE(IsVRBodyPoseDataSafe(pose.Body));
            }
        }

        WHEN("a version-3 body omits an upper chain node")
        {
            pose.Body.LeftForearm = {};
            THEN("the complete full-body frame fails closed")
            {
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("a version-3 body carries the complete upper chain")
        {
            THEN("all new parent-local nodes are required and safe")
            {
                REQUIRE(pose.Body.FormatVersion == 3);
                REQUIRE(pose.Body.Spine0.Valid);
                REQUIRE(pose.Body.Spine1.Valid);
                REQUIRE(pose.Body.Spine2.Valid);
                REQUIRE(pose.Body.Neck.Valid);
                REQUIRE(pose.Body.LeftClavicle.Valid);
                REQUIRE(pose.Body.LeftUpperArm.Valid);
                REQUIRE(pose.Body.LeftForearm.Valid);
                REQUIRE(pose.Body.RightClavicle.Valid);
                REQUIRE(pose.Body.RightUpperArm.Valid);
                REQUIRE(pose.Body.RightForearm.Valid);
                REQUIRE(IsVRBodyPoseDataSafe(pose.Body));
            }
        }

        WHEN("an unknown body format is followed by another fixed field")
        {
            VRBodyPoseData sendBody{};
            sendBody.FormatVersion = 99;
            VRFingerCurlData sendFollower{};
            sendFollower.Valid = true;
            sendFollower.Thumb = 0.1f;
            sendFollower.Index = 0.2f;
            sendFollower.Middle = 0.3f;
            sendFollower.Ring = 0.4f;
            sendFollower.Pinky = 0.5f;

            THEN("the unknown body consumes only its format tag and preserves the following field")
            {
                Buffer buff(256);
                Buffer::Writer writer(&buff);
                sendBody.Serialize(writer);
                sendFollower.Serialize(writer);

                VRBodyPoseData recvBody{};
                VRFingerCurlData recvFollower{};
                Buffer::Reader reader(&buff);
                recvBody.Deserialize(reader);
                recvFollower.Deserialize(reader);
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(recvBody));
                REQUIRE(sendFollower == recvFollower);
            }
        }

        WHEN("a sparse mask declares an unknown quaternion")
        {
            VRFingerCurlData sendFollower{};
            sendFollower.Valid = true;
            sendFollower.Thumb = 0.1f;
            sendFollower.Index = 0.2f;
            sendFollower.Middle = 0.3f;
            sendFollower.Ring = 0.4f;
            sendFollower.Pinky = 0.5f;

            THEN("the declared malformed payload is consumed before validation fails")
            {
                Buffer buff(256);
                Buffer::Writer writer(&buff);
                TiltedPhoques::Serialization::WriteVarInt(writer, 1);
                TiltedPhoques::Serialization::WriteBool(writer, true);
                TiltedPhoques::Serialization::WriteVarInt(writer, 7);
                TiltedPhoques::Serialization::WriteVarInt(writer, 3);
                TiltedPhoques::Serialization::WriteVarInt(writer, 1u << 30);
                TiltedPhoques::Serialization::WriteFloat(writer, 0.0f);
                TiltedPhoques::Serialization::WriteFloat(writer, 0.0f);
                TiltedPhoques::Serialization::WriteFloat(writer, 0.0f);
                TiltedPhoques::Serialization::WriteFloat(writer, 1.0f);
                sendFollower.Serialize(writer);

                VRBodyJointPoseData recvJoints{};
                VRFingerCurlData recvFollower{};
                Buffer::Reader reader(&buff);
                recvJoints.Deserialize(reader);
                recvFollower.Deserialize(reader);
                REQUIRE_FALSE(IsVRBodyJointPoseDataSafe(recvJoints));
                REQUIRE(sendFollower == recvFollower);
            }
        }
    }
}

TEST_CASE("Movement preserves the direction float bit pattern", "[encoding.movement]")
{
    Movement sent{};
    sent.Direction = std::bit_cast<float>(std::uint32_t{0x80000000});

    Buffer buffer(256);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Movement received{};
    Buffer::Reader reader(&buffer);
    received.Deserialize(reader);

    REQUIRE(received.IsDecodedValid);
    REQUIRE(std::bit_cast<std::uint32_t>(received.Direction) == 0x80000000);
}

TEST_CASE("Differential structures", "[encoding.differential]")
{
    GIVEN("Full ActionEvent")
    {
        ActionEvent sendAction, recvAction;

        sendAction.ActionId = BuildGameId(1, 42);
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = BuildGameId(2, 87964);
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = BuildGameId(3, 963741);
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("A single cached event name")
    {
        ActionEvent sendAction, recvAction;

        TP_UNUSED(StringCache::Get().Add("test"))

        sendAction.ActionId = BuildGameId(1, 42);
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = BuildGameId(2, 87964);
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = BuildGameId(3, 963741);
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("Full Mods")
    {
        Mods sendMods, recvMods;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);

        sendMods.ModList.push_back({"Hello", 42});
        sendMods.ModList.push_back({"Hi", 14});
        sendMods.ModList.push_back({"Test", 8});
        sendMods.ModList.push_back({"Toast", 49});

        sendMods.Serialize(writer);

        Buffer::Reader reader(&buff);
        recvMods.Deserialize(reader);

        REQUIRE(sendMods == recvMods);
    }

    GIVEN("AnimationVariables")
    {
        AnimationVariables vars, recvVars;
 
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF"
                          "\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        Buffer buff(1000);
        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }

        vars.Booleans.resize(33);
        vars.Booleans[16] = false;
        vars.Booleans[17] = false;
        vars.Booleans[18] = false;
        vars.Booleans[19] = false;
        vars.Floats[3] = 42.f;
        vars.Integers[0] = 18;
        vars.Integers[3] = 0;

        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }
    }
}

TEST_CASE("Packets", "[encoding.packets]")
{
    SECTION("AuthenticationRequest")
    {
        Buffer buff(1000);

        AuthenticationRequest sendMessage, recvMessage;
        sendMessage.Token = "TesSt";
        sendMessage.Version = "stvr-test-1-g12345678";
        sendMessage.GameplayProtocolRevision = 1;
        sendMessage.GameplayCapabilities = 3;
        sendMessage.ClientSessionNonce = 0x1122334455667788ull;
        sendMessage.ConnectionAttempt = 7;
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Version == "stvr-test-1-g12345678");
        REQUIRE(sendMessage == recvMessage);
        recvMessage.Version = "different-build";
        REQUIRE_FALSE(sendMessage == recvMessage);
        recvMessage = sendMessage;
        ++recvMessage.ConnectionAttempt;
        REQUIRE_FALSE(sendMessage == recvMessage);
    }

    SECTION("AuthenticationResponse")
    {
        Buffer buff(1000);

        AuthenticationResponse sendMessage, recvMessage;
        sendMessage.Type = AuthenticationResponse::ResponseType::kAccepted;
        sendMessage.SKSEActive = true;
        sendMessage.MO2Active = true;
        sendMessage.Version = "stvr-test-1-g12345678";
        sendMessage.GameplayProtocolRevision = 1;
        sendMessage.ServerCapabilities = 3;
        sendMessage.NegotiatedCapabilities = 3;
        sendMessage.ServerInstanceNonce = 0x8877665544332211ull;
        sendMessage.ConnectionGeneration = 9;
        sendMessage.ClientSessionNonce = 0x1122334455667788ull;
        sendMessage.ConnectionAttempt = 7;
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Version == "stvr-test-1-g12345678");
        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestOwnershipClaim")
    {
        Buffer buff(4096);
        RequestOwnershipClaim sendMessage, recvMessage;
        sendMessage.ServerId = 0x1234;
        sendMessage.GrantToken = 0x1122334455667788ull;
        sendMessage.NewActorData.IsDead = true;
        sendMessage.NewActorData.IsWeaponDrawn = true;
        sendMessage.NewActorData.InitialInventory.CurrentMagicEquipment.LeftHandSpell = BuildGameId(1, 0x4567);

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8);
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyOwnershipTransfer")
    {
        Buffer buff(128);
        NotifyOwnershipTransfer sendMessage, recvMessage;
        sendMessage.ServerId = 0x2345;
        sendMessage.GrantToken = 0x8877665544332211ull;

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8);
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestEquipmentChanges final transaction")
    {
        Buffer buff(4096);
        RequestEquipmentChanges sendMessage, recvMessage;
        sendMessage.ServerId = 0x3456;
        sendMessage.TransactionId = 0x0102030405060708ull;
        auto& entry = sendMessage.CurrentInventory.Entries.emplace_back();
        entry.BaseId = BuildGameId(2, 0x5678);
        entry.Count = 1;
        entry.ExtraWorn = true;
        entry.ExtraWornLeft = true;
        entry.EquipmentFlags = Inventory::Entry::kEquipmentWeapon;
        sendMessage.CurrentInventory.CurrentMagicEquipment.RightHandSpell = BuildGameId(3, 0x6789);
        sendMessage.CurrentInventory.CurrentMagicEquipment.Shout = BuildGameId(4, 0x789A);

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8);
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyEquipmentChanges final transaction")
    {
        Buffer buff(4096);
        NotifyEquipmentChanges sendMessage, recvMessage;
        sendMessage.ServerId = 0x4567;
        sendMessage.TransactionId = 0x1020304050607080ull;
        auto& entry = sendMessage.FinalEquipment.Entries.emplace_back();
        entry.BaseId = BuildGameId(5, 0x89AB);
        entry.Count = 2;
        entry.ExtraWornLeft = true;
        entry.EquipmentFlags = Inventory::Entry::kEquipmentWeapon;
        sendMessage.FinalEquipment.CurrentMagicEquipment.LeftHandSpell = BuildGameId(6, 0x9ABC);

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8);
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("VRAppearance schema v2 round trip and validation")
    {
        Buffer buff(4096);
        VRAppearance sendAppearance, recvAppearance;
        sendAppearance.Sequence = 17;
        sendAppearance.RaceId = BuildGameId(1, 0x1234);
        sendAppearance.Sex = 1;
        sendAppearance.Weight = 62.5F;
        sendAppearance.Level = 37;
        sendAppearance.Essential = true;
        sendAppearance.HairColorId = BuildGameId(2, 0x2345);
        sendAppearance.FaceTextureId = BuildGameId(3, 0x3456);
        sendAppearance.HasFaceData = true;
        for (std::size_t index = 0; index < sendAppearance.FaceMorphs.size(); ++index)
            sendAppearance.FaceMorphs[index] = static_cast<float>(index) / 20.0F;
        sendAppearance.FaceParts = {1, 2, 3, VRAppearance::kFacePartDefault};
        constexpr std::string_view name{"VR Dragonborn"};
        sendAppearance.NameLength = static_cast<std::uint8_t>(name.size());
        std::copy(name.begin(), name.end(), sendAppearance.Name.begin());
        sendAppearance.HeadPartCount = 1;
        sendAppearance.HeadParts[0] = {2, BuildGameId(4, 0x4567)};
        sendAppearance.TintCount = 2;
        sendAppearance.Tints[0] = {6, 0x112233, 1.0F};
        SetTintTexturePath(sendAppearance.Tints[0], "textures/actors/character/overlays/warpaint.dds");
        sendAppearance.Tints[1] = {6, 0x445566, 0.5F};
        REQUIRE(sendAppearance.IsValid());

        Buffer::Writer writer(&buff);
        sendAppearance.Serialize(writer);
        Buffer::Reader reader(&buff);
        recvAppearance.Deserialize(reader);

        REQUIRE(recvAppearance.IsValid());
        REQUIRE(sendAppearance == recvAppearance);
    }

    SECTION("VRAppearance validation failure masks")
    {
        const auto expectFailure = [](const VRAppearance& acAppearance,
                                      const VRAppearance::ValidationMask aExpectedFailure) {
            REQUIRE_FALSE(acAppearance.IsValid());
            REQUIRE(acAppearance.GetValidationFailureMask() == aExpectedFailure);
        };

        auto invalidAppearance = BuildMinimalVRAppearance();
        REQUIRE(invalidAppearance.IsValid());
        REQUIRE(invalidAppearance.GetValidationFailureMask() == VRAppearance::kValidationFailureNone);

        invalidAppearance.SchemaVersion = 1;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureCoreSchema);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.NameLength = 0;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureNameLength);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.Name[0] = static_cast<char>(0xC3);
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureNameUtf8);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.Name[invalidAppearance.NameLength] = 'x';
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureNameZeroTail);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.FaceMorphs[0] = 1.0F;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureFaceMorphs);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.FaceParts[0] = 1;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureFaceParts);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.HeadPartCount = VRAppearance::kMaximumHeadParts + 1;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureHeadPartCount);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.HeadPartCount = 1;
        invalidAppearance.HeadParts[0] = {VRAppearance::kMaximumHeadParts, BuildGameId(1, 1)};
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureHeadPartEntry);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.HeadPartCount = 2;
        invalidAppearance.HeadParts[0] = {0, BuildGameId(1, 1)};
        invalidAppearance.HeadParts[1] = {0, BuildGameId(1, 2)};
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureDuplicateHeadPartSlot);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.HeadParts[0] = {0, BuildGameId(1, 1)};
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureUnusedHeadPartTail);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = VRAppearance::kMaximumTints + 1;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintCount);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        invalidAppearance.Tints[0].Type = 15;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintEntry);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        invalidAppearance.Tints[0].Alpha = 1.1F;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintEntry);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        invalidAppearance.Tints[0].TexturePathLength = 1;
        invalidAppearance.Tints[0].TexturePath[0] = static_cast<char>(0xC3);
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathUtf8);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        invalidAppearance.Tints[0].TexturePathLength = 3;
        invalidAppearance.Tints[0].TexturePath[0] = 'a';
        invalidAppearance.Tints[0].TexturePath[1] = '\0';
        invalidAppearance.Tints[0].TexturePath[2] = 'b';
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathUtf8);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        SetTintTexturePath(invalidAppearance.Tints[0], "/textures/warpaint.dds");
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathStructuralSafety);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        SetTintTexturePath(invalidAppearance.Tints[0], "textures/../warpaint.dds");
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathStructuralSafety);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        SetTintTexturePath(invalidAppearance.Tints[0], "textures//warpaint.dds");
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathStructuralSafety);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        SetTintTexturePath(invalidAppearance.Tints[0], "textures:warpaint.dds");
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathStructuralSafety);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.TintCount = 1;
        SetTintTexturePath(invalidAppearance.Tints[0], "textures/warpaint.dds");
        invalidAppearance.Tints[0].TexturePath[invalidAppearance.Tints[0].TexturePathLength] = 'x';
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureTintPathZeroTail);

        invalidAppearance = BuildMinimalVRAppearance();
        invalidAppearance.Tints[0].Color = 1;
        expectFailure(invalidAppearance, VRAppearance::kValidationFailureUnusedTintTail);
    }

    SECTION("CharacterSpawnRequest preserves a semantic NPC appearance")
    {
        Buffer buff(4096);
        CharacterSpawnRequest sendMessage, recvMessage;
        sendMessage.ServerId = 0x4567;
        sendMessage.FormId = BuildGameId(1, 0x1234);
        sendMessage.BaseId = BuildGameId(1, 0x2345);
        sendMessage.CellId = BuildGameId(1, 0x3456);
        sendMessage.IsPlayer = false;
        sendMessage.HasVRAppearance = true;
        sendMessage.InitialVRAppearance = BuildMinimalVRAppearance();
        REQUIRE(sendMessage.InitialVRAppearance.IsValid());

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buff);
        std::uint64_t opcode{};
        reader.ReadBits(opcode, 8);
        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.IsDecodedValid);
        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("VRAppearance consumes oversized bounded fields")
    {
        {
            Buffer buff(4096);
            Buffer::Writer writer(&buff);
            WriteVRAppearancePrefix(writer);
            writer.WriteBits(128, 8);
            for (std::uint8_t index = 0; index < 128; ++index)
                writer.WriteBits('a', 8);
            writer.WriteBits(0, 8);
            writer.WriteBits(0, 8);
            writer.WriteBits(0xA5, 8);

            Buffer::Reader reader(&buff);
            VRAppearance appearance;
            appearance.Deserialize(reader);
            std::uint64_t sentinel{};
            reader.ReadBits(sentinel, 8);
            REQUIRE_FALSE(appearance.IsValid());
            REQUIRE(sentinel == 0xA5);
        }

        {
            Buffer buff(4096);
            Buffer::Writer writer(&buff);
            WriteVRAppearancePrefix(writer);
            writer.WriteBits(0, 8);
            writer.WriteBits(0, 8);
            writer.WriteBits(VRAppearance::kMaximumTints + 1, 8);
            for (std::uint8_t index = 0; index <= VRAppearance::kMaximumTints; ++index)
            {
                writer.WriteBits(0, 8);
                writer.WriteBits(0, 32);
                writer.WriteBits(0, 32);
                writer.WriteBits(0, 8);
            }
            writer.WriteBits(0xA5, 8);

            Buffer::Reader reader(&buff);
            VRAppearance appearance;
            appearance.Deserialize(reader);
            std::uint64_t sentinel{};
            reader.ReadBits(sentinel, 8);
            REQUIRE_FALSE(appearance.IsValid());
            REQUIRE(sentinel == 0xA5);
        }
    }

    SECTION("CancelAssignmentRequest")
    {
        Buffer buff(1000);

        CancelAssignmentRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AssignCharacterRequest")
    {
        Buffer buff(1000);

        ActionEvent sendAction;
        sendAction.ActionId = BuildGameId(1, 42);
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = BuildGameId(2, 87964);
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = BuildGameId(3, 963741);
        sendAction.Type = 4;

        AssignCharacterRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        sendMessage.AppearanceBuffer = "toto";
        sendMessage.CellId.BaseId = 45;
        sendMessage.FormId.ModId = 48;
        sendMessage.ReferenceId.BaseId = 456799;
        sendMessage.ReferenceId.ModId = 4079;
        sendMessage.LatestAction = sendAction;
        sendMessage.Position.x = -452.4f;
        sendMessage.Position.y = 452.4f;
        sendMessage.Position.z = 125452.4f;
        sendMessage.Rotation.x = -1.87f;
        sendMessage.Rotation.y = 45.35f;
        sendMessage.HasQuestContent = true;
        sendMessage.HasFaceTints = true;

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AssignCharacterRequest preserves essential-only VR actor values")
    {
        Buffer buff(1000);
        AssignCharacterRequest sendMessage, recvMessage;
        sendMessage.Cookie = 1;
        sendMessage.ReferenceId = BuildGameId(0, 0x14);
        sendMessage.CellId = BuildGameId(1, 0x1234);
        for (const auto actorValue : SkyrimTogetherVR::GameplayBridge::kEssentialAssignmentActorValues) {
            sendMessage.CurrentActorData.InitialActorValues.ActorValuesList.emplace(actorValue, 100.0F + actorValue);
            sendMessage.CurrentActorData.InitialActorValues.ActorMaxValuesList.emplace(actorValue, 200.0F + actorValue);
        }

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buff);
        std::uint64_t opcode{};
        reader.ReadBits(opcode, 8);
        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage == sendMessage);
        REQUIRE(recvMessage.CurrentActorData.InitialActorValues.ActorValuesList.size() == 3);
        REQUIRE(recvMessage.CurrentActorData.InitialActorValues.ActorMaxValuesList.size() == 3);
    }

    SECTION("AssignObjectsRequest and response preserve bounded object state")
    {
        ObjectData object{};
        object.Id = BuildGameId(1, 0x1234);
        object.CellId = BuildGameId(1, 0x5678);
        object.WorldSpaceId = BuildGameId(1, 0x9ABC);
        object.CurrentCoords = GridCellCoords{4, -7};
        object.CurrentLockData.IsLocked = true;
        object.CurrentLockData.LockLevel = 50;
        Inventory::Entry entry{};
        entry.BaseId = BuildGameId(1, 0xF);
        entry.Count = 25;
        object.CurrentInventory.Entries.push_back(entry);

        AssignObjectsRequest sendRequest{};
        sendRequest.Objects.push_back(object);
        Buffer requestBuffer(4096);
        Buffer::Writer requestWriter(&requestBuffer);
        sendRequest.Serialize(requestWriter);
        Buffer::Reader requestReader(&requestBuffer);
        std::uint64_t opcode{};
        requestReader.ReadBits(opcode, 8);
        AssignObjectsRequest receivedRequest{};
        receivedRequest.DeserializeRaw(requestReader);
        REQUIRE(receivedRequest.IsDecodedValid);
        REQUIRE(sendRequest == receivedRequest);

        object.ServerId = 42;
        object.IsSenderFirst = false;
        AssignObjectsResponse sendResponse{};
        sendResponse.Objects.push_back(object);
        Buffer responseBuffer(4096);
        Buffer::Writer responseWriter(&responseBuffer);
        sendResponse.Serialize(responseWriter);
        Buffer::Reader responseReader(&responseBuffer);
        responseReader.ReadBits(opcode, 8);
        AssignObjectsResponse receivedResponse{};
        receivedResponse.DeserializeRaw(responseReader);
        REQUIRE(receivedResponse.IsDecodedValid);
        REQUIRE(sendResponse == receivedResponse);
    }

    SECTION("AssignObjectsRequest rejects an oversized nested inventory")
    {
        AssignObjectsRequest sendMessage{};
        ObjectData object{};
        object.Id = BuildGameId(1, 0x1234);
        object.CellId = BuildGameId(1, 0x5678);
        object.CurrentInventory.Entries.resize(Inventory::kMaximumWireEntries + 1);
        sendMessage.Objects.push_back(std::move(object));

        Buffer buffer(2 * 1024 * 1024);
        Buffer::Writer writer(&buffer);
        sendMessage.Serialize(writer);
        Buffer::Reader reader(&buffer);
        std::uint64_t opcode{};
        reader.ReadBits(opcode, 8);
        AssignObjectsRequest received{};
        received.DeserializeRaw(reader);
        REQUIRE_FALSE(received.IsDecodedValid);
        REQUIRE(received.Objects.empty());
    }

    GIVEN("ClientReferencesMoveRequest")
    {
        ClientReferencesMoveRequest sendMessage, recvMessage;
        auto& update = sendMessage.Updates[1];
        auto& move = update.UpdatedMovement;

        AnimationVariables vars;
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        move.Variables = vars;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Updates[1].UpdatedMovement == sendMessage.Updates[1].UpdatedMovement);
    }

    SECTION("RequestVRPoseUpdate")
    {
        Buffer buff(4096);

        RequestVRPoseUpdate sendMessage, recvMessage;
        sendMessage.Pose = BuildPoseUpdate();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRPoseUpdate::BodyV0Absent")
    {
        Buffer buff(4096);

        RequestVRPoseUpdate sendMessage, recvMessage;
        sendMessage.Pose = BuildPoseUpdateBodyV0();

        REQUIRE(sendMessage.Pose.Body.FormatVersion == 0);
        REQUIRE(sendMessage.Pose.Body.Valid == false);
        REQUIRE(sendMessage.Pose.Body.CaptureSequence == 0);
        REQUIRE(sendMessage.Pose.Body.RootGeneration == 0);
        REQUIRE((!sendMessage.Pose.Body.Pelvis.Valid && !sendMessage.Pose.Body.LeftThigh.Valid &&
                 !sendMessage.Pose.Body.LeftCalf.Valid && !sendMessage.Pose.Body.LeftFoot.Valid &&
                 !sendMessage.Pose.Body.RightThigh.Valid && !sendMessage.Pose.Body.RightCalf.Valid &&
                 !sendMessage.Pose.Body.RightFoot.Valid));

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRPoseUpdate::BodyV1Invalid")
    {
        Buffer buff(4096);

        RequestVRPoseUpdate sendMessage, recvMessage;
        sendMessage.Pose = BuildPoseUpdateBodyV1Invalid();

        REQUIRE(sendMessage.Pose.Body.FormatVersion == 1);
        REQUIRE(sendMessage.Pose.Body.Valid == false);
        REQUIRE(sendMessage.Pose.Body.CaptureSequence == 0);
        REQUIRE(sendMessage.Pose.Body.RootGeneration == 0);
        REQUIRE((!sendMessage.Pose.Body.Pelvis.Valid && !sendMessage.Pose.Body.LeftThigh.Valid &&
                 !sendMessage.Pose.Body.LeftCalf.Valid && !sendMessage.Pose.Body.LeftFoot.Valid &&
                 !sendMessage.Pose.Body.RightThigh.Valid && !sendMessage.Pose.Body.RightCalf.Valid &&
                 !sendMessage.Pose.Body.RightFoot.Valid));

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRPoseUpdate::BodyV1WithoutJointExtension")
    {
        Buffer buff(4096);

        RequestVRPoseUpdate sendMessage, recvMessage;
        sendMessage.Pose = BuildPoseUpdateBodyV1();

        REQUIRE(sendMessage.Pose.Body.FormatVersion == 1);
        REQUIRE(sendMessage.Pose.Body.Valid);
        REQUIRE(sendMessage.Pose.Body.Joints.FormatVersion == 0);
        REQUIRE_FALSE(sendMessage.Pose.Body.Joints.Valid);

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyVRPoseUpdate")
    {
        Buffer buff(4096);

        NotifyVRPoseUpdate sendMessage, recvMessage;
        sendMessage.PlayerId = 7;
        sendMessage.Pose = BuildPoseUpdate();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVREquipmentUpdate")
    {
        Buffer buff(1000);

        RequestVREquipmentUpdate sendMessage, recvMessage;
        sendMessage.Equipment = BuildEquipmentUpdate();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyVREquipmentUpdate")
    {
        Buffer buff(1000);

        NotifyVREquipmentUpdate sendMessage, recvMessage;
        sendMessage.PlayerId = 8;
        sendMessage.Equipment = BuildEquipmentUpdate();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRMovementUpdate")
    {
        Buffer buff(1000);

        RequestVRMovementUpdate sendMessage, recvMessage;
        sendMessage.Movement = BuildMovementUpdate();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyVRMovementUpdate")
    {
        Buffer buff(1000);

        NotifyVRMovementUpdate sendMessage, recvMessage;
        sendMessage.PlayerId = 9;
        sendMessage.Movement = BuildMovementUpdate();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRActivationEvent")
    {
        Buffer buff(1000);

        RequestVRActivationEvent sendMessage, recvMessage;
        sendMessage.Activation = BuildActivationEvent();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyVRActivationEvent")
    {
        Buffer buff(1000);

        NotifyVRActivationEvent sendMessage, recvMessage;
        sendMessage.PlayerId = 10;
        sendMessage.Activation = BuildActivationEvent();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRMagicEffectEvent")
    {
        Buffer buff(1000);

        RequestVRMagicEffectEvent sendMessage, recvMessage;
        sendMessage.MagicEffect = BuildMagicEffectEvent();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyVRMagicEffectEvent")
    {
        Buffer buff(1000);

        NotifyVRMagicEffectEvent sendMessage, recvMessage;
        sendMessage.PlayerId = 11;
        sendMessage.MagicEffect = BuildMagicEffectEvent();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestVRCombatHitEvent")
    {
        Buffer buff(1000);

        RequestVRCombatHitEvent sendMessage, recvMessage;
        sendMessage.Hit = BuildCombatHitEvent();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyVRCombatHitEvent")
    {
        Buffer buff(1000);

        NotifyVRCombatHitEvent sendMessage, recvMessage;
        sendMessage.PlayerId = 12;
        sendMessage.Hit = BuildCombatHitEvent();

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }
}

TEST_CASE("VR handoff launch identity", "[vr.handoff]")
{
    std::string normalized;
    REQUIRE(SkyrimTogetherVR::Handoff::NormalizeLaunchNonce("ABCDEF0123456789ABCDEF0123456789", normalized));
    REQUIRE(normalized == "abcdef0123456789abcdef0123456789");
    REQUIRE_FALSE(SkyrimTogetherVR::Handoff::NormalizeLaunchNonce("abcdef0123456789abcdef012345678", normalized));
    REQUIRE_FALSE(SkyrimTogetherVR::Handoff::NormalizeLaunchNonce("abcdef0123456789abcdef012345678g", normalized));
}

TEST_CASE("StringCache", "[encoding.string_cache]")
{
    SECTION("Messages")
    {
        StringCacheUpdate update;
        update.Values.push_back("Hello");
        update.Values.push_back("Bye");

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        update.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        StringCacheUpdate recvUpdate;
        recvUpdate.DeserializeRaw(reader);

        REQUIRE(update == recvUpdate);
    }
}
