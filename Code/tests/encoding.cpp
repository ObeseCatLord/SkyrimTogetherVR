#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "StringCache.h"
#include "Messages/StringCacheUpdate.h"

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/Vector2_NetQuantize.h>
#include <Structs/VRActivationEvent.h>
#include <Structs/VRCombatHitEvent.h>
#include <Structs/VREquipmentUpdate.h>
#include <Structs/VRGrabEvent.h>
#include <Structs/VRHiggsState.h>
#include <Structs/VRMagicEffectEvent.h>
#include <Structs/VRMovementUpdate.h>
#include <Structs/VRPoseUpdate.h>
#include <Structs/VRProjectileEvent.h>

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
    pose.Body.FormatVersion = 1;
    pose.Body.Valid = true;
    pose.Body.CaptureSequence = 1;
    pose.Body.RootGeneration = 1;
    pose.Body.Pelvis = BuildPoseNode(250.0f);
    pose.Body.LeftThigh = BuildPoseNode(290.0f);
    pose.Body.LeftCalf = BuildPoseNode(300.0f);
    pose.Body.LeftFoot = BuildPoseNode(310.0f);
    pose.Body.RightThigh = BuildPoseNode(320.0f);
    pose.Body.RightCalf = BuildPoseNode(330.0f);
    pose.Body.RightFoot = BuildPoseNode(340.0f);
    pose.Body.LeftThigh.Position = {};
    pose.Body.LeftCalf.Position = {};
    pose.Body.LeftFoot.Position = {};
    pose.Body.RightThigh.Position = {};
    pose.Body.RightCalf.Position = {};
    pose.Body.RightFoot.Position = {};
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

GameId BuildGameId(uint32_t aModId, uint32_t aBaseId)
{
    GameId result{};
    result.ModId = aModId;
    result.BaseId = aBaseId;
    return result;
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

    state.LastEventValid = true;
    state.LastEvent.Sequence = 3;
    state.LastEvent.EventKind = VRHiggsEventSnapshot::Kind::kGrabbed;
    state.LastEvent.HasHand = true;
    state.LastEvent.IsLeft = true;
    state.LastEvent.ObjectId = BuildGameId(25, 0xBBBB);
    state.LastEvent.Mass = 5.5f;
    state.LastEvent.SeparatingVelocity = 12.25f;
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
            pose.Body.FormatVersion = 1;
            THEN("the zero invalid state is accepted")
            {
                REQUIRE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE(IsVRPoseUpdateSafe(pose));
            }
        }

        WHEN("the body format is unknown")
        {
            pose.Body = {};
            pose.Body.FormatVersion = 2;
            THEN("the fixed-order schema fails closed")
            {
                REQUIRE_FALSE(IsVRBodyPoseDataSafe(pose.Body));
                REQUIRE_FALSE(IsVRPoseUpdateSafe(pose));
            }
        }
    }
}

TEST_CASE("Differential structures", "[encoding.differential]")
{
    GIVEN("Full ActionEvent")
    {
        ActionEvent sendAction, recvAction;

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
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

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
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

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AuthenticationResponse")
    {
        Buffer buff(1000);

        AuthenticationResponse sendMessage, recvMessage;
        sendMessage.Type = AuthenticationResponse::ResponseType::kAccepted;
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

        REQUIRE(sendMessage == recvMessage);
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
        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
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
