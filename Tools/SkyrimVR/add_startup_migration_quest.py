#!/usr/bin/env python3
"""Add the one-shot startup migration quest to SkyrimTogether.esp.

The original Start Game Enabled quest may already have its flags baked into an
old save from before the SEQ file was shipped.  This tool adds one new quest
record, which existing saves have never seen.  The copied record is rebound to
the migration Papyrus scripts.  The native cadence-owner claim keeps exactly
one quest timer active when both records are present on a clean install.
"""

import argparse
import pathlib
import struct
import sys
import tempfile

import generate_seq


SOURCE_FORM_ID = 0x02003DD0
MIGRATION_FORM_ID = 0x02003DD1
SOURCE_EDID = b"SkyrimTogetherVerifyLaunchWarning\x00"
MIGRATION_EDID = b"SkyrimTogetherVRStartupMigrationQ\x00"
SOURCE_QUEST_SCRIPT = b"SkyrimTogetherVerifyLaunchScript"
MIGRATION_QUEST_SCRIPT = b"SkyrimTogetherVRMigrationXScript"
SOURCE_ALIAS_SCRIPT = b"SkyrimTogetherPlayerAliasScript"
MIGRATION_ALIAS_SCRIPT = b"SkyrimTogetherVRMigrationScript"
START_GAME_ENABLED_RUN_ONCE_FLAGS = 0x0111
VR_HEADER_VERSION = 1.70
VR_HEADER_VERSION_BYTES = struct.pack("<f", VR_HEADER_VERSION)
RECORD_HEADER_SIZE = 24
GROUP_HEADER_SIZE = 24


class MigrationError(ValueError):
    """The staged ESP does not match the expected SkyrimTogetherVR structure."""


def read_u16(data, offset):
    if offset + 2 > len(data):
        raise MigrationError("unexpected end of file while reading a uint16")
    return int.from_bytes(data[offset : offset + 2], "little")


def read_u32(data, offset):
    if offset + 4 > len(data):
        raise MigrationError("unexpected end of file while reading a uint32")
    return int.from_bytes(data[offset : offset + 4], "little")


def write_u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def iter_records(data, start=0, end=None, parent_groups=()):
    if end is None:
        end = len(data)
    cursor = start
    while cursor < end:
        if cursor + RECORD_HEADER_SIZE > end:
            raise MigrationError(f"truncated record header at 0x{cursor:x}")
        signature = data[cursor : cursor + 4]
        size = read_u32(data, cursor + 4)
        if signature == b"GRUP":
            if size < GROUP_HEADER_SIZE or cursor + size > end:
                raise MigrationError(f"invalid GRUP at 0x{cursor:x}")
            yield from iter_records(data, cursor + GROUP_HEADER_SIZE, cursor + size, parent_groups + (cursor,))
            cursor += size
            continue
        record_end = cursor + RECORD_HEADER_SIZE + size
        if record_end > end:
            raise MigrationError(f"truncated {signature!r} record at 0x{cursor:x}")
        yield cursor, record_end, signature, read_u32(data, cursor + 12), parent_groups
        cursor = record_end


def iter_subrecord_offsets(payload):
    cursor = 0
    extended_size = None
    while cursor < len(payload):
        if cursor + 6 > len(payload):
            raise MigrationError(f"truncated subrecord at 0x{cursor:x}")
        signature = payload[cursor : cursor + 4]
        size = read_u16(payload, cursor + 4)
        cursor += 6
        if signature == b"XXXX":
            if size != 4 or cursor + 4 > len(payload):
                raise MigrationError(f"invalid XXXX subrecord at 0x{cursor - 6:x}")
            extended_size = read_u32(payload, cursor)
            cursor += 4
            continue
        data_size = extended_size if extended_size is not None else size
        extended_size = None
        value_end = cursor + data_size
        if value_end > len(payload):
            raise MigrationError(f"truncated {signature!r} subrecord at 0x{cursor - 6:x}")
        yield signature, cursor, value_end
        cursor = value_end
    if extended_size is not None:
        raise MigrationError("XXXX subrecord without a following subrecord")


def tes4_header_version_offset(data):
    if len(data) < RECORD_HEADER_SIZE or data[:4] != b"TES4":
        raise MigrationError("input is not a TES4 plugin")
    header_size = read_u32(data, 4)
    header_end = RECORD_HEADER_SIZE + header_size
    if header_end > len(data):
        raise MigrationError("TES4 header record is truncated")
    header_payload = data[RECORD_HEADER_SIZE:header_end]
    for signature, start, end in iter_subrecord_offsets(header_payload):
        if signature == b"HEDR":
            if end - start < 4:
                raise MigrationError("TES4 HEDR subrecord is truncated")
            return RECORD_HEADER_SIZE + start
    raise MigrationError("TES4 HEDR subrecord is missing")


def normalize_vr_header_version(data):
    version_offset = tes4_header_version_offset(data)
    if data[version_offset : version_offset + 4] == VR_HEADER_VERSION_BYTES:
        return bytes(data), False
    result = bytearray(data)
    result[version_offset : version_offset + 4] = VR_HEADER_VERSION_BYTES
    return bytes(result), True


def find_quest(data, form_id):
    for start, end, signature, record_form_id, parents in iter_records(data):
        if signature == b"QUST" and record_form_id == form_id:
            return start, end, parents
    return None


def quest_payload(data, quest):
    return data[quest[0] + RECORD_HEADER_SIZE : quest[1]]


def quest_subrecord(payload, expected_signature):
    for signature, start, end in iter_subrecord_offsets(payload):
        if signature == expected_signature:
            return payload[start:end]
    return None


def validate_record(data, quest, form_id, edid, quest_script, alias_script):
    if quest is None:
        raise MigrationError(f"quest 0x{form_id:08X} is missing")
    payload = quest_payload(data, quest)
    if quest_subrecord(payload, b"EDID") != edid:
        raise MigrationError(f"quest 0x{form_id:08X} has an unexpected EDID")
    dnam = quest_subrecord(payload, b"DNAM")
    if dnam is None or len(dnam) < 2 or read_u16(dnam, 0) != START_GAME_ENABLED_RUN_ONCE_FLAGS:
        raise MigrationError(f"quest 0x{form_id:08X} is not Start Game Enabled Run Once")
    if quest_script not in payload or alias_script not in payload:
        raise MigrationError(f"quest 0x{form_id:08X} is missing its expected Papyrus scripts")
    if payload.count(struct.pack("<I", form_id)) < 2:
        raise MigrationError(f"quest 0x{form_id:08X} is missing rewritten self references")


def verify_plugin(plugin):
    data = pathlib.Path(plugin).read_bytes()
    if len(data) < RECORD_HEADER_SIZE or data[:4] != b"TES4":
        raise MigrationError(f"{plugin} is not a TES4 plugin")
    version_offset = tes4_header_version_offset(data)
    if data[version_offset : version_offset + 4] != VR_HEADER_VERSION_BYTES:
        actual = struct.unpack_from("<f", data, version_offset)[0]
        raise MigrationError(
            f"TES4 header version {actual:.2f} is not Skyrim VR-compatible {VR_HEADER_VERSION:.2f}"
        )
    validate_record(
        data,
        find_quest(data, SOURCE_FORM_ID),
        SOURCE_FORM_ID,
        SOURCE_EDID,
        SOURCE_QUEST_SCRIPT,
        SOURCE_ALIAS_SCRIPT,
    )
    validate_record(
        data,
        find_quest(data, MIGRATION_FORM_ID),
        MIGRATION_FORM_ID,
        MIGRATION_EDID,
        MIGRATION_QUEST_SCRIPT,
        MIGRATION_ALIAS_SCRIPT,
    )
    if (MIGRATION_FORM_ID >> 24) != generate_seq.plugin_master_count(data):
        raise MigrationError("migration FormID is not owned by SkyrimTogether.esp")
    return tuple(generate_seq.start_game_enabled_quest_ids(plugin))


def verify_plugin_bytes(data):
    with tempfile.NamedTemporaryFile(suffix=".esp") as handle:
        handle.write(data)
        handle.flush()
        return verify_plugin(pathlib.Path(handle.name))


def replace_exact_same_size(data, source, target, label):
    if len(source) != len(target):
        raise MigrationError(f"{label} replacement changes record size")
    count = data.count(source)
    if count != 1:
        raise MigrationError(f"expected exactly one {label} reference, found {count}")
    return data.replace(source, target), count


def add_migration_quest(data):
    data, header_changed = normalize_vr_header_version(data)
    if find_quest(data, MIGRATION_FORM_ID) is not None:
        verify_plugin_bytes(data)
        return data, header_changed

    source = find_quest(data, SOURCE_FORM_ID)
    validate_record(
        data,
        source,
        SOURCE_FORM_ID,
        SOURCE_EDID,
        SOURCE_QUEST_SCRIPT,
        SOURCE_ALIAS_SCRIPT,
    )
    if (MIGRATION_FORM_ID >> 24) != generate_seq.plugin_master_count(data):
        raise MigrationError("migration FormID is not owned by SkyrimTogether.esp")

    source_start, source_end, parent_groups = source
    clone = bytearray(data[source_start:source_end])
    write_u32(clone, 12, MIGRATION_FORM_ID)
    payload = clone[RECORD_HEADER_SIZE:]
    payload, _ = replace_exact_same_size(payload, SOURCE_EDID, MIGRATION_EDID, "EDID")
    payload, _ = replace_exact_same_size(payload, SOURCE_QUEST_SCRIPT, MIGRATION_QUEST_SCRIPT, "quest script")
    payload, _ = replace_exact_same_size(payload, SOURCE_ALIAS_SCRIPT, MIGRATION_ALIAS_SCRIPT, "alias script")
    source_id = struct.pack("<I", SOURCE_FORM_ID)
    target_id = struct.pack("<I", MIGRATION_FORM_ID)
    self_reference_count = payload.count(source_id)
    if self_reference_count != 2:
        raise MigrationError(f"expected two source self references, found {self_reference_count}")
    payload = payload.replace(source_id, target_id)
    clone[RECORD_HEADER_SIZE:] = payload

    result = bytearray(data[:source_end] + clone + data[source_end:])
    for group_offset in parent_groups:
        write_u32(result, group_offset + 4, read_u32(result, group_offset + 4) + len(clone))

    header_size = read_u32(result, 4)
    header_payload = result[RECORD_HEADER_SIZE : RECORD_HEADER_SIZE + header_size]
    for signature, start, end in iter_subrecord_offsets(header_payload):
        if signature == b"HEDR":
            if end - start < 12:
                raise MigrationError("TES4 HEDR subrecord is truncated")
            absolute = RECORD_HEADER_SIZE + start + 4
            write_u32(result, absolute, read_u32(result, absolute) + 1)
            break
    else:
        raise MigrationError("TES4 HEDR subrecord is missing")

    verify_plugin_bytes(result)
    return bytes(result), True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--write", action="store_true", help="write the migration-enabled ESP")
    parser.add_argument("--verify", action="store_true", help="verify an ESP already contains the migration quest")
    args = parser.parse_args()
    if not args.write and not args.verify:
        parser.error("specify --write, --verify, or both")

    try:
        if args.write:
            migrated, changed = add_migration_quest(args.plugin.read_bytes())
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(migrated)
            print(f"{'wrote' if changed else 'preserved'} {args.output}: migration 0x{MIGRATION_FORM_ID:08X}")
        if args.verify:
            quest_ids = verify_plugin(args.output)
            print(f"verified {args.output}: " + ", ".join(f"0x{form_id:08X}" for form_id in quest_ids))
    except (OSError, MigrationError, generate_seq.PluginFormatError) as exc:
        print(f"startup migration failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
