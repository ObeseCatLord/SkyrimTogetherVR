#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "VRAnimationGraphProtocol.h"

namespace SkyrimTogetherVR::GameplayBridge
{
// This mapping is shared only by modules in one 64-bit Windows process. Its
// records deliberately contain no native pointers, C++ ownership, callbacks,
// game types, or variable-sized data.
inline constexpr wchar_t kMappingHandleEnvironment[] = L"STVR_GAMEPLAY_BRIDGE_HANDLE";
inline constexpr std::uint32_t kMappingMagic = 0x42564753; // SGVB
inline constexpr std::uint16_t kMappingAbiVersion = 12;
inline constexpr std::uint32_t kCapabilityRevision = 20;
// SkyrimVR.exe reports file version 1.4.15.0, which is also the version used
// by the VR Address Library filename and CommonLib's executable detection.
inline constexpr std::uint32_t kSkyrimVrRuntimeVersion = 0x010400F0;
// SKSEVR 2.0.12 deliberately publishes 1.4.15.1 through SKSEInterface. Do not
// use CommonLib's executable-version constant to validate that interface.
inline constexpr std::uint32_t kSkseVrInterfaceRuntimeVersion = 0x010400F1;
inline constexpr std::uint32_t kMinimumSkseVrVersion = 0x020000C0; // 2.0.12
inline constexpr std::uint32_t kMinimumSkseVrReleaseIndex = 60;
inline constexpr std::uint16_t kFixedPayloadBytes = 80;
inline constexpr std::uint16_t kGameplayTextBytesPerChunk = 44;
inline constexpr std::uint16_t kMaximumGameplayTextChunks = 16;
inline constexpr float kMaximumProjectileCoordinate = 1000000.0F;
inline constexpr float kMaximumProjectileAngle = 6.2831855F;
inline constexpr std::int32_t kMaximumProjectileArea = 100000;
inline constexpr float kMaximumProjectilePower = 100000.0F;
inline constexpr float kMaximumProjectileScale = 1000.0F;
// A complete bounded object-cell snapshot can contain up to 1536 records. Keep
// enough event capacity for that transaction plus normal local observations;
// command traffic must also hold one complete authoritative container reply.
inline constexpr std::uint32_t kDefaultEventRingCapacity = 2048;
inline constexpr std::uint32_t kDefaultCommandRingCapacity = 1024;

enum class EndpointState : std::uint32_t
{
    Uninitialized = 0,
    Prepared = 1,
    Ready = 2,
    Retiring = 3,
    Retired = 4,
    Faulted = 5,
};

enum class Capability : std::uint64_t
{
    Lifecycle = 1ull << 0,
    LocalPlayerDiscovery = 1ull << 1,
    LocalPlayerSnapshot = 1ull << 2,
    RemoteAvatarLifecycle = 1ull << 3,
    RemoteRootTransform = 1ull << 4,
    RemoteSpatialTransfer = 1ull << 5,
    LocalAnimationGraphSnapshot = 1ull << 6,
    RemoteAnimationGraphSnapshot = 1ull << 7,
    AnimationEvents = 1ull << 8,
    Appearance = 1ull << 9,
    EquipmentAndInventory = 1ull << 10,
    ActorState = 1ull << 11,
    WorldReferences = 1ull << 12,
    CombatAndMagic = 1ull << 13,
    QuestAndDialogue = 1ull << 14,
    WorldState = 1ull << 15,
    VrBodyPose = 1ull << 16,
    HiggsInteraction = 1ull << 17,
    PlanckInteraction = 1ull << 18,
    NpcOwnership = 1ull << 19,
    ExactAnimationActions = 1ull << 20,
};

using CapabilityMask = std::uint64_t;

inline constexpr CapabilityMask kInitialCapabilities =
    static_cast<CapabilityMask>(Capability::Lifecycle) |
    static_cast<CapabilityMask>(Capability::LocalPlayerDiscovery) |
    static_cast<CapabilityMask>(Capability::LocalPlayerSnapshot) |
    static_cast<CapabilityMask>(Capability::RemoteAvatarLifecycle) |
    static_cast<CapabilityMask>(Capability::RemoteRootTransform) |
    static_cast<CapabilityMask>(Capability::RemoteSpatialTransfer) |
    static_cast<CapabilityMask>(Capability::LocalAnimationGraphSnapshot) |
    static_cast<CapabilityMask>(Capability::RemoteAnimationGraphSnapshot) |
    static_cast<CapabilityMask>(Capability::AnimationEvents) |
    static_cast<CapabilityMask>(Capability::Appearance) |
    static_cast<CapabilityMask>(Capability::EquipmentAndInventory) |
    static_cast<CapabilityMask>(Capability::ActorState) |
    static_cast<CapabilityMask>(Capability::WorldReferences) |
    static_cast<CapabilityMask>(Capability::CombatAndMagic) |
    static_cast<CapabilityMask>(Capability::QuestAndDialogue) |
    static_cast<CapabilityMask>(Capability::WorldState) |
    static_cast<CapabilityMask>(Capability::VrBodyPose) |
    static_cast<CapabilityMask>(Capability::HiggsInteraction) |
    static_cast<CapabilityMask>(Capability::PlanckInteraction) |
    static_cast<CapabilityMask>(Capability::NpcOwnership) |
    static_cast<CapabilityMask>(Capability::ExactAnimationActions);

[[nodiscard]] constexpr bool HasCapability(const CapabilityMask a_mask, const Capability a_capability) noexcept
{
    return (a_mask & static_cast<CapabilityMask>(a_capability)) != 0;
}

// The adapter owns token allocation and validates the token against the
// identity epoch and generation before resolving it to a native actor.
struct AdapterHandle
{
    std::uint64_t Value;
};

// Zero is invalid. One is reserved for the process-lifetime local player token;
// remote avatar handles are allocated by the adapter from a separate range.
inline constexpr AdapterHandle kLocalPlayerHandle{1};
inline constexpr std::uint64_t kFirstRemoteAvatarHandle = 2;

// All state-changing bridge work is bound to these identities. Future
// protocol-specific fields may be appended only in a new ABI version.
struct BridgeIdentity
{
    std::uint64_t ServerInstanceNonce;
    std::uint64_t ConnectionGeneration;
    std::uint64_t LifecycleEpoch;
    std::uint64_t EntityId;
    std::uint32_t EntityGeneration;
    std::uint32_t Reserved0;
    std::uint64_t SequenceId;
    std::uint64_t ActionId;
};

struct MessageHeader
{
    std::uint16_t Kind;
    std::uint16_t PayloadSize;
    std::uint32_t Flags;
    BridgeIdentity Identity;
};

struct RootTransform
{
    float PositionX;
    float PositionY;
    float PositionZ;
    float RotationX;
    float RotationY;
    float RotationZ;
    float RotationW;
    float Scale;
};

enum class EventKind : std::uint16_t
{
    Lifecycle = 1,
    LocalPlayerState = 2,
    RemoteAvatarState = 3,
    LocalAnimationGraphChunk = 4,
    RemoteAnimationGraphState = 5,
    RemoteSpatialTransferState = 6,
    LocalGameplayAction = 7,
    RemoteGameplayActionState = 8,
    LocalGameplayTextChunk = 9,
    LocalProjectileLaunch = 10,
    LocalActorActionMetadata = 11,
    LocalActorActionGraphChunk = 12,
    LocalActorActionTextChunk = 13,
};

enum class GameplayDomain : std::uint16_t
{
    Animation = 1,
    Appearance = 2,
    Equipment = 3,
    Inventory = 4,
    ActorState = 5,
    Object = 6,
    Combat = 7,
    Projectile = 8,
    Magic = 9,
    Quest = 10,
    Dialogue = 11,
    Party = 12,
    WorldState = 13,
    VrBodyPose = 14,
    Higgs = 15,
    Planck = 16,
    NpcOwnership = 17,
};

// Domain-specific actions use this stable opcode space. Fields that are not
// meaningful for an action must be zero so malformed or version-skewed records
// are rejected before touching the game.
//
// Local SetLockState records use TargetLocalFormId for the local player bridge
// identity, LocalFormIdA for the changed reference, LocalFormIdB for its parent
// cell, ValueA for the locked boolean, and ValueB for the lock level when
// locked (zero when unlocked).  This is deliberately distinct from the
// inbound object-application schema, whose target is the changed reference.
enum class GameplayAction : std::uint16_t
{
    AnimationEvent = 1,
    DrawWeapon = 2,
    SetRace = 3,
    SetSex = 4,
    SetWeight = 5,
    SetName = 6,
    SetHeadPart = 7,
    SetTint = 8,
    EquipForm = 9,
    UnequipForm = 10,
    InventoryDelta = 11,
    SetActorValue = 12,
    SetActorMaximum = 13,
    SetDeathState = 14,
    Respawn = 15,
    Mount = 16,
    Activate = 17,
    SetLockState = 18,
    SetOpenState = 19,
    SetOwnership = 20,
    ScriptAnimation = 21,
    MeleeHit = 22,
    SetCombatTarget = 23,
    LaunchProjectile = 24,
    CastSpell = 25,
    InterruptCast = 26,
    ApplyMagicEffect = 27,
    AddSpell = 28,
    RemoveSpell = 29,
    SetQuestState = 30,
    SetQuestStage = 31,
    Dialogue = 32,
    Subtitle = 33,
    Package = 34,
    SetWaypoint = 35,
    RemoveWaypoint = 36,
    Teleport = 37,
    SetCalendar = 38,
    SetWeather = 39,
    VrPoseChunk = 40,
    VrCalibration = 41,
    HiggsGrab = 42,
    HiggsPull = 43,
    HiggsDrop = 44,
    HiggsStash = 45,
    HiggsConsume = 46,
    PlanckHit = 47,
    PlanckGrab = 48,
    PlanckRagdoll = 49,
    InventoryReset = 50,
    SetLevel = 51,
    SetEssential = 52,
    SetFactionRank = 53,
    ResetFactions = 54,
    ModifyActorValue = 55,
    ObjectSnapshotBegin = 56,
    ObjectSnapshotItem = 57,
    ObjectSnapshotEnd = 58,
    NpcSnapshotBegin = 59,
    NpcSnapshotValue = 60,
    NpcSnapshotItem = 61,
    NpcSnapshotFaction = 62,
    NpcSnapshotEnd = 63,
    StartNpcObservation = 64,
    StopNpcObservation = 65,
    ApplyServerSettings = 66,
    ReleaseWeather = 67,
    EquipmentSnapshotBegin = 68,
    EquipmentSnapshotItem = 69,
    EquipmentSnapshotEnd = 70,
    SyncExperience = 71,
    ActorAction = 72,
    ArmLocalCapture = 73,
};

[[nodiscard]] constexpr bool IsSupportedLegacyAnimationEvent(const std::string_view aEvent) noexcept
{
    constexpr std::string_view allowed[]{
        "IdleForceDefaultState", "IdleReturnToDefault", "ReturnDefaultState", "ReturnToDefault",
        "ForceFurnExit", "IdleStop", "IdleStopInstant", "GetUpBegin", "JumpUp", "JumpDown",
        "JumpLand", "SneakStart", "SneakStop", "SprintStart", "SprintStop", "Ragdoll", "GetUpEnd",
        "ChairEnter", "ChairExit", "HorseEnter", "HorseExit", "weaponDraw", "weaponSheathe",
    };
    for (const auto event : allowed) {
        if (event == aEvent)
            return true;
    }
    return false;
}

inline constexpr std::uint32_t kFirstSkillActorValue = 6;
inline constexpr std::uint32_t kLastSkillActorValue = 23;
inline constexpr float kMaximumSyncedExperience = 100000.0F;

[[nodiscard]] constexpr bool IsCombatSkillActorValue(const std::uint32_t a_actorValue) noexcept
{
    return a_actorValue == 6 || a_actorValue == 7 || a_actorValue == 8 || a_actorValue == 9 ||
           a_actorValue == 18 || a_actorValue == 19 || a_actorValue == 20 ||
           a_actorValue == 21 || a_actorValue == 22;
}

// InventoryDelta flags are shared by local capture and remote application.
// Quest-item metadata is advisory; Drop requests the original client's
// non-authoritative local drop visual for a negative actor inventory delta.
inline constexpr std::uint32_t kInventoryQuestItem = 1u << 0;
inline constexpr std::uint32_t kInventoryDrop = 1u << 1;
inline constexpr std::uint32_t kInventorySnapshotEntry = 1u << 2;
inline constexpr std::uint32_t kObjectSnapshotContainer = 1u << 0;
inline constexpr std::uint32_t kObjectSnapshotPlayerHome = 1u << 1;
inline constexpr std::uint32_t kNpcSnapshotDead = 1u << 0;
inline constexpr std::uint32_t kNpcSnapshotWeaponDrawn = 1u << 1;
inline constexpr std::uint32_t kEquipmentSnapshotWorn = 1u << 0;
inline constexpr std::uint32_t kEquipmentSnapshotWornLeft = 1u << 1;
inline constexpr std::uint32_t kEquipmentSnapshotWeapon = 1u << 2;
inline constexpr std::uint32_t kEquipmentSnapshotAmmo = 1u << 3;
// Skyrim VR can safely refresh the skin-tone mask through the public
// CommonLib surface. Other FaceGen tint types remain fail-closed.
inline constexpr std::uint32_t kSupportedSkinTintType = 6;
// SetCalendar retains the local date while still applying server time and
// timescale when SyncPlayerCalendar is disabled.
inline constexpr std::uint32_t kPreserveCalendarDate = 1u << 0;
// ApplyServerSettings keeps the original Skyrim Together encounter policy:
// encounters are disabled while connected unless this client leads its party.
inline constexpr std::uint32_t kWorldEncountersEnabled = 1u << 0;

// VrPoseChunk uses one position/scale record followed by three basis records.
// ValueA is the source pose sequence and bits 8..15 identify the node.
enum class GameplayPoseNode : std::uint8_t
{
    Hmd = 0,
    LeftHand = 1,
    RightHand = 2,
    Pelvis = 3,
    LeftThigh = 4,
    LeftCalf = 5,
    LeftFoot = 6,
    RightThigh = 7,
    RightCalf = 8,
    RightFoot = 9,
    Count,
};

inline constexpr std::uint32_t kPoseChunkPresent = 1u << 0;
inline constexpr std::uint32_t kPoseChunkBasis = 1u << 1;
inline constexpr std::uint32_t kPoseChunkAxisShift = 2;
inline constexpr std::uint32_t kPoseChunkAxisMask = 0x3u << kPoseChunkAxisShift;
inline constexpr std::uint32_t kPoseChunkNodeShift = 8;
inline constexpr std::uint32_t kPoseChunkNodeMask = 0xFFu << kPoseChunkNodeShift;

[[nodiscard]] constexpr Capability CapabilityForDomain(const GameplayDomain a_domain) noexcept
{
    switch (a_domain)
    {
    case GameplayDomain::Animation:
        return Capability::AnimationEvents;
    case GameplayDomain::Appearance:
        return Capability::Appearance;
    case GameplayDomain::Equipment:
    case GameplayDomain::Inventory:
        return Capability::EquipmentAndInventory;
    case GameplayDomain::ActorState:
        return Capability::ActorState;
    case GameplayDomain::Object:
        return Capability::WorldReferences;
    case GameplayDomain::Combat:
    case GameplayDomain::Projectile:
    case GameplayDomain::Magic:
        return Capability::CombatAndMagic;
    case GameplayDomain::Quest:
    case GameplayDomain::Dialogue:
    case GameplayDomain::Party:
        return Capability::QuestAndDialogue;
    case GameplayDomain::WorldState:
        return Capability::WorldState;
    case GameplayDomain::VrBodyPose:
        return Capability::VrBodyPose;
    case GameplayDomain::Higgs:
        return Capability::HiggsInteraction;
    case GameplayDomain::Planck:
        return Capability::PlanckInteraction;
    case GameplayDomain::NpcOwnership:
        return Capability::NpcOwnership;
    default:
        return static_cast<Capability>(0);
    }
}

[[nodiscard]] constexpr bool IsActionInDomain(
    const GameplayDomain a_domain,
    const GameplayAction a_action) noexcept
{
    const auto action = static_cast<std::uint16_t>(a_action);
    switch (a_domain)
    {
    case GameplayDomain::Animation:
        return (action >= 1 && action <= 2) || action == 72;
    case GameplayDomain::Appearance:
        return action >= 3 && action <= 8;
    case GameplayDomain::Equipment:
        return (action >= 9 && action <= 10) || (action >= 68 && action <= 70);
    case GameplayDomain::Inventory:
        return action == 11 || action == 50;
    case GameplayDomain::ActorState:
        return (action >= 12 && action <= 16) || (action >= 51 && action <= 55) || action == 71 || action == 73;
    case GameplayDomain::Object:
        return (action >= 17 && action <= 21) || (action >= 56 && action <= 58);
    case GameplayDomain::Combat:
        return action >= 22 && action <= 23;
    case GameplayDomain::Projectile:
        return action == 24;
    case GameplayDomain::Magic:
        return action >= 25 && action <= 29;
    case GameplayDomain::Quest:
        return action >= 30 && action <= 31;
    case GameplayDomain::Dialogue:
        return action >= 32 && action <= 34;
    case GameplayDomain::Party:
        return action >= 35 && action <= 36;
    case GameplayDomain::WorldState:
        return (action >= 37 && action <= 39) || action == 66 || action == 67;
    case GameplayDomain::VrBodyPose:
        return action >= 40 && action <= 41;
    case GameplayDomain::Higgs:
        return action >= 42 && action <= 46;
    case GameplayDomain::Planck:
        return action >= 47 && action <= 49;
    case GameplayDomain::NpcOwnership:
        return action >= 59 && action <= 65;
    default:
        return false;
    }
}

enum class LifecycleState : std::uint32_t
{
    PluginLoaded = 1,
    DataLoaded = 2,
    NewGame = 3,
    PreLoadGame = 4,
    PostLoadGame = 5,
    CellChanged = 6,
    EpochRetired = 7,
};

enum class EpochRetireReason : std::uint32_t
{
    Disconnect = 1,
    LifecycleReset = 2,
    Shutdown = 3,
};

enum class CommandStatus : std::uint32_t
{
    Success = 0,
    Inactive = 1,
    Unsupported = 2,
    Malformed = 3,
    WrongThread = 4,
    StaleSession = 5,
    StaleEpoch = 6,
    StaleEntity = 7,
    InvalidHandle = 8,
    MissingForm = 9,
    MissingCell = 10,
    EngineRejected = 11,
    QueueOverflow = 12,
};

enum class CommandPumpResult : std::uint32_t
{
    Success = 0,
    Inactive = 1,
    AbiMismatch = 2,
    WrongProcess = 3,
    WrongThread = 4,
    StaleEpoch = 5,
    Faulted = 6,
};

enum class RemoteAvatarState : std::uint32_t
{
    Created = 1,
    Destroyed = 2,
    Rejected = 3,
    Faulted = 4,
};

struct LifecycleEventPayload
{
    std::uint32_t ObservedState;
    std::uint32_t Reason;
    std::uint64_t ObservedLifecycleEpoch;
    CapabilityMask AvailableCapabilities;
    std::uint8_t Reserved[56];
};

struct LocalPlayerStatePayload
{
    AdapterHandle LocalPlayerHandle;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    RootTransform Root;
    std::uint64_t SnapshotFlags;
    std::uint32_t LocalActorBaseFormId;
    std::uint8_t Reserved[20];
};

// This result is how the mapped client receives an adapter-issued handle for
// subsequent remote-avatar update and destroy commands.
struct RemoteAvatarStatePayload
{
    AdapterHandle AvatarHandle;
    std::uint32_t State;
    std::uint32_t Status;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    std::uint32_t LocalActorReferenceFormId;
    RootTransform Root;
    std::uint8_t Reserved[20];
};

struct AnimationGraphChunkPayload
{
    AdapterHandle AvatarHandle;
    std::uint64_t SnapshotId;
    std::uint16_t DescriptorVersion;
    std::uint16_t ValueType;
    std::uint16_t StartIndex;
    std::uint16_t ValueCount;
    std::uint16_t TotalCount;
    std::uint16_t Reserved0;
    std::uint32_t ChunkFlags;
    float Direction;
    std::uint32_t Values[AnimationGraphProtocol::kValuesPerChunk];
    std::uint8_t ReservedTail[16];
};

enum class RemoteAnimationGraphState : std::uint32_t
{
    Applied = 1,
    Rejected = 2,
    Faulted = 3,
};

struct RemoteAnimationGraphStatePayload
{
    AdapterHandle AvatarHandle;
    std::uint64_t SnapshotId;
    std::uint32_t State;
    std::uint32_t Status;
    std::uint8_t Reserved[56];
};

struct RemoteSpatialTransferStatePayload
{
    AdapterHandle AvatarHandle;
    std::uint32_t SourceCellFormId;
    std::uint32_t SourceWorldspaceFormId;
    std::uint32_t TargetCellFormId;
    std::uint32_t TargetWorldspaceFormId;
    std::uint32_t Status;
    std::uint8_t Reserved[52];
};

struct GameplayActionPayload
{
    AdapterHandle TargetHandle;
    AdapterHandle SecondaryHandle;
    std::uint32_t TargetLocalFormId;
    std::uint16_t Domain;
    std::uint16_t Action;
    std::uint32_t LocalFormIdA;
    std::uint32_t LocalFormIdB;
    std::uint32_t LocalFormIdC;
    std::uint32_t LocalFormIdD;
    std::int32_t ValueA;
    std::int32_t ValueB;
    float ScalarA;
    float ScalarB;
    float ScalarC;
    float ScalarD;
    std::uint32_t ActionFlags;
    std::uint32_t Reserved0;
    std::uint8_t ReservedTail[8];
};

struct GameplayActionStatePayload
{
    AdapterHandle TargetHandle;
    std::uint16_t Domain;
    std::uint16_t Action;
    std::uint32_t Status;
    std::uint32_t TargetLocalFormId;
    std::uint8_t Reserved[60];
};

struct GameplayTextChunkPayload
{
    AdapterHandle TargetHandle;
    std::uint32_t TargetLocalFormId;
    std::uint16_t Domain;
    std::uint16_t Action;
    std::uint64_t TextId;
    std::uint16_t ChunkIndex;
    std::uint16_t ChunkCount;
    std::uint16_t ByteCount;
    std::uint16_t Reserved0;
    std::uint32_t AuxiliaryLocalFormId;
    char Utf8Bytes[kGameplayTextBytesPerChunk];
};

// Fixed-width representation of the original projectile-launch wire. YAngle
// and UnkBool1 have no semantic CommonLib LaunchData field; UnkBool2 maps to
// ChainShatter.
enum ProjectileLaunchFlag : std::uint32_t
{
    ProjectileAlwaysHit = 1u << 0,
    ProjectileNoDamageOutsideCombat = 1u << 1,
    ProjectileAutoAim = 1u << 2,
    ProjectileChainShatter = 1u << 3,
    ProjectileDeferInitialization = 1u << 4,
    ProjectileForceConeOfFire = 1u << 5,
};

inline constexpr std::uint32_t kProjectileLaunchKnownFlags = ProjectileAlwaysHit | ProjectileNoDamageOutsideCombat |
                                                             ProjectileAutoAim | ProjectileChainShatter |
                                                             ProjectileDeferInitialization | ProjectileForceConeOfFire;

struct ApplyProjectileLaunchPayload
{
    AdapterHandle TargetHandle;
    std::uint32_t LocalProjectileBaseFormId;
    std::uint32_t LocalWeaponFormId;
    std::uint32_t LocalAmmoFormId;
    std::uint32_t LocalSpellFormId;
    std::uint32_t LocalParentCellFormId;
    float OriginX;
    float OriginY;
    float OriginZ;
    float AngleX;
    float AngleZ;
    float Power;
    float Scale;
    std::int32_t CastingSource;
    std::int32_t Area;
    std::uint32_t LaunchFlags;
    // Producer events carry the local NPC reference here when TargetHandle is
    // zero. Inbound commands resolve their shooter through TargetHandle and
    // must leave this field zero.
    std::uint32_t LocalShooterFormId;
    std::uint8_t ReservedTail[8];
};

// Exact ActorMediator actions are transferred as a bounded transaction. Graph
// and text chunks use the same Identity.ActionId, SnapshotId, and TextId; the
// metadata record/command is published last and commits the transaction.
struct ActorActionPayload
{
    AdapterHandle TargetHandle;
    std::uint32_t ActorLocalFormId;
    std::uint32_t ActionLocalFormId;
    std::uint32_t ActionTargetLocalFormId;
    std::uint32_t IdleLocalFormId;
    std::uint32_t State1;
    std::uint32_t State2;
    std::uint32_t Type;
    std::uint32_t ActionFlags;
    std::uint64_t SnapshotId;
    std::uint64_t TextId;
    std::uint8_t Reserved[24];
};

struct ActorActionGraphChunkPayload
{
    AdapterHandle TargetHandle;
    std::uint32_t ActorLocalFormId;
    std::uint32_t Reserved0;
    std::uint64_t SnapshotId;
    std::uint16_t DescriptorVersion;
    std::uint16_t ValueType;
    std::uint16_t StartIndex;
    std::uint16_t ValueCount;
    std::uint16_t TotalCount;
    std::uint16_t Reserved1;
    std::uint32_t ChunkFlags;
    float Direction;
    std::uint32_t Values[AnimationGraphProtocol::kValuesPerChunk];
    std::uint8_t ReservedTail[4];
};

union EventPayload
{
    LifecycleEventPayload Lifecycle;
    LocalPlayerStatePayload LocalPlayerState;
    RemoteAvatarStatePayload RemoteAvatarState;
    AnimationGraphChunkPayload LocalAnimationGraphChunk;
    RemoteAnimationGraphStatePayload RemoteAnimationGraphState;
    RemoteSpatialTransferStatePayload RemoteSpatialTransferState;
    GameplayActionPayload LocalGameplayAction;
    GameplayActionStatePayload RemoteGameplayActionState;
    GameplayTextChunkPayload LocalGameplayTextChunk;
    ApplyProjectileLaunchPayload LocalProjectileLaunch;
    ActorActionPayload LocalActorActionMetadata;
    ActorActionGraphChunkPayload LocalActorActionGraphChunk;
    GameplayTextChunkPayload LocalActorActionTextChunk;
    std::uint8_t Bytes[kFixedPayloadBytes];
};

struct alignas(8) EventRecord
{
    MessageHeader Header;
    EventPayload Payload;
};

enum class CommandKind : std::uint16_t
{
    CreateRemoteAvatar = 1,
    DestroyRemoteAvatar = 2,
    UpdateRemoteRootTransform = 3,
    RetireEpoch = 4,
    ApplyRemoteAnimationGraphChunk = 5,
    ApplyGameplayAction = 6,
    ApplyGameplayTextChunk = 7,
    ApplyProjectileLaunch = 8,
    StageActorActionGraphChunk = 9,
    StageActorActionTextChunk = 10,
    ApplyActorAction = 11,
};

struct CreateRemoteAvatarPayload
{
    // Form IDs are translated into this client's local load order before the
    // command is submitted. The adapter never interprets server mod IDs.
    std::uint32_t LocalActorBaseFormId;
    std::uint32_t CreateFlags;
    std::uint32_t LocalReferenceFormId;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    RootTransform InitialRoot;
    std::uint8_t Reserved[28];
};

enum RemoteAvatarCreateFlag : std::uint32_t
{
    UseExistingReference = 1u << 0,
    PlayerAvatar = 1u << 1,
};

struct DestroyRemoteAvatarPayload
{
    AdapterHandle AvatarHandle;
    std::uint32_t DestroyReason;
    std::uint32_t DestroyFlags;
    std::uint8_t Reserved[64];
};

struct UpdateRemoteRootTransformPayload
{
    AdapterHandle AvatarHandle;
    RootTransform Root;
    std::uint32_t UpdateFlags;
    std::uint32_t LocalCellFormId;
    std::uint32_t LocalWorldspaceFormId;
    std::uint8_t Reserved[28];
};

enum RootTransformUpdateFlag : std::uint32_t
{
    SpatialTransfer = 1u << 0,
};

struct RetireEpochPayload
{
    std::uint64_t RetiredLifecycleEpoch;
    std::uint32_t Reason;
    std::uint32_t RetireFlags;
    std::uint8_t Reserved[64];
};

union CommandPayload
{
    CreateRemoteAvatarPayload CreateRemoteAvatar;
    DestroyRemoteAvatarPayload DestroyRemoteAvatar;
    UpdateRemoteRootTransformPayload UpdateRemoteRootTransform;
    RetireEpochPayload RetireEpoch;
    AnimationGraphChunkPayload ApplyRemoteAnimationGraphChunk;
    GameplayActionPayload ApplyGameplayAction;
    GameplayTextChunkPayload ApplyGameplayTextChunk;
    ApplyProjectileLaunchPayload ApplyProjectileLaunch;
    ActorActionGraphChunkPayload StageActorActionGraphChunk;
    GameplayTextChunkPayload StageActorActionTextChunk;
    ActorActionPayload ApplyActorAction;
    std::uint8_t Bytes[kFixedPayloadBytes];
};

struct alignas(8) CommandRecord
{
    MessageHeader Header;
    CommandPayload Payload;
};

using AtomicU32 = std::atomic<std::uint32_t>;
using AtomicU64 = std::atomic<std::uint64_t>;

// The creator placement-constructs the mapping, initializes this header and
// both rings, then publishes EndpointState::Prepared. Ready is published only
// after both owner thread IDs and capabilities are valid. Retiring prevents new
// work; Retired makes all remaining records stale through LifecycleEpoch
// validation.
struct alignas(8) MappingHeader
{
    std::uint32_t Magic;
    std::uint16_t AbiVersion;
    std::uint16_t HeaderSize;
    std::uint32_t MappingSize;
    std::uint32_t PublisherProcessId;
    std::uint32_t RuntimeVersion;
    std::uint32_t CapabilityRevision;
    AtomicU32 State;
    std::uint32_t Reserved0;
    AtomicU64 RequestedCapabilities;
    AtomicU64 AvailableCapabilities;
    AtomicU64 ActiveCapabilities;
    AtomicU64 ServerInstanceNonce;
    AtomicU64 ConnectionGeneration;
    AtomicU64 LifecycleEpoch;
    AtomicU64 EventConsumerThreadId;
    AtomicU64 CommandExecutionThreadId;
    AtomicU64 ProducedEventCount;
    AtomicU64 ConsumedEventCount;
    AtomicU64 SubmittedCommandCount;
    AtomicU64 ExecutedCommandCount;
    AtomicU64 RejectedCommandCount;
    AtomicU64 StaleCommandCount;
};

template <class T>
struct alignas(8) RingSlot
{
    AtomicU64 Sequence;
    T Record;
};

// A bounded, lock-free multi-producer/single-consumer queue using one
// monotonically increasing sequence per slot. Any callback thread may push
// events, and the mapped client owner may push commands. Event consumption is
// restricted to EventConsumerThreadId and command execution to
// CommandExecutionThreadId; this single-consumer contract preserves the order
// of transaction records published by TryPushBatch. InitializeRing is
// exclusive: no producer or consumer may touch a ring until it returns, nor
// while an endpoint is retired/reset.
template <class T, std::size_t Capacity>
struct alignas(8) BoundedMpmcRing
{
    static_assert(Capacity >= 2);
    static_assert((Capacity & (Capacity - 1)) == 0);
    static_assert(std::is_standard_layout_v<T>);
    static_assert(std::is_trivially_copyable_v<T>);

    AtomicU64 EnqueuePosition;
    AtomicU64 DequeuePosition;
    AtomicU64 DroppedPushCount;
    AtomicU64 EmptyPopCount;
    RingSlot<T> Slots[Capacity];
};

template <class T, std::size_t Capacity>
inline void InitializeRing(BoundedMpmcRing<T, Capacity>& a_ring) noexcept
{
    a_ring.EnqueuePosition.store(0, std::memory_order_relaxed);
    a_ring.DequeuePosition.store(0, std::memory_order_relaxed);
    a_ring.DroppedPushCount.store(0, std::memory_order_relaxed);
    a_ring.EmptyPopCount.store(0, std::memory_order_relaxed);

    for (std::size_t i = 0; i < Capacity; ++i)
        a_ring.Slots[i].Sequence.store(i, std::memory_order_relaxed);
}

template <class T, std::size_t Capacity>
[[nodiscard]] inline bool TryPush(BoundedMpmcRing<T, Capacity>& a_ring, const T& a_record) noexcept
{
    std::uint64_t position = a_ring.EnqueuePosition.load(std::memory_order_relaxed);
    RingSlot<T>* pSlot{};

    for (;;)
    {
        pSlot = &a_ring.Slots[position & (Capacity - 1)];
        const auto sequence = pSlot->Sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::int64_t>(sequence - position);

        if (difference == 0)
        {
            if (a_ring.EnqueuePosition.compare_exchange_weak(
                    position, position + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                break;
        }
        else if (difference < 0)
        {
            a_ring.DroppedPushCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        else
            position = a_ring.EnqueuePosition.load(std::memory_order_relaxed);
    }

    pSlot->Record = a_record;
    pSlot->Sequence.store(position + 1, std::memory_order_release);
    return true;
}

// Reserve one contiguous range so records in a logical transaction cannot be
// interleaved with another producer. Records must be ordered so the final item
// is the transaction's commit record; consumers cannot reach it until every
// preceding slot has been published.
template <class T, std::size_t Capacity>
[[nodiscard]] inline bool TryPushBatch(
    BoundedMpmcRing<T, Capacity>& a_ring, const T* ap_records, const std::size_t a_count) noexcept
{
    if (!ap_records || a_count == 0 || a_count > Capacity)
    {
        a_ring.DroppedPushCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::uint64_t position = a_ring.EnqueuePosition.load(std::memory_order_relaxed);
    for (;;)
    {
        bool rangeAvailable = true;
        for (std::size_t index = 0; index < a_count; ++index)
        {
            const auto slotPosition = position + index;
            const auto& slot = a_ring.Slots[slotPosition & (Capacity - 1)];
            const auto sequence = slot.Sequence.load(std::memory_order_acquire);
            if (static_cast<std::int64_t>(sequence - slotPosition) != 0)
            {
                rangeAvailable = false;
                break;
            }
        }

        if (!rangeAvailable)
        {
            const auto current = a_ring.EnqueuePosition.load(std::memory_order_relaxed);
            if (current != position)
            {
                position = current;
                continue;
            }
            a_ring.DroppedPushCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (a_ring.EnqueuePosition.compare_exchange_weak(
                position, position + a_count, std::memory_order_relaxed, std::memory_order_relaxed))
            break;
    }

    for (std::size_t index = 0; index < a_count; ++index)
    {
        const auto slotPosition = position + index;
        auto& slot = a_ring.Slots[slotPosition & (Capacity - 1)];
        slot.Record = ap_records[index];
        slot.Sequence.store(slotPosition + 1, std::memory_order_release);
    }
    return true;
}

template <class T, std::size_t Capacity>
[[nodiscard]] inline bool TryPop(BoundedMpmcRing<T, Capacity>& a_ring, T& a_record) noexcept
{
    std::uint64_t position = a_ring.DequeuePosition.load(std::memory_order_relaxed);
    RingSlot<T>* pSlot{};

    for (;;)
    {
        pSlot = &a_ring.Slots[position & (Capacity - 1)];
        const auto sequence = pSlot->Sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::int64_t>(sequence - (position + 1));

        if (difference == 0)
        {
            if (a_ring.DequeuePosition.compare_exchange_weak(
                    position, position + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                break;
        }
        else if (difference < 0)
        {
            a_ring.EmptyPopCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        else
            position = a_ring.DequeuePosition.load(std::memory_order_relaxed);
    }

    a_record = pSlot->Record;
    pSlot->Sequence.store(position + Capacity, std::memory_order_release);
    return true;
}

using EventRing = BoundedMpmcRing<EventRecord, kDefaultEventRingCapacity>;
using CommandRing = BoundedMpmcRing<CommandRecord, kDefaultCommandRingCapacity>;

struct alignas(8) GameplayBridgeMapping
{
    MappingHeader Header;
    EventRing Events;
    CommandRing Commands;
};

static_assert(sizeof(std::uint64_t) == 8);
static_assert(sizeof(float) == 4);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(sizeof(AtomicU32) == 4);
static_assert(sizeof(AtomicU64) == 8);
static_assert(alignof(AtomicU32) == 4);
static_assert(alignof(AtomicU64) == 8);
static_assert(AtomicU32::is_always_lock_free);
static_assert(AtomicU64::is_always_lock_free);
static_assert(std::is_standard_layout_v<AtomicU32>);
static_assert(std::is_standard_layout_v<AtomicU64>);

// The mapped synchronization objects are placement-constructed once and are
// never copied. Their cross-view contract is deliberately pinned to Windows
// x64/MSVC and guarded by the lock-free, size, alignment, and layout checks
// below; MSVC correctly does not classify std::atomic as trivially copyable.

static_assert(std::is_standard_layout_v<AdapterHandle>);
static_assert(std::is_trivially_copyable_v<AdapterHandle>);
static_assert(sizeof(AdapterHandle) == 0x08);
static_assert(std::is_standard_layout_v<BridgeIdentity>);
static_assert(std::is_trivially_copyable_v<BridgeIdentity>);
static_assert(sizeof(BridgeIdentity) == 0x38);
static_assert(offsetof(BridgeIdentity, LifecycleEpoch) == 0x10);
static_assert(offsetof(BridgeIdentity, EntityId) == 0x18);
static_assert(offsetof(BridgeIdentity, SequenceId) == 0x28);
static_assert(offsetof(BridgeIdentity, ActionId) == 0x30);
static_assert(sizeof(MessageHeader) == 0x40);
static_assert(alignof(MessageHeader) == 8);
static_assert(offsetof(MessageHeader, Identity) == 0x08);
static_assert(sizeof(RootTransform) == 0x20);
static_assert(alignof(RootTransform) == 4);
static_assert(std::is_standard_layout_v<LifecycleEventPayload>);
static_assert(std::is_trivially_copyable_v<LifecycleEventPayload>);
static_assert(std::is_standard_layout_v<LocalPlayerStatePayload>);
static_assert(std::is_trivially_copyable_v<LocalPlayerStatePayload>);
static_assert(std::is_standard_layout_v<RemoteAvatarStatePayload>);
static_assert(std::is_trivially_copyable_v<RemoteAvatarStatePayload>);
static_assert(std::is_standard_layout_v<AnimationGraphChunkPayload>);
static_assert(std::is_trivially_copyable_v<AnimationGraphChunkPayload>);
static_assert(std::is_standard_layout_v<RemoteAnimationGraphStatePayload>);
static_assert(std::is_trivially_copyable_v<RemoteAnimationGraphStatePayload>);
static_assert(std::is_standard_layout_v<RemoteSpatialTransferStatePayload>);
static_assert(std::is_trivially_copyable_v<RemoteSpatialTransferStatePayload>);
static_assert(std::is_standard_layout_v<GameplayActionPayload>);
static_assert(std::is_trivially_copyable_v<GameplayActionPayload>);
static_assert(std::is_standard_layout_v<GameplayActionStatePayload>);
static_assert(std::is_trivially_copyable_v<GameplayActionStatePayload>);
static_assert(std::is_standard_layout_v<GameplayTextChunkPayload>);
static_assert(std::is_trivially_copyable_v<GameplayTextChunkPayload>);
static_assert(std::is_standard_layout_v<ApplyProjectileLaunchPayload>);
static_assert(std::is_trivially_copyable_v<ApplyProjectileLaunchPayload>);
static_assert(std::is_standard_layout_v<CreateRemoteAvatarPayload>);
static_assert(std::is_trivially_copyable_v<CreateRemoteAvatarPayload>);
static_assert(std::is_standard_layout_v<DestroyRemoteAvatarPayload>);
static_assert(std::is_trivially_copyable_v<DestroyRemoteAvatarPayload>);
static_assert(std::is_standard_layout_v<UpdateRemoteRootTransformPayload>);
static_assert(std::is_trivially_copyable_v<UpdateRemoteRootTransformPayload>);
static_assert(std::is_standard_layout_v<RetireEpochPayload>);
static_assert(std::is_trivially_copyable_v<RetireEpochPayload>);
static_assert(sizeof(LifecycleEventPayload) == kFixedPayloadBytes);
static_assert(sizeof(LocalPlayerStatePayload) == kFixedPayloadBytes);
static_assert(sizeof(RemoteAvatarStatePayload) == kFixedPayloadBytes);
static_assert(offsetof(RemoteAvatarStatePayload, LocalActorReferenceFormId) == 0x18);
static_assert(offsetof(RemoteAvatarStatePayload, Root) == 0x1C);
static_assert(sizeof(AnimationGraphChunkPayload) == kFixedPayloadBytes);
static_assert(sizeof(RemoteAnimationGraphStatePayload) == kFixedPayloadBytes);
static_assert(sizeof(RemoteSpatialTransferStatePayload) == kFixedPayloadBytes);
static_assert(sizeof(GameplayActionPayload) == kFixedPayloadBytes);
static_assert(sizeof(GameplayActionStatePayload) == kFixedPayloadBytes);
static_assert(sizeof(GameplayTextChunkPayload) == kFixedPayloadBytes);
static_assert(sizeof(ApplyProjectileLaunchPayload) == kFixedPayloadBytes);
static_assert(sizeof(ActorActionPayload) == kFixedPayloadBytes);
static_assert(sizeof(ActorActionGraphChunkPayload) == kFixedPayloadBytes);
static_assert(offsetof(ActorActionPayload, TargetHandle) == 0x00);
static_assert(offsetof(ActorActionPayload, ActorLocalFormId) == 0x08);
static_assert(offsetof(ActorActionPayload, State1) == 0x18);
static_assert(offsetof(ActorActionPayload, SnapshotId) == 0x28);
static_assert(offsetof(ActorActionPayload, TextId) == 0x30);
static_assert(offsetof(ActorActionGraphChunkPayload, SnapshotId) == 0x10);
static_assert(offsetof(ActorActionGraphChunkPayload, Values) == 0x2C);
static_assert(offsetof(ApplyProjectileLaunchPayload, TargetHandle) == 0x00);
static_assert(offsetof(ApplyProjectileLaunchPayload, LocalProjectileBaseFormId) == 0x08);
static_assert(offsetof(ApplyProjectileLaunchPayload, LocalParentCellFormId) == 0x18);
static_assert(offsetof(ApplyProjectileLaunchPayload, OriginX) == 0x1C);
static_assert(offsetof(ApplyProjectileLaunchPayload, AngleX) == 0x28);
static_assert(offsetof(ApplyProjectileLaunchPayload, Power) == 0x30);
static_assert(offsetof(ApplyProjectileLaunchPayload, CastingSource) == 0x38);
static_assert(offsetof(ApplyProjectileLaunchPayload, LaunchFlags) == 0x40);
static_assert(sizeof(CreateRemoteAvatarPayload) == kFixedPayloadBytes);
static_assert(sizeof(DestroyRemoteAvatarPayload) == kFixedPayloadBytes);
static_assert(sizeof(UpdateRemoteRootTransformPayload) == kFixedPayloadBytes);
static_assert(sizeof(RetireEpochPayload) == kFixedPayloadBytes);
static_assert(sizeof(EventRecord) == 0x90);
static_assert(sizeof(CommandRecord) == 0x90);
static_assert(alignof(EventRecord) == 8);
static_assert(alignof(CommandRecord) == 8);
static_assert(offsetof(EventRecord, Header) == 0x00);
static_assert(offsetof(EventRecord, Payload) == 0x40);
static_assert(offsetof(CommandRecord, Header) == 0x00);
static_assert(offsetof(CommandRecord, Payload) == 0x40);
static_assert(std::is_standard_layout_v<EventRecord>);
static_assert(std::is_trivially_copyable_v<EventRecord>);
static_assert(std::is_standard_layout_v<CommandRecord>);
static_assert(std::is_trivially_copyable_v<CommandRecord>);
static_assert(sizeof(MappingHeader) == 0x90);
static_assert(alignof(MappingHeader) == 8);
static_assert(offsetof(MappingHeader, State) == 0x18);
static_assert(offsetof(MappingHeader, RequestedCapabilities) == 0x20);
static_assert(offsetof(MappingHeader, AvailableCapabilities) == 0x28);
static_assert(offsetof(MappingHeader, ActiveCapabilities) == 0x30);
static_assert(offsetof(MappingHeader, LifecycleEpoch) == 0x48);
static_assert(offsetof(MappingHeader, CommandExecutionThreadId) == 0x58);
static_assert(offsetof(MappingHeader, RejectedCommandCount) == 0x80);
static_assert(sizeof(RingSlot<EventRecord>) == 0x98);
static_assert(sizeof(RingSlot<CommandRecord>) == 0x98);
static_assert(offsetof(EventRing, Slots) == 0x20);
static_assert(offsetof(CommandRing, Slots) == 0x20);
static_assert(sizeof(EventRing) == 0x4C020);
static_assert(sizeof(CommandRing) == 0x26020);
static_assert(alignof(EventRing) == 8);
static_assert(alignof(CommandRing) == 8);
static_assert(std::is_standard_layout_v<MappingHeader>);
static_assert(std::is_standard_layout_v<EventRing>);
static_assert(std::is_standard_layout_v<CommandRing>);
static_assert(sizeof(GameplayBridgeMapping) == 0x720D0);
static_assert(alignof(GameplayBridgeMapping) == 8);
static_assert(offsetof(GameplayBridgeMapping, Events) == 0x90);
static_assert(offsetof(GameplayBridgeMapping, Commands) == 0x4C0B0);
static_assert(std::is_standard_layout_v<GameplayBridgeMapping>);
} // namespace SkyrimTogetherVR::GameplayBridge
