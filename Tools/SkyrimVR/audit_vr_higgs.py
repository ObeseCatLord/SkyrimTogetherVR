#!/usr/bin/env python3
"""Audit the staged SkyrimTogetherVR HIGGS observation relay."""

from __future__ import annotations

import argparse
import pathlib


REQUIRED_TOKENS = {
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))',
        "observation_services = true",
    ),
    "Code/client/World.cpp": (
        "#include <Services/VRHiggsService.h>",
        "TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE",
        "ctx().emplace<VRHiggsService>(*this, m_dispatcher, m_transport);",
    ),
    "Code/client/Services/VRHiggsService.h": (
        "struct VRHiggsService",
        "NotifyVRHiggsState",
        "VRHiggsState",
        "GetRemoteHiggsStates",
    ),
    "Code/client/Services/Generic/VRHiggsService.cpp": (
        "SkyrimTogetherVR.higgs",
        "SkyrimTogetherVR.higgsnet",
        "ReadKeyValueFile",
        "RequestVRHiggsState",
        "NotifyVRHiggsState",
        "localHiggsAvailable",
        "remoteHiggsCount",
        "remoteHiggs",
        "SnapshotAvailable",
        "higgs.snapshotAvailable",
        "ParseHandState",
        "ParseLastEvent",
    ),
    "Code/encoding/Opcodes.h": (
        "kRequestVRHiggsState",
        "kNotifyVRHiggsState",
    ),
    "Code/encoding/Structs/VRHiggsState.h": (
        "struct VRHiggsState",
        "struct VRHiggsHandState",
        "struct VRHiggsEventSnapshot",
        "CallbacksRegistered",
        "SnapshotAvailable",
        "SnapshotSequence",
        "TwoHanding",
    ),
    "Code/encoding/Messages/RequestVRHiggsState.h": (
        "struct RequestVRHiggsState",
        "kRequestVRHiggsState",
        "VRHiggsState State",
    ),
    "Code/encoding/Messages/NotifyVRHiggsState.h": (
        "struct NotifyVRHiggsState",
        "kNotifyVRHiggsState",
        "uint32_t PlayerId",
        "VRHiggsState State",
    ),
    "Code/encoding/Messages/ClientMessageFactory.h": (
        "#include <Messages/RequestVRHiggsState.h>",
        "RequestVRHiggsState",
    ),
    "Code/encoding/Messages/ServerMessageFactory.h": (
        "#include <Messages/NotifyVRHiggsState.h>",
        "NotifyVRHiggsState",
    ),
    "Code/server/Services/VRHiggsRelayService.cpp": (
        "PacketEvent<RequestVRHiggsState>",
        "NotifyVRHiggsState",
        "kMinHiggsRelayIntervalMs",
        "IsNewerSequence",
        "HasHiggsObservation",
        "OnPlayerLeave",
        "ShouldRelayHiggsState",
        "statePacket.Sequence == 0 || !HasHiggsObservation(statePacket)",
        "acState.BridgeLoaded || acState.Detected || acState.InterfaceAvailable",
        "state.HasSequence",
        "GameServer::Get()->GetTick()",
        "now - state.LastRelayTick < kMinHiggsRelayIntervalMs",
        "state.LastSequence = statePacket.Sequence",
        "m_playerHiggsRelayState.erase",
        "notify.PlayerId = playerId;",
        "notify.State = acMessage.Packet.State;",
        "GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);",
    ),
    "Code/server/Services/VRHiggsRelayService.h": (
        "PlayerHiggsRelayState",
        "LastSequence",
        "LastRelayTick",
        "HasSequence",
        "OnPlayerLeave",
        "ShouldRelayHiggsState",
        "m_playerHiggsRelayState",
        "m_playerLeaveConnection",
    ),
    "Code/server/World.cpp": (
        "#include <Services/VRHiggsRelayService.h>",
        "ctx().emplace<VRHiggsRelayService>(*this, m_dispatcher);",
    ),
    "Code/tests/encoding.cpp": (
        "BuildHiggsState",
        "RequestVRHiggsState",
        "NotifyVRHiggsState",
        'GIVEN("VRHiggsState")',
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        "#include <Services/VRHiggsService.h>",
        "TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE",
        "Local HIGGS relay",
        "Remote HIGGS relays",
        "SnapshotAvailable",
    ),
    "Tools/SkyrimVR/vr_handoff.py": (
        "SkyrimTogetherVR.higgsnet",
        "remoteHiggsCount",
        "remoteHiggs",
        "higgsRelayRemote",
        "higgsSnapshot",
    ),
    "Docs/SkyrimVR/higgs-compatibility.md": (
        "VRHiggsService",
        "SkyrimTogetherVR.higgsnet",
        "observation-only",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "VRHiggsService",
        "VRHiggsRelayService",
        "audit_vr_higgs.py",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "VRHiggsService",
        "SkyrimTogetherVR.higgsnet",
    ),
}

FORBIDDEN_SERVICE_TOKENS = (
    "HiggsPluginAPI",
    "IHiggsInterface001",
    "AddPulledCallback",
    "AddGrabbedCallback",
    "AddDroppedCallback",
    "AddCollisionCallback",
    "->GrabObject",
    "->SetGrabTransform",
    "->DisableHand",
    "->EnableHand",
    "->SetHiggsLayerBitfield",
    "->SetSettingDouble",
    "ObjectService",
    "TESObjectREFR::Activate",
    "Projectile::Launch",
    "EquipManager",
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

    service_path = root / "Code/client/Services/Generic/VRHiggsService.cpp"
    service_text = service_path.read_text(encoding="utf-8", errors="replace") if service_path.exists() else ""
    forbidden = [token for token in FORBIDDEN_SERVICE_TOKENS if token in service_text]
    if forbidden:
        failures.append("Code/client/Services/Generic/VRHiggsService.cpp: forbidden mutation/HIGGS/gameplay token(s)")
        failures.extend(f"  {token}" for token in forbidden)

    print(f"Audited required HIGGS relay surfaces: {len(REQUIRED_TOKENS)}")
    print(f"Forbidden VRHiggsService tokens present: {len(forbidden)}")
    print(f"Failures: {len(failures)}")
    for failure in failures:
        print(failure)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
