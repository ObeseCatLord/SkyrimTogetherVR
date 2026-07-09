#!/usr/bin/env python3
"""Audit the readout-only SkyrimTogetherVR remote-player proxy contract."""

from __future__ import annotations

import pathlib
import tempfile

import vr_handoff


WRITER_REQUIRED = {
    "Code/client/Services/Generic/VRRemotePlayerService.cpp": (
        "SkyrimTogetherVR.remoteplayers",
        'file << "ready=1\\n";',
        'file << "online="',
        'file << "localPlayerId="',
        'file << "trackedPlayerCount="',
        'file << "joinedPlayerCount="',
        'file << "remotePoseCount="',
        'file << "remoteMovementCount="',
        'file << "remoteEquipmentCount="',
        'file << "remoteActivationCount="',
        'file << "remoteMagicEffectCount="',
        'file << "remoteCombatHitCount="',
        'file << "remoteProjectileEventCount="',
        'file << "remoteGrabCount="',
        'file << "remoteHiggsCount="',
        'file << "poseMatchedCount="',
        'file << "movementMatchedCount="',
        'file << "equipmentMatchedCount="',
        'file << "activationMatchedCount="',
        'file << "magicMatchedCount="',
        'file << "combatMatchedCount="',
        'file << "projectileMatchedCount="',
        'file << "grabMatchedCount="',
        'file << "higgsMatchedCount="',
        'file << "vrikMatchedCount="',
        'file << "sameCellCount="',
        'file << "sameWorldSpaceCount="',
        'file << "sameSpaceCount="',
        'file << "avatarValidationReadyCount="',
        'file << "higgsAvatarValidationReadyCount="',
        'const std::string prefix = "remotePlayer."',
        '".playerId="',
        '".username="',
        '".level="',
        '".ageSeconds="',
        'WriteGameId(file, prefix + ".cell", remoteCell);',
        'WriteGameId(file, prefix + ".worldSpace", remoteWorldSpace);',
        'WriteBool(file, prefix + ".poseAvailable", poseAvailable);',
        '".poseSequence="',
        'WriteBool(file, prefix + ".hmdAvailable", hmdAvailable);',
        'WriteBool(file, prefix + ".leftHandAvailable", leftHandAvailable);',
        'WriteBool(file, prefix + ".rightHandAvailable", rightHandAvailable);',
        'WriteBool(file, prefix + ".vrikDetected", vrikDetected);',
        'WriteBool(file, prefix + ".vrikInterfaceAvailable", vrikInterfaceAvailable);',
        'WriteBool(file, prefix + ".vrikAvailable", vrikAvailable);',
        'WriteBool(file, prefix + ".movementAvailable", movementAvailable);',
        'WriteBool(file, prefix + ".equipmentAvailable", equipmentAvailable);',
        'WriteBool(file, prefix + ".activationAvailable", activationAvailable);',
        'WriteBool(file, prefix + ".magicAvailable", magicAvailable);',
        'WriteBool(file, prefix + ".combatAvailable", combatAvailable);',
        'WriteBool(file, prefix + ".projectileAvailable", projectileAvailable);',
        'WriteBool(file, prefix + ".grabAvailable", grabAvailable);',
        'WriteBool(file, prefix + ".higgsAvailable", higgsAvailable);',
        'WriteBool(file, prefix + ".sameCellAvailable", sameCellAvailable);',
        'WriteBool(file, prefix + ".sameCell", sameCell);',
        'WriteBool(file, prefix + ".sameWorldSpaceAvailable", sameWorldSpaceAvailable);',
        'WriteBool(file, prefix + ".sameWorldSpace", sameWorldSpace);',
        'WriteBool(file, prefix + ".sameSpaceAvailable", sameSpaceAvailable);',
        'WriteBool(file, prefix + ".sameSpace", sameSpace);',
        'WriteBool(file, prefix + ".avatarValidationReady", avatarValidationReady);',
        '".avatarValidationBlocker="',
        'WriteBool(file, prefix + ".higgsAvatarValidationReady", higgsAvatarValidationReady);',
        '".higgsAvatarValidationBlocker="',
    ),
    "Code/client/Services/VRRemotePlayerService.h": (
        "struct VRRemotePlayerInfo",
        "uint32_t PlayerId",
        "String Username",
        "GameId WorldSpaceId",
        "GameId CellId",
        "uint16_t Level",
        "double AgeSeconds",
        "GetRemotePlayerCount",
        "GetRemotePlayers",
    ),
    "Code/client/World.cpp": (
        "TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE",
        "SkyrimTogetherVR remote-player proxy service is enabled in readout-only mode",
        "ctx().emplace<VRRemotePlayerService>(*this, m_dispatcher, m_transport);",
    ),
    "Code/client/xmake.lua": (
        'add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE=1")',
    ),
}

PARSER_REQUIRED = {
    "Tools/SkyrimVR/vr_handoff.py": (
        '"remoteplayers": "SkyrimTogetherVR.remoteplayers"',
        '"remoteplayers": ("ready", "online", "trackedPlayerCount", "joinedPlayerCount", "poseMatchedCount", "movementMatchedCount", "equipmentMatchedCount", "activationMatchedCount", "magicMatchedCount", "combatMatchedCount", "projectileMatchedCount", "grabMatchedCount", "higgsMatchedCount", "vrikMatchedCount", "sameCellCount", "sameWorldSpaceCount", "sameSpaceCount", "avatarValidationReadyCount", "higgsAvatarValidationReadyCount")',
        'proxy_values = readouts.get("remoteplayers", {})',
        'collect_remote_ids(proxy_values, "remotePlayer")',
        'proxy_values.get(f"{prefix}.username"',
        'proxy_values.get(f"{prefix}.level"',
        'proxy_values.get(f"{prefix}.sameCell")',
        'proxy_values.get(f"{prefix}.sameWorldSpace")',
        'proxy_values.get(f"{prefix}.sameSpace")',
        'proxy_values.get(f"{prefix}.poseAvailable")',
        'proxy_values.get(f"{prefix}.hmdAvailable")',
        'proxy_values.get(f"{prefix}.leftHandAvailable")',
        'proxy_values.get(f"{prefix}.rightHandAvailable")',
        'proxy_values.get(f"{prefix}.vrikDetected")',
        'proxy_values.get(f"{prefix}.vrikInterfaceAvailable")',
        'proxy_values.get(f"{prefix}.vrikAvailable")',
        'proxy_values.get(f"{prefix}.movementAvailable")',
        'proxy_values.get(f"{prefix}.equipmentAvailable")',
        'proxy_values.get(f"{prefix}.activationAvailable")',
        'proxy_values.get(f"{prefix}.magicAvailable")',
        'proxy_values.get(f"{prefix}.combatAvailable")',
        'proxy_values.get(f"{prefix}.projectileAvailable")',
        'proxy_values.get(f"{prefix}.grabAvailable")',
        'proxy_values.get(f"{prefix}.higgsAvailable")',
        'proxy_values.get(f"{prefix}.avatarValidationReady")',
        'proxy_values.get(f"{prefix}.avatarValidationBlocker"',
        'proxy_values.get(f"{prefix}.higgsAvatarValidationReady")',
        'proxy_values.get(f"{prefix}.higgsAvatarValidationBlocker"',
        'game_id_summary(proxy_values, f"{prefix}.cell")',
        'game_id_summary(proxy_values, f"{prefix}.worldSpace")',
        'player["proxy"]',
        'f"proxy={player.get(',
        'const columns = ["playerId", "proxy"',
        '"remotePlayer.7.username=Remote Tester',
        'assert "name=Remote Tester" in remote_players[0]["proxy"]',
        'assert "sameCell=yes" in remote_players[0]["proxy"]',
        'assert "hmd=yes" in remote_players[0]["proxy"]',
        'assert "leftHand=yes" in remote_players[0]["proxy"]',
        'assert "rightHand=yes" in remote_players[0]["proxy"]',
        'assert "avatarReady=yes" in remote_players[0]["proxy"]',
        'assert "higgsAvatarReady=yes" in remote_players[0]["proxy"]',
    ),
    "Code/client/Games/PapyrusFunctions.cpp": (
        'GetHandoffPath("SkyrimTogetherVR.remoteplayers")',
        'Remote players: ',
        'Remote player proxy:',
        '"tracked="',
        '" joined="',
        '" action="',
        '"\\naction activation="',
        '" sameCell="',
        '" avatarReady="',
        '"\\nsame cell="',
        '" avatar ready="',
        '" higgs avatar ready="',
    ),
}

DOC_REQUIRED = {
    "Docs/SkyrimVR/vr-handoff-bridge.md": (
        "SkyrimTogetherVR.remoteplayers",
        "`proxy` column",
        "higgsAvatarReady=yes",
        "without creating actors or objects",
        "Remote Players",
    ),
    "Docs/SkyrimVR/connection-only-mode.md": (
        "SkyrimTogetherVR.remoteplayers",
        "VRRemotePlayerService",
        "higgsAvatarReady=yes",
        "does not spawn actors, create marker objects, move scene nodes, equip items, replay HIGGS events, replay gameplay actions, or call PLANCK",
    ),
    "Docs/SkyrimVR/porting-status.md": (
        "VRRemotePlayerService",
        "SkyrimTogetherVR.remoteplayers",
        "HIGGS-aware avatar-validation readiness",
        "without spawning actors, creating marker objects, moving scene nodes, equipping items, replaying gameplay actions, or replaying HIGGS/PLANCK state",
    ),
}

FORBIDDEN_WRITER_TOKENS = {
    "Code/client/Services/Generic/VRRemotePlayerService.cpp": (
        "MoveTo(",
        "ForcePosition",
        "SetRotationData",
        "TESObjectREFR",
        "EquipManager",
        "AddOrRemoveItem",
        "emplace_or_replace<RemoteVRPoseComponent>",
        "emplace_or_replace<RemoteVREquipmentComponent>",
        "ApplyRemoteAvatar",
        "CreateRef",
        "PlaceAtMe",
    ),
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def audit_required(root: pathlib.Path, mapping: dict[str, tuple[str, ...]]) -> dict[str, list[str]]:
    missing: dict[str, list[str]] = {}
    for relative_path, tokens in mapping.items():
        path = root / relative_path
        if not path.exists():
            missing[relative_path] = ["<missing file>"]
            continue
        text = read_text(path)
        file_missing = [token for token in tokens if token not in text]
        if file_missing:
            missing[relative_path] = file_missing
    return missing


def audit_forbidden(root: pathlib.Path, mapping: dict[str, tuple[str, ...]]) -> dict[str, list[str]]:
    present: dict[str, list[str]] = {}
    for relative_path, tokens in mapping.items():
        path = root / relative_path
        if not path.exists():
            continue
        text = read_text(path)
        file_present = [token for token in tokens if token in text]
        if file_present:
            present[relative_path] = file_present
    return present


def run_parser_fixture() -> list[str]:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        handoff_dir = pathlib.Path(temp_dir)
        remoteplayers = handoff_dir / "SkyrimTogetherVR.remoteplayers"
        remoteplayers.write_text(
            "\n".join(
                (
                    "ready=1",
                    "online=1",
                    "localPlayerId=3",
                    "trackedPlayerCount=1",
                    "joinedPlayerCount=1",
                    "remotePoseCount=1",
                    "remoteMovementCount=1",
                    "remoteEquipmentCount=1",
                    "remoteActivationCount=1",
                    "remoteMagicEffectCount=1",
                    "remoteCombatHitCount=1",
                    "remoteProjectileEventCount=1",
                    "remoteGrabCount=1",
                    "remoteHiggsCount=1",
                    "remotePlayer.7.playerId=7",
                    "remotePlayer.7.username=Remote Tester",
                    "remotePlayer.7.level=12",
                    "remotePlayer.7.ageSeconds=0.5",
                    "remotePlayer.7.cell.serverModId=1",
                    "remotePlayer.7.cell.serverBaseId=100",
                    "remotePlayer.7.worldSpace.serverModId=1",
                    "remotePlayer.7.worldSpace.serverBaseId=200",
                    "remotePlayer.7.poseAvailable=1",
                    "remotePlayer.7.poseSequence=3",
                    "remotePlayer.7.hmdAvailable=1",
                    "remotePlayer.7.leftHandAvailable=1",
                    "remotePlayer.7.rightHandAvailable=1",
                    "remotePlayer.7.vrikDetected=1",
                    "remotePlayer.7.vrikInterfaceAvailable=1",
                    "remotePlayer.7.vrikAvailable=1",
                    "remotePlayer.7.movementAvailable=1",
                    "remotePlayer.7.equipmentAvailable=1",
                    "remotePlayer.7.activationAvailable=1",
                    "remotePlayer.7.magicAvailable=1",
                    "remotePlayer.7.combatAvailable=1",
                    "remotePlayer.7.projectileAvailable=1",
                    "remotePlayer.7.grabAvailable=1",
                    "remotePlayer.7.higgsAvailable=1",
                    "remotePlayer.7.sameCellAvailable=1",
                    "remotePlayer.7.sameCell=1",
                    "remotePlayer.7.sameWorldSpaceAvailable=1",
                    "remotePlayer.7.sameWorldSpace=1",
                    "remotePlayer.7.sameSpaceAvailable=1",
                    "remotePlayer.7.sameSpace=1",
                    "remotePlayer.7.avatarValidationReady=1",
                    "remotePlayer.7.avatarValidationBlocker=ready",
                    "remotePlayer.7.higgsAvatarValidationReady=1",
                    "remotePlayer.7.higgsAvatarValidationBlocker=ready",
                    "poseMatchedCount=1",
                    "movementMatchedCount=1",
                    "equipmentMatchedCount=1",
                    "activationMatchedCount=1",
                    "magicMatchedCount=1",
                    "combatMatchedCount=1",
                    "projectileMatchedCount=1",
                    "grabMatchedCount=1",
                    "higgsMatchedCount=1",
                    "vrikMatchedCount=1",
                    "sameCellCount=1",
                    "sameWorldSpaceCount=1",
                    "sameSpaceCount=1",
                    "avatarValidationReadyCount=1",
                    "higgsAvatarValidationReadyCount=1",
                    "",
                )
            ),
            encoding="utf-8",
        )

        payload = vr_handoff.build_readout_payload(handoff_dir)
        summary = payload["summaries"].get("remoteplayers", "")
        players = payload["remotePlayers"]
        if "trackedPlayerCount=1" not in summary:
            failures.append("remoteplayers summary omitted trackedPlayerCount")
        if "sameCellCount=1" not in summary:
            failures.append("remoteplayers summary omitted sameCellCount")
        if "activationMatchedCount=1" not in summary:
            failures.append("remoteplayers summary omitted activationMatchedCount")
        if "grabMatchedCount=1" not in summary:
            failures.append("remoteplayers summary omitted grabMatchedCount")
        if "avatarValidationReadyCount=1" not in summary:
            failures.append("remoteplayers summary omitted avatarValidationReadyCount")
        if "higgsAvatarValidationReadyCount=1" not in summary:
            failures.append("remoteplayers summary omitted higgsAvatarValidationReadyCount")
        if len(players) != 1:
            failures.append(f"expected one remote player in parser fixture, got {len(players)}")
        elif "proxy" not in players[0]:
            failures.append("parser fixture remote player is missing proxy field")
        else:
            proxy = players[0]["proxy"]
            for token in (
                "name=Remote Tester",
                "level=12",
                "cell=1:100",
                "world=1:200",
                "sameCell=yes",
                "sameWorld=yes",
                "sameSpace=yes",
                "pose=yes",
                "hmd=yes",
                "leftHand=yes",
                "rightHand=yes",
                "vrikDetected=yes",
                "vrikApi=yes",
                "vrik=yes",
                "movement=yes",
                "equipment=yes",
                "activation=yes",
                "magic=yes",
                "combat=yes",
                "projectile=yes",
                "grab=yes",
                "higgs=yes",
                "avatarReady=yes",
                "blocker=ready",
                "higgsAvatarReady=yes",
                "higgsBlocker=ready",
            ):
                if token not in proxy:
                    failures.append(f"parser fixture proxy missing token: {token}")

    return failures


def format_token_map(mapping: dict[str, list[str]]) -> str:
    lines: list[str] = []
    for relative_path, tokens in sorted(mapping.items()):
        lines.append(f"- {relative_path}")
        lines.extend(f"  - {token}" for token in tokens)
    return "\n".join(lines)


def main() -> int:
    root = repo_root()
    missing_writer = audit_required(root, WRITER_REQUIRED)
    missing_parser = audit_required(root, PARSER_REQUIRED)
    missing_docs = audit_required(root, DOC_REQUIRED)
    forbidden_writer = audit_forbidden(root, FORBIDDEN_WRITER_TOKENS)
    fixture_failures = run_parser_fixture()

    print(f"Missing remote-player writer tokens: {sum(len(values) for values in missing_writer.values())}")
    print(f"Missing remote-player parser tokens: {sum(len(values) for values in missing_parser.values())}")
    print(f"Missing remote-player doc tokens: {sum(len(values) for values in missing_docs.values())}")
    print(f"Forbidden remote-player mutation tokens: {sum(len(values) for values in forbidden_writer.values())}")
    print(f"Remote-player parser fixture failures: {len(fixture_failures)}")

    if missing_writer:
        print("\nMissing writer tokens:")
        print(format_token_map(missing_writer))
    if missing_parser:
        print("\nMissing parser tokens:")
        print(format_token_map(missing_parser))
    if missing_docs:
        print("\nMissing doc tokens:")
        print(format_token_map(missing_docs))
    if forbidden_writer:
        print("\nForbidden writer tokens:")
        print(format_token_map(forbidden_writer))
    if fixture_failures:
        print("\nParser fixture failures:")
        for failure in fixture_failures:
            print(f"- {failure}")

    return 1 if missing_writer or missing_parser or missing_docs or forbidden_writer or fixture_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
