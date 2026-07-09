#!/usr/bin/env python3
"""Patch the VR package ESP so the bundled spell opens the VR connection menu."""

from __future__ import annotations

import argparse
import pathlib
import struct


MGEF_FORM_ID = 0x02001824
SPEL_FORM_ID = 0x02001825
EFFECT_SCRIPT = "SkyrimTogetherVRConnectionSpellEffect"


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def u16(value: int) -> bytes:
    return struct.pack("<H", value)


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


def zstring(value: str) -> bytes:
    return value.encode("utf-8") + b"\0"


def bstring(value: str) -> bytes:
    data = value.encode("utf-8")
    return u16(len(data)) + data


def make_vmad(script_name: str) -> bytes:
    payload = bytearray()
    payload += u16(5)  # VMAD version
    payload += u16(2)  # Skyrim object format
    payload += u16(1)  # script count
    payload += bstring(script_name)
    payload += b"\0"   # script status
    payload += u16(0)  # property count
    return bytes(payload)


def iter_top_level(data: bytes):
    offset = 0
    while offset + 24 <= len(data):
        kind = data[offset:offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        if kind == b"GRUP":
            yield ("GRUP", offset, size, data[offset + 8:offset + 12])
            offset += size
        else:
            yield ("RECORD", offset, 24 + size, kind)
            offset += 24 + size


def iter_group_records(data: bytes, group_offset: int, group_size: int):
    offset = group_offset + 24
    end = group_offset + group_size
    while offset + 24 <= end:
        kind = data[offset:offset + 4]
        if kind == b"GRUP":
            size = struct.unpack_from("<I", data, offset + 4)[0]
            offset += size
            continue

        size = struct.unpack_from("<I", data, offset + 4)[0]
        form_id = struct.unpack_from("<I", data, offset + 12)[0]
        yield offset, kind, form_id, size
        offset += 24 + size


def parse_fields(body: bytes):
    fields = []
    offset = 0
    while offset + 6 <= len(body):
        kind = body[offset:offset + 4]
        size = struct.unpack_from("<H", body, offset + 4)[0]
        payload_start = offset + 6
        payload_end = payload_start + size
        if payload_end > len(body):
            raise ValueError(f"Field {kind!r} extends past record body")
        fields.append((kind, body[payload_start:payload_end]))
        offset = payload_end
    if offset != len(body):
        raise ValueError("Trailing bytes after final field")
    return fields


def build_fields(fields) -> bytes:
    out = bytearray()
    for kind, payload in fields:
        if len(payload) > 0xFFFF:
            raise ValueError(f"Field {kind!r} is too large for this patcher")
        out += kind
        out += u16(len(payload))
        out += payload
    return bytes(out)


def set_field(fields, kind: bytes, payload: bytes, *, after: bytes | None = None):
    for index, (field_kind, _) in enumerate(fields):
        if field_kind == kind:
            fields[index] = (kind, payload)
            return

    insert_at = len(fields)
    if after is not None:
        for index, (field_kind, _) in enumerate(fields):
            if field_kind == after:
                insert_at = index + 1
                break
    fields.insert(insert_at, (kind, payload))


def patch_record(data: bytearray, record_type: bytes, form_id: int, patch_body):
    for entry_type, group_offset, group_size, label in iter_top_level(bytes(data)):
        if entry_type != "GRUP" or label != record_type:
            continue

        for record_offset, kind, candidate_form_id, record_size in iter_group_records(bytes(data), group_offset, group_size):
            if kind != record_type or candidate_form_id != form_id:
                continue

            body_start = record_offset + 24
            body_end = body_start + record_size
            old_body = bytes(data[body_start:body_end])
            new_body = patch_body(old_body)
            delta = len(new_body) - len(old_body)
            if delta == 0 and new_body == old_body:
                return False

            data[record_offset + 4:record_offset + 8] = u32(len(new_body))
            data[body_start:body_end] = new_body
            if delta:
                old_group_size = struct.unpack_from("<I", data, group_offset + 4)[0]
                data[group_offset + 4:group_offset + 8] = u32(old_group_size + delta)
            return True

    raise SystemExit(f"Could not find {record_type.decode()} record {form_id:08X}")


def patch_magic_effect(body: bytes) -> bytes:
    fields = parse_fields(body)
    set_field(fields, b"VMAD", make_vmad(EFFECT_SCRIPT), after=b"EDID")
    set_field(fields, b"FULL", zstring("Skyrim Together VR Connection Effect"))
    set_field(fields, b"DNAM", zstring("Opens the Skyrim Together VR connection menu."))
    return build_fields(fields)


def patch_spell(body: bytes) -> bytes:
    fields = parse_fields(body)
    set_field(fields, b"FULL", zstring("Skyrim Together VR"))
    set_field(fields, b"DESC", zstring("Connect to or disconnect from the local Skyrim Together server."))

    for index, (kind, payload) in enumerate(fields):
        if kind == b"SPIT":
            if len(payload) < 4:
                raise ValueError("SPEL SPIT field is shorter than expected")
            payload = bytearray(payload)
            payload[0:4] = u32(3)  # MagicSystem::LESSER_POWER
            fields[index] = (kind, bytes(payload))
            break
    else:
        raise ValueError("SPEL record has no SPIT field")

    return build_fields(fields)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--esp",
        type=pathlib.Path,
        default=repo_root() / "GameFiles" / "SkyrimVR" / "SkyrimTogether.esp",
        help="ESP to patch.",
    )
    parser.add_argument("--check", action="store_true", help="Do not write; return non-zero if changes are needed.")
    args = parser.parse_args()

    path = args.esp.resolve()
    data = bytearray(path.read_bytes())
    original = bytes(data)

    changed_effect = patch_record(data, b"MGEF", MGEF_FORM_ID, patch_magic_effect)
    changed_spell = patch_record(data, b"SPEL", SPEL_FORM_ID, patch_spell)
    changed = changed_effect or changed_spell or bytes(data) != original

    if args.check:
        if changed:
            print(f"{path} needs VR connection menu ESP patch")
            return 1
        print(f"{path} already has VR connection menu ESP patch")
        return 0

    if changed:
        path.write_bytes(data)
        print(f"Patched {path}")
    else:
        print(f"{path} already patched")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
