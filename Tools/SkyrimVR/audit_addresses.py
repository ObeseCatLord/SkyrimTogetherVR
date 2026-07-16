#!/usr/bin/env python3
import argparse
import csv
import json
import os
import pathlib
import re
import sys
from collections import Counter, defaultdict

from vr_address_contract import VALIDATED_COMMONLIB_VR_ALIASES, VALIDATED_VR_ADDRESS_OVERRIDES

BASE = 0x140000000
MIN_AE_TO_SE_DATA_ROWS = 50
MIN_ADDRESS_OVERRIDE_DATA_ROWS = 2800
VALID_OVERRIDE_SOURCES = {"database", "addrlib", "sse_vr", "commonlib_vtable"}

POINTER_RE = re.compile(
    r"POINTER_SKYRIMSE(?:_LEGACY)?\s*\([^,\n]+,\s*[^,\n]+,\s*(\d+)"
    r"|VersionDbPtr<[^>]+>\s+\w+\s*\{\s*(\d+)\s*\}"
    r"|VersionDbPtr<[^>]+>\s+[\w:]+\s*\(\s*(\d+)\s*\)"
    r"|RttiLocator<[^>]+>\s+\w+\s*\(\s*(\d+)\s*\)"
)
RTTI_REGISTER_RE = re.compile(r"RttiLocator<([^>]+)>\s+\w+\((\d+)\)")
CAST_RE = re.compile(r"\bCast\s*<\s*([^>\s]+)\s*>")


def parse_int(value, base=0):
    value = value.strip().strip('"')
    if not value:
        return None

    try:
        return int(value, base)
    except ValueError:
        return None


def parse_vr_offset(value):
    value = value.strip().strip('"')
    if not value or "." in value:
        return None

    try:
        parsed = int(value, 16)
    except ValueError:
        return None

    if parsed >= BASE:
        return parsed - BASE

    return parsed


def read_release_csv(path):
    data = {}
    if not path.exists():
        return data

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.reader(handle):
            if len(row) < 2:
                continue

            address_id = parse_int(row[0], 10)
            offset = parse_vr_offset(row[1])
            if address_id is None or offset is None:
                continue

            data[address_id] = offset

    return data


def read_database_csv(path):
    data = {}
    if not path.exists():
        return data

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            address_id = parse_int(row.get("id", ""), 10)
            vr = parse_int(row.get("vr", ""), 0)
            if address_id is None or vr is None:
                continue

            data[address_id] = {
                "offset": vr - BASE if vr >= BASE else vr,
                "status": row.get("status", ""),
                "name": row.get("name", ""),
            }

    return data


def read_addrlib_csv(path):
    data = {}
    if not path.exists():
        return data

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            address_id = parse_int(row.get("id", ""), 10)
            vr = parse_int(row.get("vr", ""), 0)
            if address_id is None or vr is None:
                continue

            data[address_id] = vr - BASE if vr >= BASE else vr

    return data


def read_offsets_csv(path):
    data = {}
    if not path.exists():
        return data

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            address_id = parse_int(row.get("id", "") or row.get("sseid", ""), 10)
            sse = parse_int(row.get("sse", "") or row.get("sse_addr", ""), 16)
            if address_id is None or sse is None:
                continue

            data[address_id] = sse if sse >= BASE else sse + BASE

    return data


def read_sse_vr_csv(path):
    data = {}
    if not path.exists():
        return data

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            sse = parse_int(row.get("sse", ""), 0)
            vr = parse_int(row.get("vr", ""), 0)
            if sse is None or vr is None:
                continue

            data[sse] = vr - BASE if vr >= BASE else vr

    return data


def read_ae_to_se(path):
    data = {}
    if not path.exists():
        return data

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            se_id = parse_int(row.get("sseid", ""), 10)
            ae_id = parse_int(row.get("aeid", ""), 10)
            if se_id is None or ae_id is None:
                continue

            data[ae_id] = se_id

    return data


def find_source_refs(repo):
    refs = []
    for path in sorted((repo / "Code" / "client").rglob("*")):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue

        rel = path.relative_to(repo)
        text = path.read_text(encoding="utf-8", errors="ignore")
        for line_no, line in enumerate(text.splitlines(), 1):
            for match in POINTER_RE.finditer(line):
                address_id = int(next(group for group in match.groups() if group))
                refs.append(
                    {
                        "id": address_id,
                        "file": str(rel),
                        "line": line_no,
                        "is_rtti": "RTTI.cpp" in str(rel) or "RTTI.h" in str(rel),
                    }
                )

    return refs


def find_rtti_type_names(repo):
    path = repo / "Code" / "client" / "Games" / "Skyrim" / "RTTI.cpp"
    names = {}
    if not path.exists():
        return names

    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = RTTI_REGISTER_RE.search(line)
        if not match:
            continue

        names[int(match.group(2))] = match.group(1).strip()

    return names


def find_cast_refs(repo):
    refs_by_type = defaultdict(list)
    for path in sorted((repo / "Code" / "client").rglob("*")):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue

        rel = path.relative_to(repo)
        text = path.read_text(encoding="utf-8", errors="ignore")
        for line_no, line in enumerate(text.splitlines(), 1):
            line = line.split("//", 1)[0]
            for match in CAST_RE.finditer(line):
                refs_by_type[match.group(1).strip()].append({"file": str(rel), "line": line_no})

    return refs_by_type


def find_missing_rtti_cast_refs(repo, missing_rtti_ids):
    rtti_type_names = find_rtti_type_names(repo)
    cast_refs = find_cast_refs(repo)
    missing_cast_refs = []

    for address_id in missing_rtti_ids:
        type_name = rtti_type_names.get(address_id)
        if not type_name:
            continue

        refs = cast_refs.get(type_name)
        if refs:
            missing_cast_refs.append({"id": address_id, "type": type_name, "refs": refs})

    return missing_cast_refs


def resolve_id(address_id, ae_to_se, release, database, addrlib, offsets_1597, sse_vr):
    if address_id in VALIDATED_VR_ADDRESS_OVERRIDES:
        row = VALIDATED_VR_ADDRESS_OVERRIDES[address_id]
        return {
            "id": address_id,
            "resolved_id": address_id,
            "translation": "direct",
            "source": row["source"],
            "offset": row["offset"],
            "status": row["status"],
            "name": row["name"],
        }

    candidates = []
    se_id = ae_to_se.get(address_id)
    if se_id is not None:
        candidates.append(("ae_to_se", se_id))
    candidates.append(("direct", address_id))

    seen = set()
    for translation, candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)

        if candidate in release:
            return {
                "id": address_id,
                "resolved_id": candidate,
                "translation": translation,
                "source": "release",
                "offset": release[candidate],
                "status": "release",
                "name": "",
            }

        if candidate in database:
            row = database[candidate]
            return {
                "id": address_id,
                "resolved_id": candidate,
                "translation": translation,
                "source": "database",
                "offset": row["offset"],
                "status": row["status"],
                "name": row["name"],
            }

        if candidate in addrlib:
            return {
                "id": address_id,
                "resolved_id": candidate,
                "translation": translation,
                "source": "addrlib",
                "offset": addrlib[candidate],
                "status": "addrlib",
                "name": "",
            }

        sse_address = offsets_1597.get(candidate)
        if sse_address in sse_vr:
            return {
                "id": address_id,
                "resolved_id": candidate,
                "translation": translation,
                "source": "sse_vr",
                "offset": sse_vr[sse_address],
                "status": "exact_sse_vr",
                "name": "",
            }

    return {
        "id": address_id,
        "resolved_id": se_id if se_id is not None else address_id,
        "translation": "ae_to_se" if se_id is not None else "direct",
        "source": "missing",
        "offset": None,
        "status": "",
        "name": "",
    }


def write_runtime_csvs(out_dir, resolved):
    out_dir.mkdir(parents=True, exist_ok=True)

    aliases = {}
    overrides = {}
    override_meta = {}

    for item in resolved.values():
        if item["translation"] == "ae_to_se":
            aliases[item["id"]] = item["resolved_id"]

        if item["source"] in VALID_OVERRIDE_SOURCES and item["offset"] is not None:
            overrides[item["resolved_id"]] = item["offset"]
            override_meta[item["resolved_id"]] = item

    # Curated contracts are package requirements even when an ID is referenced
    # only by the CommonLib VR bridge rather than the legacy desktop wrappers
    # scanned by find_source_refs(). They also take precedence over generated
    # translations by definition.
    aliases.update(
        {desktop_id: metadata["vr_id"] for desktop_id, metadata in VALIDATED_COMMONLIB_VR_ALIASES.items()}
    )
    for address_id, metadata in VALIDATED_VR_ADDRESS_OVERRIDES.items():
        overrides[address_id] = metadata["offset"]
        override_meta[address_id] = metadata

    alias_path = out_dir / "SkyrimTogetherVR_AE_to_SE.csv"
    alias_temp_path = alias_path.with_name(f"{alias_path.name}.{os.getpid()}.tmp")
    with alias_temp_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["sseid", "aeid"])
        for ae_id, se_id in sorted(aliases.items(), key=lambda pair: (pair[1], pair[0])):
            writer.writerow([se_id, ae_id])
    alias_temp_path.replace(alias_path)

    override_path = out_dir / "SkyrimTogetherVR_AddressOverrides.csv"
    override_temp_path = override_path.with_name(f"{override_path.name}.{os.getpid()}.tmp")
    with override_temp_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["id", "offset", "source", "status", "name"])
        for address_id, offset in sorted(overrides.items()):
            meta = override_meta[address_id]
            writer.writerow([address_id, f"{offset:07x}", meta["source"], meta["status"], meta["name"]])
    override_temp_path.replace(override_path)

    return alias_path, override_path


def validate_runtime_csvs(alias_path, override_path, release):
    failures = []
    alias_rows = []
    override_rows = []
    override_offsets = {}
    override_metadata = {}

    with alias_path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != ["sseid", "aeid"]:
            failures.append(f"{alias_path}: unexpected header {reader.fieldnames!r}")
        seen_ae = set()
        previous_key = None
        for index, row in enumerate(reader, 2):
            se_id = parse_int(row.get("sseid", ""), 10)
            ae_id = parse_int(row.get("aeid", ""), 10)
            if se_id is None or se_id <= 0:
                failures.append(f"{alias_path}:{index}: invalid sseid {row.get('sseid')!r}")
                continue
            if ae_id is None or ae_id <= 0:
                failures.append(f"{alias_path}:{index}: invalid aeid {row.get('aeid')!r}")
                continue
            if ae_id in seen_ae:
                failures.append(f"{alias_path}:{index}: duplicate aeid {ae_id}")
            seen_ae.add(ae_id)
            key = (se_id, ae_id)
            if previous_key is not None and key < previous_key:
                failures.append(f"{alias_path}:{index}: rows are not sorted by sseid, aeid")
            previous_key = key
            alias_rows.append((se_id, ae_id))

    if len(alias_rows) < MIN_AE_TO_SE_DATA_ROWS:
        failures.append(
            f"{alias_path}: expected at least {MIN_AE_TO_SE_DATA_ROWS} data rows, found {len(alias_rows)}"
        )

    alias_row_set = set(alias_rows)
    for desktop_id, metadata in sorted(VALIDATED_COMMONLIB_VR_ALIASES.items()):
        vr_id = metadata["vr_id"]
        expected_row = (vr_id, desktop_id)
        if expected_row not in alias_row_set:
            failures.append(
                f"{alias_path}: missing validated CommonLib VR alias {vr_id},{desktop_id} ({metadata['name']})"
            )

        release_offset = release.get(vr_id)
        if release_offset != metadata["offset"]:
            actual = "missing" if release_offset is None else f"0x{release_offset:x}"
            failures.append(
                f"VR release ID {vr_id} ({metadata['name']}) expected 0x{metadata['offset']:x}, found {actual}"
            )

    with override_path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != ["id", "offset", "source", "status", "name"]:
            failures.append(f"{override_path}: unexpected header {reader.fieldnames!r}")
        seen_ids = set()
        previous_id = None
        for index, row in enumerate(reader, 2):
            address_id = parse_int(row.get("id", ""), 10)
            offset = parse_vr_offset(row.get("offset", ""))
            source = row.get("source", "")
            if address_id is None or address_id <= 0:
                failures.append(f"{override_path}:{index}: invalid id {row.get('id')!r}")
                continue
            if offset is None or offset <= 0:
                failures.append(f"{override_path}:{index}: invalid offset {row.get('offset')!r}")
            if source not in VALID_OVERRIDE_SOURCES:
                failures.append(f"{override_path}:{index}: invalid source {source!r}")
            if address_id in seen_ids:
                failures.append(f"{override_path}:{index}: duplicate id {address_id}")
            seen_ids.add(address_id)
            if previous_id is not None and address_id < previous_id:
                failures.append(f"{override_path}:{index}: rows are not sorted by id")
            previous_id = address_id
            override_rows.append(address_id)
            if offset is not None:
                override_offsets[address_id] = offset
                override_metadata[address_id] = (
                    offset,
                    source,
                    row.get("status", ""),
                    row.get("name", ""),
                )

    if len(override_rows) < MIN_ADDRESS_OVERRIDE_DATA_ROWS:
        failures.append(
            f"{override_path}: expected at least {MIN_ADDRESS_OVERRIDE_DATA_ROWS} data rows, found {len(override_rows)}"
        )

    for address_id, expected in sorted(VALIDATED_VR_ADDRESS_OVERRIDES.items()):
        actual = override_metadata.get(address_id)
        wanted = (expected["offset"], expected["source"], expected["status"], expected["name"])
        if actual is None:
            failures.append(f"{override_path}: missing validated VR address override {address_id} ({expected['name']})")
        elif actual != wanted:
            failures.append(
                f"{override_path}: validated VR address override {address_id} is {actual!r}, expected {wanted!r}"
            )

    for se_id, ae_id in alias_rows:
        source_offset = override_offsets.get(se_id, release.get(se_id))
        if source_offset is None:
            failures.append(f"{alias_path}: alias source {se_id} is absent from the base database and project overrides")
            continue
        target_override = override_offsets.get(ae_id)
        if target_override is not None and target_override != source_offset:
            failures.append(
                f"{alias_path}: alias {se_id}->{ae_id} resolves to 0x{source_offset:x}, "
                f"but explicit target override is 0x{target_override:x}"
            )

    return failures, len(alias_rows), len(override_rows)


def write_report(path, refs, resolved, missing_rtti_cast_refs, alias_rows, override_rows, csv_failures):
    path.parent.mkdir(parents=True, exist_ok=True)

    unique_ids = {ref["id"] for ref in refs}
    non_rtti_ids = {ref["id"] for ref in refs if not ref["is_rtti"]}
    rtti_ids = unique_ids - non_rtti_ids

    by_source = Counter(item["source"] for item in resolved.values())
    non_rtti_by_source = Counter(resolved[address_id]["source"] for address_id in non_rtti_ids)

    missing_non_rtti = sorted(address_id for address_id in non_rtti_ids if resolved[address_id]["source"] == "missing")
    missing_all = sorted(address_id for address_id in unique_ids if resolved[address_id]["source"] == "missing")

    refs_by_id = defaultdict(list)
    for ref in refs:
        refs_by_id[ref["id"]].append(ref)

    with path.open("w", encoding="utf-8") as handle:
        handle.write("# SkyrimTogetherVR Address Audit\n\n")
        handle.write("Generated by `Tools/SkyrimVR/audit_addresses.py`.\n\n")
        handle.write("## Summary\n\n")
        handle.write(f"- Unique address IDs: {len(unique_ids)}\n")
        handle.write(f"- Unique non-RTTI address IDs: {len(non_rtti_ids)}\n")
        handle.write(f"- Unique RTTI address IDs: {len(rtti_ids)}\n")
        handle.write(f"- Missing non-RTTI IDs: {len(missing_non_rtti)}\n")
        handle.write(f"- Missing IDs including RTTI: {len(missing_all)}\n\n")

        handle.write("## Resolution Sources\n\n")
        for source, count in sorted(by_source.items()):
            handle.write(f"- {source}: {count}\n")

        handle.write("\n## Non-RTTI Resolution Sources\n\n")
        for source, count in sorted(non_rtti_by_source.items()):
            handle.write(f"- {source}: {count}\n")

        handle.write("\n## Missing Non-RTTI IDs\n\n")
        if not missing_non_rtti:
            handle.write("None.\n")
        else:
            handle.write("| ID | Candidate ID | References |\n")
            handle.write("|---:|---:|---|\n")
            for address_id in missing_non_rtti:
                item = resolved[address_id]
                refs_text = "<br>".join(f"`{ref['file']}:{ref['line']}`" for ref in refs_by_id[address_id][:8])
                handle.write(f"| {address_id} | {item['resolved_id']} | {refs_text} |\n")

        missing_rtti = sorted(address_id for address_id in rtti_ids if resolved[address_id]["source"] == "missing")
        handle.write("\n## Missing RTTI IDs\n\n")
        if not missing_rtti:
            handle.write("None.\n")
        else:
            handle.write("| ID | Candidate ID | References |\n")
            handle.write("|---:|---:|---|\n")
            for address_id in missing_rtti:
                item = resolved[address_id]
                refs_text = "<br>".join(f"`{ref['file']}:{ref['line']}`" for ref in refs_by_id[address_id][:8])
                handle.write(f"| {address_id} | {item['resolved_id']} | {refs_text} |\n")

        handle.write("\n## Missing RTTI IDs Used By Cast<>\n\n")
        if not missing_rtti_cast_refs:
            handle.write("None.\n")
        else:
            handle.write("| ID | Type | Cast References |\n")
            handle.write("|---:|---|---|\n")
            for item in missing_rtti_cast_refs:
                refs_text = "<br>".join(f"`{ref['file']}:{ref['line']}`" for ref in item["refs"][:8])
                handle.write(f"| {item['id']} | `{item['type']}` | {refs_text} |\n")

        handle.write("\n## Runtime Helper CSVs\n\n")
        handle.write("- `GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv`\n")
        handle.write("- `GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv`\n")
        handle.write(f"- AE-to-SE helper CSV data rows: {alias_rows}\n")
        handle.write(f"- Address override helper CSV data rows: {override_rows}\n")
        handle.write(f"- Helper CSV validation failures: {len(csv_failures)}\n")
        handle.write("\nThe override CSV is generated from VR database/addrlib rows and exact `offsets-1.5.97.0.csv` plus `sse_vr.csv` matches that are not present in the public release CSV. Entries from `addrlib.csv` are partially automated and must be validated before enabling gameplay hooks.\n")


def fmt_offset(value):
    if value is None:
        return None
    return f"0x{value:x}"


def refs_for_id(refs_by_id, address_id, limit=8):
    return [
        {"file": ref["file"], "line": ref["line"], "isRtti": ref["is_rtti"]}
        for ref in refs_by_id[address_id][:limit]
    ]


def write_json_report(
    path,
    refs,
    resolved,
    missing_rtti_cast_refs,
    alias_rows,
    override_rows,
    csv_failures,
    *,
    refs_root,
    release_path,
    runtime_csv_dir,
):
    path.parent.mkdir(parents=True, exist_ok=True)

    unique_ids = {ref["id"] for ref in refs}
    non_rtti_ids = {ref["id"] for ref in refs if not ref["is_rtti"]}
    rtti_ids = unique_ids - non_rtti_ids
    refs_by_id = defaultdict(list)
    for ref in refs:
        refs_by_id[ref["id"]].append(ref)

    by_source = Counter(item["source"] for item in resolved.values())
    non_rtti_by_source = Counter(resolved[address_id]["source"] for address_id in non_rtti_ids)
    missing_non_rtti = sorted(address_id for address_id in non_rtti_ids if resolved[address_id]["source"] == "missing")
    missing_rtti = sorted(address_id for address_id in rtti_ids if resolved[address_id]["source"] == "missing")
    missing_all = sorted(address_id for address_id in unique_ids if resolved[address_id]["source"] == "missing")
    resolved_non_rtti = []

    for address_id in sorted(non_rtti_ids):
        item = resolved[address_id]
        resolved_non_rtti.append(
            {
                "id": address_id,
                "resolvedId": item["resolved_id"],
                "translation": item["translation"],
                "source": item["source"],
                "offset": fmt_offset(item["offset"]),
                "status": item["status"],
                "name": item["name"],
                "references": refs_for_id(refs_by_id, address_id),
            }
        )

    report = {
        "schema": "skyrim_together_vr_address_audit_v1",
        "generatedBy": "Tools/SkyrimVR/audit_addresses.py",
        "inputs": {
            "refsRoot": str(refs_root),
            "releaseCsv": str(release_path),
            "runtimeCsvDir": str(runtime_csv_dir),
        },
        "summary": {
            "uniqueAddressIds": len(unique_ids),
            "uniqueNonRttiAddressIds": len(non_rtti_ids),
            "uniqueRttiAddressIds": len(rtti_ids),
            "missingNonRttiIds": len(missing_non_rtti),
            "missingIdsIncludingRtti": len(missing_all),
            "missingRttiIdsUsedByCast": len(missing_rtti_cast_refs),
            "aeToSeHelperCsvRows": alias_rows,
            "addressOverrideHelperCsvRows": override_rows,
            "helperCsvValidationFailures": len(csv_failures),
        },
        "resolutionSources": dict(sorted(by_source.items())),
        "nonRttiResolutionSources": dict(sorted(non_rtti_by_source.items())),
        "missingNonRtti": [
            {
                "id": address_id,
                "candidateId": resolved[address_id]["resolved_id"],
                "references": refs_for_id(refs_by_id, address_id),
            }
            for address_id in missing_non_rtti
        ],
        "missingRtti": [
            {
                "id": address_id,
                "candidateId": resolved[address_id]["resolved_id"],
                "references": refs_for_id(refs_by_id, address_id),
            }
            for address_id in missing_rtti
        ],
        "missingRttiUsedByCast": [
            {
                "id": item["id"],
                "type": item["type"],
                "references": item["refs"],
            }
            for item in missing_rtti_cast_refs
        ],
        "runtimeHelperCsvs": {
            "aeToSe": "GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv",
            "addressOverrides": "GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv",
            "aeToSeDataRows": alias_rows,
            "addressOverrideDataRows": override_rows,
            "validationFailures": csv_failures,
        },
        "resolvedNonRtti": resolved_non_rtti,
    }
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--refs", type=pathlib.Path, default=pathlib.Path("../_refs/skyrim_vr_address_library"))
    parser.add_argument("--release", type=pathlib.Path, default=pathlib.Path("../_refs/VRAddressLibraryRelease/SKSE/Plugins/version-1-4-15-0.csv"))
    parser.add_argument("--report", type=pathlib.Path, default=pathlib.Path("Docs/SkyrimVR/address-audit.md"))
    parser.add_argument("--json-report", type=pathlib.Path, default=pathlib.Path("Docs/SkyrimVR/address-audit.json"))
    parser.add_argument("--runtime-csv-dir", type=pathlib.Path, default=pathlib.Path("GameFiles/SkyrimVR/Data/SKSE/Plugins"))
    args = parser.parse_args()

    repo = args.repo.resolve()
    refs_root = (repo / args.refs).resolve() if not args.refs.is_absolute() else args.refs
    release_path = (repo / args.release).resolve() if not args.release.is_absolute() else args.release

    release = read_release_csv(release_path)
    database = read_database_csv(refs_root / "database.csv")
    addrlib = read_addrlib_csv(refs_root / "addrlib.csv")
    offsets_1597 = read_offsets_csv(refs_root / "offsets-1.5.97.0.csv")
    sse_vr = read_sse_vr_csv(refs_root / "sse_vr.csv")
    ae_to_se = read_ae_to_se(refs_root / "se_ae.csv")
    ae_to_se.update(
        {desktop_id: metadata["vr_id"] for desktop_id, metadata in VALIDATED_COMMONLIB_VR_ALIASES.items()}
    )
    refs = find_source_refs(repo)

    unique_ids = sorted({ref["id"] for ref in refs})
    resolved = {address_id: resolve_id(address_id, ae_to_se, release, database, addrlib, offsets_1597, sse_vr) for address_id in unique_ids}
    missing_rtti = sorted({ref["id"] for ref in refs if ref["is_rtti"] and resolved[ref["id"]]["source"] == "missing"})
    missing_rtti_cast_refs = find_missing_rtti_cast_refs(repo, missing_rtti)

    alias_path, override_path = write_runtime_csvs(repo / args.runtime_csv_dir, resolved)
    csv_failures, alias_rows, override_rows = validate_runtime_csvs(alias_path, override_path, release)
    write_report(repo / args.report, refs, resolved, missing_rtti_cast_refs, alias_rows, override_rows, csv_failures)
    write_json_report(
        repo / args.json_report,
        refs,
        resolved,
        missing_rtti_cast_refs,
        alias_rows,
        override_rows,
        csv_failures,
        refs_root=refs_root,
        release_path=release_path,
        runtime_csv_dir=repo / args.runtime_csv_dir,
    )

    missing_non_rtti = sorted({ref["id"] for ref in refs if not ref["is_rtti"] and resolved[ref["id"]]["source"] == "missing"})
    print(f"Scanned {len(unique_ids)} unique address IDs")
    print(f"Missing non-RTTI IDs: {len(missing_non_rtti)}")
    print(f"Missing RTTI IDs used by Cast<>: {len(missing_rtti_cast_refs)}")
    print(f"AE-to-SE helper CSV data rows: {alias_rows}")
    print(f"Address override helper CSV data rows: {override_rows}")
    print(f"Address helper CSV validation failures: {len(csv_failures)}")
    for failure in csv_failures:
        print(f"- {failure}")

    if missing_non_rtti:
        print("Missing non-RTTI address IDs are not allowed in the SkyrimTogetherVR readiness gate.")
    if missing_rtti_cast_refs:
        print("Missing RTTI IDs used by Cast<> are not allowed in the SkyrimTogetherVR readiness gate.")

    return 1 if missing_non_rtti or missing_rtti_cast_refs or csv_failures else 0


if __name__ == "__main__":
    sys.exit(main())
