#!/usr/bin/env python3
"""Generate and verify the SEQ index for Start Game Enabled quests in a plugin.

Skyrim loads ``Data/Seq/<plugin>.seq`` before it can start a Start Game Enabled
quest.  The file is a packed, little-endian list of the relevant FormIDs; it
has no header.  Keeping this generator beside the staged ESP makes the asset
derivable instead of relying on an opaque hand-authored binary.
"""

import argparse
import pathlib
import struct
import sys
import zlib


RECORD_HEADER_SIZE = 24
GROUP_HEADER_SIZE = 24
COMPRESSED_RECORD_FLAG = 0x00040000
START_GAME_ENABLED_FLAG = 0x0010


class PluginFormatError(ValueError):
    """The input does not contain a structurally valid TES4 plugin."""


def read_u16(data, offset):
    if offset + 2 > len(data):
        raise PluginFormatError("unexpected end of file while reading a uint16")
    return int.from_bytes(data[offset : offset + 2], "little")


def read_u32(data, offset):
    if offset + 4 > len(data):
        raise PluginFormatError("unexpected end of file while reading a uint32")
    return int.from_bytes(data[offset : offset + 4], "little")


def iter_records(data, start=0, end=None):
    """Yield ``(signature, flags, form_id, payload)`` from records and groups."""
    if end is None:
        end = len(data)
    cursor = start

    while cursor < end:
        if cursor + RECORD_HEADER_SIZE > end:
            raise PluginFormatError(f"truncated record header at 0x{cursor:x}")

        signature = data[cursor : cursor + 4]
        size = read_u32(data, cursor + 4)
        if signature == b"GRUP":
            if size < GROUP_HEADER_SIZE:
                raise PluginFormatError(f"invalid GRUP size {size} at 0x{cursor:x}")
            group_end = cursor + size
            if group_end > end:
                raise PluginFormatError(f"truncated GRUP at 0x{cursor:x}")
            yield from iter_records(data, cursor + GROUP_HEADER_SIZE, group_end)
            cursor = group_end
            continue

        record_end = cursor + RECORD_HEADER_SIZE + size
        if record_end > end:
            raise PluginFormatError(f"truncated {signature!r} record at 0x{cursor:x}")

        flags = read_u32(data, cursor + 8)
        form_id = read_u32(data, cursor + 12)
        payload = data[cursor + RECORD_HEADER_SIZE : record_end]
        if flags & COMPRESSED_RECORD_FLAG:
            if len(payload) < 4:
                raise PluginFormatError(f"truncated compressed {signature!r} record at 0x{cursor:x}")
            expected_size = read_u32(payload, 0)
            try:
                payload = zlib.decompress(payload[4:])
            except zlib.error as exc:
                raise PluginFormatError(
                    f"could not decompress {signature!r} record at 0x{cursor:x}: {exc}"
                ) from exc
            if len(payload) != expected_size:
                raise PluginFormatError(
                    f"decompressed {signature!r} size mismatch at 0x{cursor:x}: "
                    f"expected {expected_size}, got {len(payload)}"
                )

        yield signature, flags, form_id, payload
        cursor = record_end


def iter_subrecords(payload):
    """Yield ``(signature, payload)`` while handling Bethesda ``XXXX`` sizes."""
    cursor = 0
    extended_size = None

    while cursor < len(payload):
        if cursor + 6 > len(payload):
            raise PluginFormatError(f"truncated subrecord header at 0x{cursor:x}")

        signature = payload[cursor : cursor + 4]
        size = read_u16(payload, cursor + 4)
        cursor += 6
        if signature == b"XXXX":
            if size != 4 or cursor + 4 > len(payload):
                raise PluginFormatError(f"invalid XXXX subrecord at 0x{cursor - 6:x}")
            extended_size = read_u32(payload, cursor)
            cursor += 4
            continue

        data_size = extended_size if extended_size is not None else size
        extended_size = None
        end = cursor + data_size
        if end > len(payload):
            raise PluginFormatError(f"truncated {signature!r} subrecord at 0x{cursor - 6:x}")
        yield signature, payload[cursor:end]
        cursor = end

    if extended_size is not None:
        raise PluginFormatError("XXXX subrecord without a following subrecord")


def plugin_master_count(data):
    """Return the number of masters recorded by a normal ESP/ESM header."""
    if len(data) < RECORD_HEADER_SIZE or data[:4] != b"TES4":
        raise PluginFormatError("input is not a TES4 plugin")
    header_size = read_u32(data, 4)
    header_end = RECORD_HEADER_SIZE + header_size
    if header_end > len(data):
        raise PluginFormatError("truncated TES4 header")
    return sum(1 for signature, _value in iter_subrecords(data[RECORD_HEADER_SIZE:header_end]) if signature == b"MAST")


def start_game_enabled_quest_ids(plugin):
    """Return new Start Game Enabled QUST FormIDs in their plugin record order.

    A plugin can override a master quest that is already Start Game Enabled.
    xEdit does not put that inherited startup into this plugin's SEQ file.  For a
    normal ESP, records owned by the plugin have a FormID top byte equal to the
    number of masters in its TES4 header; inherited records use a lower index.
    """
    data = pathlib.Path(plugin).read_bytes()
    if len(data) < RECORD_HEADER_SIZE or data[:4] != b"TES4":
        raise PluginFormatError(f"{plugin} is not a TES4 plugin")

    self_index = plugin_master_count(data)
    if self_index > 0xFD:
        raise PluginFormatError(f"unsupported master count for a normal ESP: {self_index}")

    quest_ids = []
    for signature, _flags, form_id, payload in iter_records(data):
        if signature != b"QUST" or (form_id >> 24) != self_index:
            continue
        dnam = next((value for kind, value in iter_subrecords(payload) if kind == b"DNAM"), None)
        if dnam is not None and len(dnam) >= 2 and read_u16(dnam, 0) & START_GAME_ENABLED_FLAG:
            quest_ids.append(form_id)
    return tuple(quest_ids)


def sequence_bytes(plugin):
    return b"".join(struct.pack("<I", form_id) for form_id in start_game_enabled_quest_ids(plugin))


def parse_form_id(value):
    form_id = int(value, 0)
    if not 0 <= form_id <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"FormID out of range: {value}")
    return form_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--write", action="store_true", help="write the generated SEQ asset")
    parser.add_argument("--verify", action="store_true", help="fail unless the existing asset is byte-exact")
    parser.add_argument(
        "--expected-form-id",
        action="append",
        type=parse_form_id,
        default=[],
        help="require a Start Game Enabled quest FormID (repeatable)",
    )
    args = parser.parse_args()

    if not args.write and not args.verify:
        parser.error("specify --write, --verify, or both")

    try:
        quest_ids = start_game_enabled_quest_ids(args.plugin)
        expected = b"".join(struct.pack("<I", form_id) for form_id in quest_ids)
    except (OSError, PluginFormatError) as exc:
        print(f"SEQ generation failed: {exc}", file=sys.stderr)
        return 1

    missing = sorted(set(args.expected_form_id) - set(quest_ids))
    if missing:
        print(
            "SEQ generation failed: expected Start Game Enabled quest FormID(s) missing: "
            + ", ".join(f"0x{form_id:08X}" for form_id in missing),
            file=sys.stderr,
        )
        return 1
    if not quest_ids:
        print("SEQ generation failed: plugin has no Start Game Enabled quests", file=sys.stderr)
        return 1

    if args.write:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(expected)
        print(f"wrote {args.output}: " + ", ".join(f"0x{form_id:08X}" for form_id in quest_ids))

    if args.verify:
        try:
            actual = args.output.read_bytes()
        except OSError as exc:
            print(f"SEQ verification failed: {exc}", file=sys.stderr)
            return 1
        if actual != expected:
            print(
                f"SEQ verification failed: {args.output} does not match {args.plugin}; "
                f"expected {expected.hex()}, got {actual.hex()}",
                file=sys.stderr,
            )
            return 1
        print(f"verified {args.output}: " + ", ".join(f"0x{form_id:08X}" for form_id in quest_ids))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
