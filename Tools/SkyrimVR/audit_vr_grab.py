#!/usr/bin/env python3
"""Audit the staged SkyrimTogetherVR grab/release observer."""

from __future__ import annotations

import argparse
import pathlib


REQUIRED_TOKENS = {
    "Code/client/Games/Skyrim/RuntimeLayout.h": (
        "ScriptEventSourceHolderCommonLibNgOffsets",
        "GrabRelease = 0x580",
        "EventDispatcherManagerLocalShimOffsets",
    ),
    "Code/client/Games/Skyrim/Events/EventDispatcher.h": (
        "struct TESGrabReleaseEvent",
        "GetReferenceData",
        "GetGrabbedData",
        "static_assert(offsetof(TESGrabReleaseEvent, ref) == 0x0);",
        "static_assert(offsetof(TESGrabReleaseEvent, grabbed) == 0x8);",
        "static_assert(sizeof(TESGrabReleaseEvent) == 0x10);",
        "GetGrabReleaseEventData",
        "CommonLibEventSourceOffsets::GrabRelease",
        "offsetof(EventDispatcherManager, grabReleaseEvent)",
    ),
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE=1")',
    ),
    "Code/client/World.cpp": (
        "#include <Services/VRGrabService.h>",
        "TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE",
        "ctx().emplace<VRGrabService>(*this, m_dispatcher, m_transport);",
    ),
    "Code/client/Services/VRGrabService.h": (
        "struct VRGrabService final : BSTEventSink<TESGrabReleaseEvent>",
        "NotifyVRGrabEvent",
        "VRGrabEvent",
    ),
    "Code/client/Services/Generic/VRGrabService.cpp": (
        "SkyrimTogetherVR.grab",
        "TESGrabReleaseEvent",
        "GetGrabReleaseEventData().RegisterSink",
        "RequestVRGrabEvent",
        "NotifyVRGrabEvent",
        "localGrabAvailable",
        "remoteGrabCount",
        "localGrab",
        "remoteGrab",
        "ObjectId",
        "CellId",
        "WorldSpaceId",
        "Position",
        "FormType",
        "Grabbed",
    ),
    "Code/encoding/Opcodes.h": (
        "kRequestVRGrabEvent",
        "kNotifyVRGrabEvent",
    ),
    "Code/encoding/Structs/VRGrabEvent.h": (
        "struct VRGrabEvent",
        "ObjectId",
        "CellId",
        "WorldSpaceId",
        "Position",
        "FormType",
        "Grabbed",
    ),
    "Code/encoding/Messages/RequestVRGrabEvent.h": (
        "struct RequestVRGrabEvent",
        "kRequestVRGrabEvent",
        "VRGrabEvent Grab",
    ),
    "Code/encoding/Messages/NotifyVRGrabEvent.h": (
        "struct NotifyVRGrabEvent",
        "kNotifyVRGrabEvent",
        "uint32_t PlayerId",
        "VRGrabEvent Grab",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRGrabEvent.h>",
        "RequestVRGrabEvent",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRGrabEvent.h>",
        "NotifyVRGrabEvent",
    ),
    "Code/server/Services/VRGrabRelayService.cpp": (
        "PacketEvent<RequestVRGrabEvent>",
        "NotifyVRGrabEvent",
        "kMinGrabRelayIntervalMs",
        "IsNewerSequence",
        "HasGrabObject",
        "OnPlayerLeave",
        "ShouldRelayGrab",
        "grab.Sequence == 0 || !HasGrabObject(grab)",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinGrabRelayIntervalMs",
        "state.LastSequence = grab.Sequence",
        "m_playerGrabRelayState.erase",
        "notify.PlayerId = playerId;",
        "notify.Grab = acMessage.Packet.Grab;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRGrabRelayService.h": (
        "PlayerGrabRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayGrab",
        "m_playerGrabRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRGrabRelayService.h>",
        "ctx().emplace<VRGrabRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildGrabEvent",
        "RequestVRGrabEvent",
        "NotifyVRGrabEvent",
        'GIVEN("VRGrabEvent")',
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "SkyrimTogetherVR.grab",
        "localGrabAvailable",
        "remoteGrabCount",
    ),
    "Docs/SkyrimVR/vr-grab-relay.md": (
        "TESGrabReleaseEvent",
        "SkyrimTogetherVR.grab",
        "HIGGS",
        "does not",
    ),
    "Docs/SkyrimVR/higgs-compatibility.md": (
        "VRGrabService",
        "TESGrabReleaseEvent",
        "SkyrimTogetherVR.grab",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "VRGrabService",
        "VRGrabRelayService",
        "audit_vr_grab.py",
    ),
}

FORBIDDEN_SERVICE_TOKENS = (
    "Higgs",
    "HIGGS",
    "AddGrabbedCallback",
    "AddDroppedCallback",
    "GrabObject",
    "DropObject",
    "SetGrabTransform",
    "DisableHand",
    "EnableHand",
    "SetHiggsLayerBitfield",
    "ActivateRequest",
    "NotifyActivate",
    "ObjectService",
    "TESObjectREFR::Activate",
    "HookGrab",
    "HookDrop",
    "HookPickUpObject",
    "HookDropObject",
    "Projectile::Launch",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def missing_tokens(text: str, tokens: tuple[str, ...]) -> list[str]:
    return [token for token in tokens if token not in text]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=pathlib.Path, default=repo_root())
    args = parser.parse_args()

    root = args.repo.resolve()
    failures: list[str] = []

    for rel_path, tokens in REQUIRED_TOKENS.items():
        path = root / rel_path
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        missing = missing_tokens(text, tokens)
        if missing:
            failures.append(f"{rel_path}: missing {len(missing)} required token(s)")
            failures.extend(f"  {token}" for token in missing)

    service_path = root / "Code/client/Services/Generic/VRGrabService.cpp"
    service_text = service_path.read_text(encoding="utf-8", errors="replace") if service_path.exists() else ""
    forbidden = [token for token in FORBIDDEN_SERVICE_TOKENS if token in service_text]
    if forbidden:
        failures.append("Code/client/Services/Generic/VRGrabService.cpp: forbidden mutation/HIGGS token(s)")
        failures.extend(f"  {token}" for token in forbidden)

    print(f"Audited required grab surfaces: {len(REQUIRED_TOKENS)}")
    print(f"Forbidden VRGrabService tokens present: {len(forbidden)}")
    print(f"Failures: {len(failures)}")
    for failure in failures:
        print(failure)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
