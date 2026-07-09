#!/usr/bin/env python3
import argparse
import csv
import dataclasses
import json
import pathlib
import struct
import subprocess
import tempfile

import pefile

import vr_paths


STEAM_STUB_HEADER_SIZE = 240
STEAM_STUB_SIGNATURE = 0xC0DEC0DF
IMAGE_SCN_MEM_EXECUTE = 0x20000000


@dataclasses.dataclass
class CodeImage:
    source_path: pathlib.Path
    analysis_path: pathlib.Path
    pe: pefile.PE
    description: str
    disassembly_label: str
    validation_status: str
    validation_confidence: str
    ceg_detected: bool
    decrypted: bool
    notes: list[str]
    temp_dir: tempfile.TemporaryDirectory | None = None


PATCH_SITES = [
    {
        "name": "UI active menu queue SwapCall",
        "source": "Code/client/Games/Skyrim/Interface/UI.cpp:134",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED",
        "id": 82082,
        "source_addend": 0x682,
        "addend": 0x67B,
        "size": 5,
        "operation": "SwapCall",
        "active": True,
        "expect": "call",
        "notes": "Unfreezes allow-listed menus while connected. VR addend is the nearest call opcode identified from decrypted VR bytes; semantics still need manual review.",
    },
    {
        "name": "Skip startup movie branch",
        "source": "Code/client/Games/Skyrim/Interface/UI.cpp:141",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE_VR_RESOLVED",
        "id": 36548,
        "addend": 0xFE,
        "size": 1,
        "operation": "Put<uint8_t>(0xEB)",
        "active": True,
        "expect": "manual",
        "notes": "SE/AE branch offset must be rediscovered for VR.",
    },
    {
        "name": "Favorites menu can-process patch",
        "source": "Code/client/Games/Skyrim/Interface/UI.cpp:148",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS_VR_RESOLVED",
        "id": 51538,
        "addend": 0x15,
        "size": 2,
        "operation": "Put<uint16_t>(0x9090)",
        "active": True,
        "expect": "manual",
        "notes": "Imported from SkyrimSoulsRE behavior; VR menu path needs separate validation.",
    },
    {
        "name": "Renderer window style patch",
        "source": "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:109",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE_VR_RESOLVED",
        "id": 77226,
        "addend": 0x175,
        "size": 4,
        "operation": "Put(WS_OVERLAPPEDWINDOW)",
        "active": True,
        "expect": "manual",
        "notes": "Flat-window patch; may only affect mirror window in VR.",
    },
    {
        "name": "DirectInput exclusive mode patch",
        "source": "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:116",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE_VR_RESOLVED",
        "id": 68781,
        "addend": 0x57,
        "size": 4,
        "operation": "Put(3)",
        "active": True,
        "expect": "manual",
        "notes": "Flat DirectInput behavior does not cover VR controller actions.",
    },
    {
        "name": "Render timer SwapCall",
        "source": "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:124",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER",
        "id": 77246,
        "addend": 0x9,
        "size": 5,
        "operation": "SwapCall",
        "active": True,
        "expect": "call",
        "vr_source_blocked": True,
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER_VR_RESOLVED",
        "notes": "Drives current D3D11 overlay render path. No valid VR call boundary is currently encoded; this site is source-blocked for VR.",
    },
    {
        "name": "BSTaskletManager constructor SwapCall",
        "source": "Code/client/Games/Skyrim/BSSystem/BSTaskletManager.cpp:37",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR",
        "id": 69554,
        "addend": 0x63,
        "size": 5,
        "operation": "SwapCall",
        "active": True,
        "expect": "call",
        "vr_source_blocked": True,
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR_VR_RESOLVED",
        "notes": "Thread naming helper. No valid VR call boundary is currently encoded; this site is source-blocked for VR.",
    },
    {
        "name": "SetThreadName entry Jump",
        "source": "Code/client/Games/Skyrim/BSSystem/BSThread.cpp:86",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SET_THREAD_NAME",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SET_THREAD_NAME_VR_RESOLVED",
        "id": 69066,
        "addend": 0x0,
        "size": 5,
        "operation": "Jump",
        "active": True,
        "expect": "function",
        "notes": "Entry detour; target signature must match VR.",
    },
    {
        "name": "Projectile null-shooter Jump",
        "source": "Code/client/Games/Skyrim/Projectiles/Projectile.cpp:155",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER_VR_RESOLVED",
        "id": 34452,
        "addend": 0x374,
        "size": 5,
        "operation": "Jump",
        "active": True,
        "expect": "manual",
        "notes": "Intra-function control-flow patch; especially sensitive to VR compiler layout.",
    },
    {
        "name": "Skills menu process message NOP 1",
        "source": "Code/client/Games/Skyrim/Interface/Menus/SkillsMenu.cpp:14",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE_VR_RESOLVED",
        "id": 52510,
        "addend": 0x84E,
        "size": 6,
        "operation": "Nop",
        "active": True,
        "expect": "manual",
        "notes": "Stats/skills menu behavior from SkyrimSoulsRE.",
    },
    {
        "name": "Skills menu process message NOP 2",
        "source": "Code/client/Games/Skyrim/Interface/Menus/SkillsMenu.cpp:16",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE_VR_RESOLVED",
        "id": 52510,
        "addend": 0xA10,
        "size": 4,
        "operation": "Nop",
        "active": True,
        "expect": "manual",
        "notes": "Stats/skills menu freeze-frame flag patch.",
    },
    {
        "name": "Skills menu process message NOP 3",
        "source": "Code/client/Games/Skyrim/Interface/Menus/SkillsMenu.cpp:18",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE_VR_RESOLVED",
        "id": 52510,
        "addend": 0x1040,
        "size": 2,
        "operation": "Nop",
        "active": True,
        "expect": "manual",
        "notes": "Stats/skills menu live-update patch.",
    },
    {
        "name": "Skills menu control patch NOP 1",
        "source": "Code/client/Games/Skyrim/Interface/Menus/SkillsMenu.cpp:24",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS_VR_RESOLVED",
        "id": 52518,
        "addend": 0x46,
        "size": 4,
        "operation": "Nop",
        "active": True,
        "expect": "manual",
        "notes": "Stats/skills controls patch.",
    },
    {
        "name": "Skills menu control patch NOP 2",
        "source": "Code/client/Games/Skyrim/Interface/Menus/SkillsMenu.cpp:25",
        "vr_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS",
        "vr_resolution_flag": "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS_VR_RESOLVED",
        "id": 52518,
        "addend": 0x4A,
        "size": 2,
        "operation": "Nop",
        "active": True,
        "expect": "manual",
        "notes": "Stats/skills controls patch.",
    },
    {
        "name": "Map menu first-person NOP 1",
        "source": "Code/client/Games/Skyrim/Interface/Menus/MapMenu.cpp:13",
        "id": 53112,
        "addend": 0x53,
        "size": 4,
        "operation": "Nop",
        "active": False,
        "expect": "manual",
        "notes": "Currently under #if 0 upstream; keep disabled for VR.",
    },
    {
        "name": "Map menu first-person NOP 2",
        "source": "Code/client/Games/Skyrim/Interface/Menus/MapMenu.cpp:14",
        "id": 53112,
        "addend": 0x9D,
        "size": 2,
        "operation": "Nop",
        "active": False,
        "expect": "manual",
        "notes": "Currently under #if 0 upstream; keep disabled for VR.",
    },
    {
        "name": "Map menu first-person NOP 3",
        "source": "Code/client/Games/Skyrim/Interface/Menus/MapMenu.cpp:15",
        "id": 53112,
        "addend": 0x9F,
        "size": 1,
        "operation": "Nop",
        "active": False,
        "expect": "manual",
        "notes": "Currently under #if 0 upstream; keep disabled for VR.",
    },
    {
        "name": "CreateHavokThread RaiseException patch",
        "source": "Code/client/Games/Skyrim/BSSystem/BSThread.cpp:89",
        "id": 57704,
        "addend": 0x81,
        "size": 6,
        "operation": "Nop+PutCall",
        "active": False,
        "expect": "call",
        "notes": "Currently under #if 0 upstream.",
    },
    {
        "name": "Hardcoded BNetThread CreateThread patch",
        "source": "Code/client/Games/Skyrim/BSSystem/BSThread.cpp:94",
        "absolute": 0x1411F0FD4,
        "size": 6,
        "operation": "Nop+PutCall",
        "active": False,
        "expect": "call",
        "notes": "Hardcoded SE/AE address under #if 0; must never be reused for VR.",
    },
]


REQUIRED_SOURCE_TOKENS = {
    "Code/client/Games/Skyrim/Interface/UI.cpp": (
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE, TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED)",
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE, TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE_VR_RESOLVED)",
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS, TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS_VR_RESOLVED)",
        "kUIActiveMenuQueueSwapCallAddend = 0x67B",
        "kUIActiveMenuQueueSwapCallAddend = 0x682",
        "ProcessHook.Get() + kUIActiveMenuQueueSwapCallAddend",
    ),
    "Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp": (
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE, TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE_VR_RESOLVED)",
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE, TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE_VR_RESOLVED)",
        "TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER, TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER_VR_RESOLVED)",
        "timerLoc.GetPtr()) + 9",
    ),
    "Code/client/Games/Skyrim/BSSystem/BSTaskletManager.cpp": (
        "TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR, TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR_VR_RESOLVED)",
        "getTaskletManagerInstance.Get() + 0x63",
    ),
    "Code/client/Games/Skyrim/BSSystem/BSThread.cpp": (
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_SET_THREAD_NAME, TP_SKYRIM_VR_INLINE_PATCH_SET_THREAD_NAME_VR_RESOLVED)",
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.cpp": (
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER, TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER_VR_RESOLVED)",
    ),
    "Code/client/Games/Skyrim/Interface/Menus/SkillsMenu.cpp": (
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE, TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE_VR_RESOLVED)",
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH(TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS, TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS_VR_RESOLVED)",
    ),
    "Code/client/Games/Skyrim/VR/VRHookPolicy.h": (
        "TP_SKYRIM_VR_INLINE_PATCH_UI_ACTIVE_MENU_QUEUE_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_SKIP_STARTUP_MOVIE_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_FAVORITES_CAN_PROCESS_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_RENDERER_WINDOW_STYLE_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_DIRECTINPUT_NONEXCLUSIVE_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_RENDER_TIMER_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_TASKLET_MANAGER_CTOR_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_SET_THREAD_NAME_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_PROJECTILE_NULL_SHOOTER_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_PROCESS_MESSAGE_VR_RESOLVED",
        "TP_SKYRIM_VR_INLINE_PATCH_SKILLS_MENU_CONTROLS_VR_RESOLVED",
        "TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH",
        "TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH",
    ),
}


def parse_int(value, base=0):
    value = value.strip().strip('"')
    if not value:
        return None

    try:
        return int(value, base)
    except ValueError:
        return None


def load_offset_csv(path, data):
    if not path.exists():
        return

    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.reader(handle):
            if len(row) < 2:
                continue
            address_id = parse_int(row[0], 10)
            offset = parse_int(row[1], 16)
            if address_id is None or offset is None:
                continue
            data[address_id] = offset


def load_aliases(path, data):
    if not path.exists():
        return

    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            se_id = parse_int(row.get("sseid", ""), 10)
            ae_id = parse_int(row.get("aeid", ""), 10)
            if se_id is None or ae_id is None:
                continue
            if se_id in data and ae_id not in data:
                data[ae_id] = data[se_id]


def load_runtime_offsets(repo, release):
    data = {}
    load_offset_csv(release, data)
    load_offset_csv(repo / "GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AddressOverrides.csv", data)
    load_aliases(repo / "GameFiles/SkyrimVR/Data/SKSE/Plugins/SkyrimTogetherVR_AE_to_SE.csv", data)
    return data


def find_executable_text_section(pe):
    for section in pe.sections:
        name = section.Name.rstrip(b"\0").decode("ascii", errors="replace")
        if name == ".text" and section.Characteristics & IMAGE_SCN_MEM_EXECUTE:
            return section
    for section in pe.sections:
        name = section.Name.rstrip(b"\0").decode("ascii", errors="replace")
        if name == ".text":
            return section
    return None


def decode_steam_xor(data):
    decoded = bytearray(data)
    if len(decoded) < 8:
        return decoded

    key = struct.unpack_from("<I", decoded, 0)[0]
    for offset in range(4, len(decoded), 4):
        value = struct.unpack_from("<I", decoded, offset)[0]
        struct.pack_into("<I", decoded, offset, value ^ key)
        key = value
    return decoded


def read_u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def read_u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


def detect_steam_ceg(pe):
    entry_rva = pe.OPTIONAL_HEADER.AddressOfEntryPoint
    try:
        entry_bytes = pe.get_data(entry_rva, 4)
    except Exception:
        return False
    if len(entry_bytes) < 4:
        return False
    return int.from_bytes(entry_bytes, "little") == 0xE8


def aes_decrypt_ecb(key, data):
    try:
        from Crypto.Cipher import AES

        return AES.new(key, AES.MODE_ECB).decrypt(data)
    except ImportError as exc:
        try:
            from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        except ImportError:
            raise RuntimeError("neither PyCryptodome nor cryptography AES is available") from exc

        decryptor = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
        return decryptor.update(data) + decryptor.finalize()


def aes_decrypt_cbc(key, iv, data):
    try:
        from Crypto.Cipher import AES

        return AES.new(key, AES.MODE_CBC, iv).decrypt(data)
    except ImportError as exc:
        try:
            from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        except ImportError:
            raise RuntimeError("neither PyCryptodome nor cryptography AES is available") from exc

        decryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
        return decryptor.update(data) + decryptor.finalize()


def decrypt_ceg_image(exe, pe):
    aes_block_size = 16

    text_section = find_executable_text_section(pe)
    if text_section is None:
        raise RuntimeError("could not locate executable .text section")

    image = bytearray(exe.read_bytes())
    entry_rva = pe.OPTIONAL_HEADER.AddressOfEntryPoint
    entry_offset = pe.get_offset_from_rva(entry_rva)
    header_offset = entry_offset - STEAM_STUB_HEADER_SIZE
    if header_offset < 0:
        raise RuntimeError("Steam stub header would start before the file")

    encoded_header = image[header_offset:entry_offset]
    if len(encoded_header) != STEAM_STUB_HEADER_SIZE:
        raise RuntimeError("Steam stub header is truncated")

    stub = decode_steam_xor(encoded_header)
    signature = read_u32(stub, 4)
    if signature != STEAM_STUB_SIGNATURE:
        raise RuntimeError(f"Steam stub signature mismatch: 0x{signature:08x}")

    aes_key = bytes(stub[88:120])
    encrypted_iv = bytes(stub[120:136])
    stolen_text = bytes(stub[136:152])
    original_entry_point = read_u64(stub, 32)
    code_virtual_address = read_u64(stub, 72)
    code_raw_size = read_u64(stub, 80)

    if code_virtual_address != text_section.VirtualAddress:
        raise RuntimeError(
            f"Steam stub code RVA 0x{code_virtual_address:x} does not match .text RVA 0x{text_section.VirtualAddress:x}"
        )
    if code_raw_size != text_section.SizeOfRawData:
        raise RuntimeError(
            f"Steam stub code size 0x{code_raw_size:x} does not match .text raw size 0x{text_section.SizeOfRawData:x}"
        )
    if text_section.SizeOfRawData % aes_block_size != 0:
        raise RuntimeError(".text raw size is not AES-block aligned")

    text_start = text_section.PointerToRawData
    text_size = text_section.SizeOfRawData
    encrypted_text = bytes(image[text_start : text_start + text_size])
    if len(encrypted_text) != text_size:
        raise RuntimeError(".text section is truncated")

    iv = aes_decrypt_ecb(aes_key, encrypted_iv)
    restored_text = stolen_text + encrypted_text[: text_size - len(stolen_text)]
    decrypted_text = aes_decrypt_cbc(aes_key, iv, restored_text)
    image[text_start : text_start + text_size] = decrypted_text

    nt_offset = pe.DOS_HEADER.e_lfanew
    optional_header_offset = nt_offset + 4 + 20
    if original_entry_point <= 0xFFFFFFFF:
        struct.pack_into("<I", image, optional_header_offset + 16, int(original_entry_point))

    temp_dir = tempfile.TemporaryDirectory(prefix="skyrimvr-ceg-")
    analysis_path = pathlib.Path(temp_dir.name) / f"{exe.stem}.ceg-decrypted{exe.suffix}"
    analysis_path.write_bytes(image)

    notes = [
        "Steam CEG entry stub detected at the executable entry point.",
        "The audit restored the stolen first 16 .text bytes and AES-CBC decrypted the executable .text section in a temporary analysis copy.",
        f"Original entry point from Steam stub: 0x{original_entry_point:x}.",
    ]
    return analysis_path, temp_dir, notes


def prepare_code_image(exe, decrypted_exe=None, disable_ceg_decrypt=False):
    source_pe = pefile.PE(str(exe), fast_load=False)
    ceg_detected = detect_steam_ceg(source_pe)

    if decrypted_exe is not None:
        analysis_path = decrypted_exe
        analysis_pe = pefile.PE(str(analysis_path), fast_load=False)
        return CodeImage(
            source_path=exe,
            analysis_path=analysis_path,
            pe=analysis_pe,
            description="externally supplied decrypted executable image",
            disassembly_label=str(analysis_path),
            validation_status="ready for static byte validation using supplied decrypted image",
            validation_confidence="manual provenance check required",
            ceg_detected=ceg_detected,
            decrypted=True,
            notes=[
                "A decrypted executable was supplied through --decrypted-exe.",
                "The audit trusts this file for byte reads and disassembly; ensure it came from the same SkyrimVR build as the address library.",
            ],
        )

    if ceg_detected and not disable_ceg_decrypt:
        try:
            analysis_path, temp_dir, notes = decrypt_ceg_image(exe, source_pe)
            analysis_pe = pefile.PE(str(analysis_path), fast_load=False)
            return CodeImage(
                source_path=exe,
                analysis_path=analysis_path,
                pe=analysis_pe,
                description="temporary CEG-decrypted executable image",
                disassembly_label="<CEG-decrypted SkyrimVR.exe image>",
                validation_status="ready for static byte validation using auto-decrypted .text",
                validation_confidence="high for opcode bytes; semantic hook safety still requires manual VR disassembly review",
                ceg_detected=True,
                decrypted=True,
                notes=notes,
                temp_dir=temp_dir,
            )
        except RuntimeError as exc:
            return CodeImage(
                source_path=exe,
                analysis_path=exe,
                pe=source_pe,
                description="raw on-disk executable image",
                disassembly_label=str(exe),
                validation_status="blocked: CEG-protected image could not be decrypted",
                validation_confidence="low",
                ceg_detected=True,
                decrypted=False,
                notes=[str(exc), "Patch-site opcode bytes from this image are not reliable for validation."],
            )

    if ceg_detected:
        return CodeImage(
            source_path=exe,
            analysis_path=exe,
            pe=source_pe,
            description="raw on-disk CEG-protected executable image",
            disassembly_label=str(exe),
            validation_status="blocked: CEG-protected image inspected without decryption",
            validation_confidence="low",
            ceg_detected=True,
            decrypted=False,
            notes=["Patch-site opcode bytes from this image are encrypted and are not reliable for validation."],
        )

    return CodeImage(
        source_path=exe,
        analysis_path=exe,
        pe=source_pe,
        description="raw on-disk executable image",
        disassembly_label=str(exe),
        validation_status="ready for static byte validation",
        validation_confidence="medium; semantic hook safety still requires manual VR disassembly review",
        ceg_detected=False,
        decrypted=False,
        notes=["No Steam CEG entry stub was detected."],
    )


def bytes_hex(data):
    return " ".join(f"{byte:02x}" for byte in data)


def classify(site, data, call_target=None, text_start=None, text_end=None):
    if data is None:
        return "missing bytes"
    if len(data) < site["size"]:
        return "short read"
    if site["expect"] == "call":
        if data[0] != 0xE8:
            return "not a call opcode"
        if call_target is None:
            return "call opcode with unreadable target"
        if text_start is not None and text_end is not None and not (text_start <= call_target < text_end):
            return "call opcode with out-of-text target"
        return "candidate call"
    if site["expect"] == "function":
        return "entry bytes captured"
    return "bytes captured"


def is_text_address(address, text_start, text_end):
    return address is not None and text_start <= address < text_end


def find_nearest_call_delta(pe, image_base, rva, text_start, text_end, radius=32):
    if rva is None:
        return None

    start = max(rva - radius, 0)
    try:
        data = pe.get_data(start, radius * 2 + 5)
    except Exception:
        return None

    best = None
    for index, byte in enumerate(data):
        if byte != 0xE8:
            continue
        call_rva = start + index
        call_target = read_call_target(pe, image_base, call_rva)
        if not is_text_address(call_target, text_start, text_end):
            continue
        delta = start + index - rva
        if best is None or abs(delta) < abs(best):
            best = delta
    return best


def read_call_target(pe, image_base, rva):
    if rva is None:
        return None

    try:
        data = pe.get_data(rva, 5)
    except Exception:
        return None

    if len(data) < 5 or data[0] != 0xE8:
        return None

    rel32 = struct.unpack_from("<i", data, 1)[0]
    return image_base + rva + 5 + rel32


def capstone_disassemble(exe, address, size, display_name):
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    except ImportError:
        return ""

    try:
        pe = pefile.PE(str(exe), fast_load=False)
    except Exception:
        return ""

    image_base = pe.OPTIONAL_HEADER.ImageBase
    start = max(address - 8, image_base)
    stop = address + max(size, 16) + 16
    if stop <= start:
        return ""

    rva = start - image_base
    try:
        data = pe.get_data(rva, stop - start)
    except Exception:
        return ""
    if not data:
        return ""

    disassembler = Cs(CS_ARCH_X86, CS_MODE_64)
    lines = [f"{display_name}:"]
    for instruction in disassembler.disasm(data, start):
        op_str = f" {instruction.op_str}" if instruction.op_str else ""
        lines.append(f"{instruction.address:x}:\t{instruction.mnemonic}{op_str}")
    return "\n".join(lines[-18:])


def disassemble(exe, address, size, display_name):
    start = max(address - 8, 0)
    stop = address + max(size, 16) + 16
    try:
        result = subprocess.run(
            [
                "llvm-objdump",
                "-d",
                "--no-show-raw-insn",
                f"--start-address=0x{start:x}",
                f"--stop-address=0x{stop:x}",
                str(exe),
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return capstone_disassemble(exe, address, size, display_name)

    if result.returncode != 0:
        return result.stderr.strip() or capstone_disassemble(exe, address, size, display_name)

    lines = []
    for line in result.stdout.splitlines():
        if line.strip():
            lines.append(line.rstrip().replace(str(exe), display_name))
    return "\n".join(lines[-18:])


def inspect_sites(code_image, repo, release):
    pe = code_image.pe
    image_base = pe.OPTIONAL_HEADER.ImageBase
    text_section = find_executable_text_section(pe)
    if text_section is None:
        text_start = image_base
        text_end = image_base
    else:
        text_start = image_base + text_section.VirtualAddress
        text_end = text_start + max(text_section.Misc_VirtualSize, text_section.SizeOfRawData)
    offsets = load_runtime_offsets(repo, release)
    rows = []

    for site in PATCH_SITES:
        vr_default_active = site.get("vr_default_active", False)
        if "absolute" in site:
            address = site["absolute"]
            rva = address - image_base
            resolved = None
        else:
            resolved = offsets.get(site["id"])
            address = image_base + resolved + site["addend"] if resolved is not None else None
            rva = resolved + site["addend"] if resolved is not None else None

        data = None
        file_offset = None
        if rva is not None:
            try:
                file_offset = pe.get_offset_from_rva(rva)
                data = pe.get_data(rva, max(site["size"], 16))
            except Exception:
                data = None
        call_target = read_call_target(pe, image_base, rva) if site["expect"] == "call" else None

        rows.append(
            {
                **site,
                "vr_default_active": vr_default_active,
                "resolved": resolved,
                "rva": rva,
                "address": address,
                "file_offset": file_offset,
                "bytes": data,
                "classification": classify(site, data, call_target, text_start, text_end),
                "nearest_call_delta": find_nearest_call_delta(pe, image_base, rva, text_start, text_end)
                if site["expect"] == "call"
                else None,
                "call_target": call_target,
                "disassembly": disassemble(
                    code_image.analysis_path, address, site["size"], code_image.disassembly_label
                )
                if address is not None
                else "",
            }
        )

    return image_base, rows


def audit_required_source_tokens(repo):
    missing = []
    for rel_path, tokens in REQUIRED_SOURCE_TOKENS.items():
        path = repo / rel_path
        try:
            text = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            missing.extend((rel_path, token) for token in tokens)
            continue

        for token in tokens:
            if token not in text:
                missing.append((rel_path, token))
    return missing


def audit_vr_guard_policy(repo, rows):
    failures = []
    policy_path = repo / "Code/client/Games/Skyrim/VR/VRHookPolicy.h"
    try:
        policy_text = policy_path.read_text(encoding="utf-8")
    except FileNotFoundError:
        policy_text = ""
        failures.append("missing VRHookPolicy.h")

    active_vr_rows = [row for row in rows if row["active"] and row.get("vr_flag")]
    unique_resolution_flags = sorted({row.get("vr_resolution_flag", "") for row in active_vr_rows})
    source_blocked_rows = [row for row in active_vr_rows if row.get("vr_source_blocked", False)]
    missing_resolution_metadata = [row["name"] for row in active_vr_rows if not row.get("vr_resolution_flag")]
    if missing_resolution_metadata:
        failures.append(
            "active inline patch sites missing VR resolution metadata: "
            + ", ".join(missing_resolution_metadata)
        )
    if source_blocked_rows and "TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH" not in policy_text:
        failures.append("VRHookPolicy.h is missing TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH")

    for resolution_flag in unique_resolution_flags:
        if not resolution_flag:
            continue
        if f"#ifndef {resolution_flag}" not in policy_text or f"#define {resolution_flag} 0" not in policy_text:
            failures.append(f"VR resolution flag does not default to 0 in VRHookPolicy.h: {resolution_flag}")

    source_paths = sorted({row["source"].split(":", 1)[0] for row in active_vr_rows})
    source_texts = {}
    for rel_path in source_paths:
        path = repo / rel_path
        try:
            source_texts[rel_path] = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            source_texts[rel_path] = ""
            failures.append(f"missing source file for inline patch guard audit: {rel_path}")

    for row in active_vr_rows:
        resolution_flag = row.get("vr_resolution_flag")
        if not resolution_flag:
            continue
        rel_path = row["source"].split(":", 1)[0]
        text = source_texts.get(rel_path, "")
        if row.get("vr_source_blocked", False):
            expected_guard = (
                f"TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH({row['vr_flag']}, {resolution_flag})"
            )
            forbidden_guard = (
                f"TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH({row['vr_flag']}, {resolution_flag})"
            )
            if expected_guard not in text:
                failures.append(f"{rel_path} missing source-blocked guard for {row['name']}: {expected_guard}")
            if forbidden_guard in text:
                failures.append(f"{rel_path} uses resolved guard for source-blocked {row['name']}: {forbidden_guard}")
        else:
            expected_guard = (
                f"TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH({row['vr_flag']}, {resolution_flag})"
            )
            if expected_guard not in text:
                failures.append(f"{rel_path} missing resolved guard for {row['name']}: {expected_guard}")

    for rel_path, text in source_texts.items():
        if "TP_SKYRIM_ALLOW_VR_INLINE_PATCH(" in text:
            failures.append(f"{rel_path} still uses one-key TP_SKYRIM_ALLOW_VR_INLINE_PATCH")

    return failures


def fmt_hex(value):
    if value is None:
        return ""
    return f"0x{value:x}"


def fmt_signed_hex(value):
    if value is None:
        return ""
    sign = "-" if value < 0 else "+"
    return f"{sign}0x{abs(value):x}"


def fmt_call_hint(row):
    delta = row.get("nearest_call_delta")
    if delta is None:
        return "no nearby call opcode within +/-0x20"
    if delta == 0:
        return "call opcode at patch address"
    if "addend" in row:
        return f"nearest call opcode at {fmt_signed_hex(delta)}; candidate addend `{fmt_hex(row['addend'] + delta)}`"
    return f"nearest call opcode at {fmt_signed_hex(delta)}"


def vr_source_guard(row):
    resolution_flag = row.get("vr_resolution_flag")
    vr_flag = row.get("vr_flag")
    if not resolution_flag or not vr_flag:
        return ""
    if row.get("vr_source_blocked", False):
        return f"TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH({vr_flag}, {resolution_flag})"
    return f"TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH({vr_flag}, {resolution_flag})"


def collect_safety_failures(code_image, rows, missing_source_tokens, guard_policy_failures):
    failures = []
    active = [row for row in rows if row["active"]]
    vr_default_active = [row for row in rows if row["vr_default_active"]]
    missing = [row for row in rows if row["bytes"] is None]
    unblocked_call_mismatch = [
        row
        for row in active
        if row["expect"] == "call"
        and row["classification"] != "candidate call"
        and not row.get("vr_source_blocked", False)
    ]
    vr_call_mismatch = [
        row
        for row in vr_default_active
        if row["expect"] == "call" and row["classification"] != "candidate call"
    ]
    source_blocked_without_resolution_gate = [
        row for row in rows if row.get("vr_source_blocked", False) and not row.get("vr_resolution_flag")
    ]
    source_blocked_default_active = [
        row for row in vr_default_active if row.get("vr_source_blocked", False)
    ]
    default_enablement_without_evidence = [
        row
        for row in active
        if default_enablement_allowed(row)
        and (
            not row.get("manual_validation_evidence")
            or not row.get("runtime_validation_evidence")
        )
    ]
    source_blocked_default_enablement = [
        row for row in active if row.get("vr_source_blocked", False) and default_enablement_allowed(row)
    ]

    if code_image.ceg_detected and not code_image.decrypted:
        failures.append("CEG-protected executable bytes were inspected without decryption.")
    if missing:
        failures.append(
            "Missing byte reads: " + ", ".join(row["name"] for row in missing)
        )
    if missing_source_tokens:
        failures.append(
            "Missing source guard/addend tokens: "
            + ", ".join(f"{rel_path}::{token}" for rel_path, token in missing_source_tokens)
        )
    if guard_policy_failures:
        failures.extend(f"VR inline patch guard policy failure: {failure}" for failure in guard_policy_failures)
    if vr_default_active:
        failures.append(
            "Inline patch sites active in the default VR target: "
            + ", ".join(row["name"] for row in vr_default_active)
        )
    if unblocked_call_mismatch:
        failures.append(
            "Active call-site opcode mismatches without a VR source block: "
            + ", ".join(row["name"] for row in unblocked_call_mismatch)
        )
    if vr_call_mismatch:
        failures.append(
            "Default VR call-site opcode mismatches: "
            + ", ".join(row["name"] for row in vr_call_mismatch)
        )
    if source_blocked_without_resolution_gate:
        failures.append(
            "Source-blocked VR call sites without a dedicated resolution gate: "
            + ", ".join(row["name"] for row in source_blocked_without_resolution_gate)
        )
    if source_blocked_default_active:
        failures.append(
            "Source-blocked VR call sites active in the default VR target: "
            + ", ".join(row["name"] for row in source_blocked_default_active)
        )
    if default_enablement_without_evidence:
        failures.append(
            "Inline patch sites marked default-enableable without manual/runtime evidence: "
            + ", ".join(row["name"] for row in default_enablement_without_evidence)
        )
    if source_blocked_default_enablement:
        failures.append(
            "Source-blocked inline patch sites marked default-enableable: "
            + ", ".join(row["name"] for row in source_blocked_default_enablement)
        )

    return failures


def semantic_validation_status(row):
    if not row["active"]:
        return "not-required-disabled-upstream"
    if row.get("vr_source_blocked", False):
        return "blocked-no-validated-vr-instruction-boundary"
    if row["classification"] == "candidate call":
        return "static-call-candidate-manual-review-required"
    if row["classification"] == "entry bytes captured":
        return "static-entry-bytes-captured-manual-review-required"
    return "static-bytes-captured-manual-review-required"


def disassembly_line_count(row):
    return len([line for line in row.get("disassembly", "").splitlines() if line.strip()])


def disassembly_review_status(row):
    if not row.get("address"):
        return "missing-address"
    if not row.get("disassembly"):
        return "missing-disassembly-context"
    if not row["active"]:
        return "captured-disabled-upstream"
    return "captured-manual-review-required"


def default_enablement_allowed(row):
    if not row["active"] or row.get("vr_source_blocked", False):
        return False
    return bool(row.get("manual_validation_evidence") and row.get("runtime_validation_evidence"))


def write_report(path, code_image, image_base, rows, missing_source_tokens, guard_policy_failures, manifest_path):
    path.parent.mkdir(parents=True, exist_ok=True)

    active = [row for row in rows if row["active"]]
    inactive = [row for row in rows if not row["active"]]
    vr_default_active = [row for row in rows if row["vr_default_active"]]
    vr_source_blocked = [row for row in rows if row.get("vr_source_blocked", False)]
    vr_resolution_gated = [row for row in rows if row.get("vr_resolution_flag")]
    missing = [row for row in rows if row["bytes"] is None]
    call_mismatch = [row for row in active if row["expect"] == "call" and row["classification"] != "candidate call"]
    call_mismatch_nearby = [row for row in call_mismatch if row.get("nearest_call_delta") is not None]
    vr_call_mismatch = [
        row
        for row in vr_default_active
        if row["expect"] == "call" and row["classification"] != "candidate call"
    ]
    semantic_manual_review = [
        row
        for row in active
        if "manual-review-required" in semantic_validation_status(row)
        or semantic_validation_status(row).startswith("blocked-")
    ]
    default_enablement_allowed_rows = [row for row in rows if default_enablement_allowed(row)]
    disassembly_context_rows = [row for row in rows if row.get("disassembly")]
    active_with_disassembly_context = [row for row in active if row.get("disassembly")]
    active_missing_disassembly_context = [row for row in active if not row.get("disassembly")]
    safety_failures = collect_safety_failures(code_image, rows, missing_source_tokens, guard_policy_failures)

    with path.open("w", encoding="utf-8") as handle:
        handle.write("# SkyrimTogetherVR Inline Patch Audit\n\n")
        handle.write("Generated by `Tools/SkyrimVR/audit_inline_patches.py`.\n\n")
        handle.write(f"- Machine-readable manifest: `{manifest_path}`\n")
        handle.write(f"- Source executable: `{code_image.source_path}`\n")
        handle.write(f"- Analysis image: {code_image.description}\n")
        handle.write(f"- Steam CEG detected: {'yes' if code_image.ceg_detected else 'no'}\n")
        handle.write(f"- Decrypted for byte/disassembly analysis: {'yes' if code_image.decrypted else 'no'}\n")
        handle.write(f"- Validation status: {code_image.validation_status}\n")
        handle.write(f"- Validation confidence: {code_image.validation_confidence}\n")
        handle.write(f"- Image base: `{fmt_hex(image_base)}`\n")
        handle.write(f"- Patch sites tracked: {len(rows)}\n")
        handle.write(f"- Active upstream patch sites: {len(active)}\n")
        handle.write(f"- Active in default VR target: {len(vr_default_active)}\n")
        handle.write(f"- VR resolution-gated patch sites: {len(vr_resolution_gated)}\n")
        handle.write(f"- Source-blocked for VR: {len(vr_source_blocked)}\n")
        handle.write(f"- Disabled/commented patch sites: {len(inactive)}\n")
        handle.write(f"- Missing byte reads: {len(missing)}\n")
        handle.write(f"- Missing source addend tokens: {len(missing_source_tokens)}\n")
        handle.write(f"- VR guard policy failures: {len(guard_policy_failures)}\n")
        handle.write(f"- Active upstream call-site opcode mismatches: {len(call_mismatch)}\n")
        handle.write(f"- Active upstream call-site mismatches with nearby call opcode hints: {len(call_mismatch_nearby)}\n")
        handle.write(f"- Default VR call-site opcode mismatches: {len(vr_call_mismatch)}\n\n")
        handle.write(f"- Active sites still requiring manual semantic validation: {len(semantic_manual_review)}\n")
        handle.write(f"- Default-enableable inline patch sites: {len(default_enablement_allowed_rows)}\n\n")
        handle.write(f"- Patch sites with disassembly context: {len(disassembly_context_rows)}\n")
        handle.write(f"- Active sites with disassembly context: {len(active_with_disassembly_context)}\n")
        handle.write(f"- Active sites missing disassembly context: {len(active_missing_disassembly_context)}\n\n")
        handle.write(f"- Audit safety failures: {len(safety_failures)}\n\n")
        if code_image.notes:
            handle.write("Notes:\n\n")
            for note in code_image.notes:
                handle.write(f"- {note}\n")
            handle.write("\n")
        handle.write(
            "This report resolves SkyrimTogether's byte-level inline patch sites against Skyrim VR addresses and "
            "captures bytes from the selected analysis image. It is not a proof that an intra-function patch is safe. "
            "Each active site still needs manual VR disassembly validation before "
            "`TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES` is enabled. Active source sites also require their "
            "per-site `TP_SKYRIM_VR_INLINE_PATCH_*` flag and a dedicated `*_VR_RESOLVED` gate to be set. The default VR target compiles with "
            "`TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0` and all per-site flags default to `0`, so these "
            "flat-SE byte patches remain disabled even if the broader gameplay hook batch is later enabled for testing. "
            "The `*_VR_RESOLVED` gates also default to `0`, so manually setting a site flag is not enough to compile "
            "an unresolved byte patch into a VR build.\n\n"
        )
        handle.write(
            "Sites marked source-blocked are stronger than resolution-gated sites: their C++ source uses "
            "`TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH`, which compiles out the patch for every VR target "
            "regardless of the broad, site, or `*_VR_RESOLVED` flags. Re-enabling one requires validating a new VR "
            "instruction boundary and then deliberately replacing that source guard with the normal resolved guard.\n\n"
        )
        handle.write(
            "For sites with a separate source addend and VR addend, the source addend is the SE-era upstream offset "
            "and the VR addend is the candidate offset currently encoded for the VR target. A `candidate call` "
            "static check only proves that the audited VR byte is a `call` opcode and records its target; it does "
            "not prove the hook signature or surrounding function semantics are safe.\n\n"
        )

        handle.write("## Summary Table\n\n")
        handle.write("| Site | Upstream Active | VR Default Active | Default Enable Allowed | Semantic Status | VR Source Guard | VR Site Flag | VR Resolution Gate | Source | ID | Source Addend | VR Addend | VR Address | Bytes | Static Check | Call Target |\n")
        handle.write("|---|---:|---:|---:|---|---|---|---|---|---:|---:|---:|---:|---|---|---:|\n")
        for row in rows:
            address_id = "" if "id" not in row else str(row["id"])
            source_addend = "" if "addend" not in row else fmt_hex(row.get("source_addend", row["addend"]))
            addend = "" if "addend" not in row else fmt_hex(row["addend"])
            byte_text = "" if row["bytes"] is None else bytes_hex(row["bytes"][: row["size"]])
            vr_flag = row.get("vr_flag", "")
            vr_flag_text = f"`{vr_flag}`" if vr_flag else ""
            vr_resolution_flag = row.get("vr_resolution_flag", "")
            vr_resolution_flag_text = f"`{vr_resolution_flag}`" if vr_resolution_flag else ""
            guard = vr_source_guard(row)
            guard_text = f"`{guard}`" if guard else ""
            handle.write(
                f"| {row['name']} | {'yes' if row['active'] else 'no'} | {'yes' if row['vr_default_active'] else 'no'} | {'yes' if default_enablement_allowed(row) else 'no'} | {semantic_validation_status(row)} | {guard_text} | {vr_flag_text} | {vr_resolution_flag_text} | `{row['source']}` | {address_id} | {source_addend} | {addend} | `{fmt_hex(row['address'])}` | `{byte_text}` | {row['classification']} | `{fmt_hex(row.get('call_target'))}` |\n"
            )

        handle.write("\n## Details\n\n")
        for row in rows:
            handle.write(f"### {row['name']}\n\n")
            handle.write(f"- Source: `{row['source']}`\n")
            handle.write(f"- Active in upstream source: {'yes' if row['active'] else 'no'}\n")
            handle.write(f"- Active in default VR target: {'yes' if row['vr_default_active'] else 'no'}\n")
            handle.write(f"- Default enable allowed: {'yes' if default_enablement_allowed(row) else 'no'}\n")
            handle.write(f"- Semantic validation status: {semantic_validation_status(row)}\n")
            handle.write(f"- Disassembly review status: {disassembly_review_status(row)}\n")
            handle.write(f"- Disassembly context lines: {disassembly_line_count(row)}\n")
            handle.write(f"- Source-blocked for VR: {'yes' if row.get('vr_source_blocked', False) else 'no'}\n")
            guard = vr_source_guard(row)
            if guard:
                handle.write(f"- VR source guard: `{guard}`\n")
            if row.get("vr_flag"):
                handle.write(f"- VR validation flag: `{row['vr_flag']}`\n")
            if row.get("vr_resolution_flag"):
                handle.write(f"- VR resolution gate: `{row['vr_resolution_flag']}`\n")
            if "id" in row:
                handle.write(f"- Address ID: `{row['id']}`\n")
                handle.write(f"- Resolved base RVA: `{fmt_hex(row['resolved'])}`\n")
                handle.write(f"- Source addend: `{fmt_hex(row.get('source_addend', row['addend']))}`\n")
                handle.write(f"- Audited VR addend: `{fmt_hex(row['addend'])}`\n")
            else:
                handle.write(f"- Absolute source address: `{fmt_hex(row['absolute'])}`\n")
            handle.write(f"- VR address: `{fmt_hex(row['address'])}`\n")
            handle.write(f"- File offset: `{fmt_hex(row['file_offset'])}`\n")
            handle.write(f"- Operation: `{row['operation']}`\n")
            handle.write(f"- Static check: {row['classification']}\n")
            if row["expect"] == "call":
                handle.write(f"- Nearby call scan: {fmt_call_hint(row)}\n")
                if row.get("call_target") is not None:
                    handle.write(f"- Call target: `{fmt_hex(row['call_target'])}`\n")
            handle.write(f"- Notes: {row['notes']}\n")
            if row["bytes"] is not None:
                handle.write(f"- Bytes at patch span: `{bytes_hex(row['bytes'][: row['size']])}`\n")
                handle.write(f"- Context bytes: `{bytes_hex(row['bytes'])}`\n")
            if row["disassembly"]:
                handle.write("\n```asm\n")
                handle.write(row["disassembly"])
                handle.write("\n```\n")
            handle.write("\n")

        handle.write("## Audit Safety Failures\n\n")
        if safety_failures:
            for failure in safety_failures:
                handle.write(f"- {failure}\n")
        else:
            handle.write("None.\n")
        handle.write("\n")

        handle.write("## VR Guard Policy Failures\n\n")
        if guard_policy_failures:
            for failure in guard_policy_failures:
                handle.write(f"- {failure}\n")
        else:
            handle.write("None.\n")
        handle.write("\n")

        handle.write("## Missing Source Addend Tokens\n\n")
        if missing_source_tokens:
            for rel_path, token in missing_source_tokens:
                handle.write(f"- `{rel_path}` missing `{token}`\n")
        else:
            handle.write("None.\n")


def row_vr_decision(row):
    if not row["active"]:
        return "disabled-upstream"
    if row["vr_default_active"]:
        return "enabled-in-default-vr"
    if row.get("vr_source_blocked", False):
        return "blocked-no-validated-vr-call-boundary"
    if row.get("vr_resolution_flag"):
        if row["classification"] == "candidate call":
            return "candidate-static-bytes-semantic-review-required"
        return "gated-static-bytes-captured-manual-review-required"
    return "missing-vr-resolution-gate"


def write_manifest(path, code_image, image_base, rows, missing_source_tokens, guard_policy_failures):
    path.parent.mkdir(parents=True, exist_ok=True)

    safety_failures = collect_safety_failures(code_image, rows, missing_source_tokens, guard_policy_failures)
    active = [row for row in rows if row["active"]]
    vr_default_active = [row for row in rows if row["vr_default_active"]]
    vr_source_blocked = [row for row in rows if row.get("vr_source_blocked", False)]
    vr_resolution_gated = [row for row in rows if row.get("vr_resolution_flag")]
    semantic_manual_review = [
        row
        for row in active
        if "manual-review-required" in semantic_validation_status(row)
        or semantic_validation_status(row).startswith("blocked-")
    ]
    default_enablement_allowed_rows = [row for row in rows if default_enablement_allowed(row)]
    disassembly_context_rows = [row for row in rows if row.get("disassembly")]
    active_with_disassembly_context = [row for row in active if row.get("disassembly")]
    active_missing_disassembly_context = [row for row in active if not row.get("disassembly")]

    manifest = {
        "schema": "SkyrimTogetherVR.inlinePatchAudit.v1",
        "generatedBy": "Tools/SkyrimVR/audit_inline_patches.py",
        "sourceExecutable": str(code_image.source_path),
        "analysisImage": code_image.description,
        "steamCegDetected": code_image.ceg_detected,
        "decryptedForAnalysis": code_image.decrypted,
        "validationStatus": code_image.validation_status,
        "validationConfidence": code_image.validation_confidence,
        "imageBase": fmt_hex(image_base),
        "summary": {
            "trackedPatchSites": len(rows),
            "activeUpstreamPatchSites": len(active),
            "activeDefaultVrPatchSites": len(vr_default_active),
            "vrResolutionGatedPatchSites": len(vr_resolution_gated),
            "sourceBlockedForVr": len(vr_source_blocked),
            "semanticManualReviewRequired": len(semantic_manual_review),
            "defaultEnablementAllowed": len(default_enablement_allowed_rows),
            "disassemblyContextSites": len(disassembly_context_rows),
            "activeSitesWithDisassemblyContext": len(active_with_disassembly_context),
            "activeSitesMissingDisassemblyContext": len(active_missing_disassembly_context),
            "missingSourceAddendTokens": len(missing_source_tokens),
            "vrGuardPolicyFailures": len(guard_policy_failures),
            "auditSafetyFailures": len(safety_failures),
        },
        "notes": code_image.notes,
        "sites": [],
        "missingSourceTokens": [
            {"path": rel_path, "token": token}
            for rel_path, token in missing_source_tokens
        ],
        "guardPolicyFailures": guard_policy_failures,
        "safetyFailures": safety_failures,
    }

    for row in rows:
        site = {
            "name": row["name"],
            "source": row["source"],
            "activeUpstream": row["active"],
            "activeDefaultVr": row["vr_default_active"],
            "defaultEnablementAllowed": default_enablement_allowed(row),
            "semanticValidationStatus": semantic_validation_status(row),
            "disassemblyReviewStatus": disassembly_review_status(row),
            "disassemblyLineCount": disassembly_line_count(row),
            "disassemblyContext": row.get("disassembly", ""),
            "sourceBlockedForVr": row.get("vr_source_blocked", False),
            "vrDecision": row_vr_decision(row),
            "vrSourceGuard": vr_source_guard(row),
            "vrFlag": row.get("vr_flag", ""),
            "vrResolutionGate": row.get("vr_resolution_flag", ""),
            "addressId": row.get("id"),
            "resolvedBaseRva": fmt_hex(row.get("resolved")),
            "sourceAddend": fmt_hex(row.get("source_addend", row.get("addend"))),
            "vrAddend": fmt_hex(row.get("addend")),
            "vrAddress": fmt_hex(row.get("address")),
            "fileOffset": fmt_hex(row.get("file_offset")),
            "operation": row["operation"],
            "expectedShape": row["expect"],
            "staticCheck": row["classification"],
            "bytes": "" if row["bytes"] is None else bytes_hex(row["bytes"][: row["size"]]),
            "contextBytes": "" if row["bytes"] is None else bytes_hex(row["bytes"]),
            "callTarget": fmt_hex(row.get("call_target")),
            "nearestCallDelta": fmt_signed_hex(row.get("nearest_call_delta")) if row.get("nearest_call_delta") is not None else "",
            "manualValidationEvidence": row.get("manual_validation_evidence", ""),
            "runtimeValidationEvidence": row.get("runtime_validation_evidence", ""),
            "notes": row["notes"],
            "activationRequirements": [],
        }
        if row["active"] and row.get("vr_source_blocked", False):
            site["activationRequirements"].append("validated replacement VR instruction boundary")
            site["activationRequirements"].append(
                "replace TP_SKYRIM_ALLOW_SOURCE_BLOCKED_VR_INLINE_PATCH with TP_SKYRIM_ALLOW_VR_RESOLVED_INLINE_PATCH in source"
            )
        elif row["active"] and row.get("vr_flag"):
            site["activationRequirements"].append("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=1")
            site["activationRequirements"].append(f"{row['vr_flag']}=1")
            if row.get("vr_resolution_flag"):
                site["activationRequirements"].append(f"{row['vr_resolution_flag']}=1")
        if row["active"]:
            site["activationRequirements"].append("manual VR semantic disassembly review")
            site["activationRequirements"].append("runtime validation before default enablement")
        manifest["sites"].append(site)

    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument(
        "--exe",
        type=pathlib.Path,
        default=vr_paths.default_skyrim_vr_exe_path(),
        help=vr_paths.skyrim_vr_exe_help(),
    )
    parser.add_argument(
        "--release",
        type=pathlib.Path,
        default=pathlib.Path("../_refs/VRAddressLibraryRelease/SKSE/Plugins/version-1-4-15-0.csv"),
    )
    parser.add_argument(
        "--decrypted-exe",
        type=pathlib.Path,
        help="Use a supplied decrypted SkyrimVR executable image for byte reads and disassembly.",
    )
    parser.add_argument(
        "--no-ceg-decrypt",
        action="store_true",
        help="Inspect --exe directly even if a Steam CEG entry stub is detected.",
    )
    parser.add_argument("--report", type=pathlib.Path, default=pathlib.Path("Docs/SkyrimVR/inline-patch-audit.md"))
    parser.add_argument("--manifest", type=pathlib.Path, default=pathlib.Path("Docs/SkyrimVR/inline-patch-manifest.json"))
    args = parser.parse_args()

    repo = args.repo.resolve()
    release = (repo / args.release).resolve() if not args.release.is_absolute() else args.release
    exe = args.exe.resolve()
    decrypted_exe = args.decrypted_exe.resolve() if args.decrypted_exe else None
    code_image = prepare_code_image(exe, decrypted_exe, args.no_ceg_decrypt)
    image_base, rows = inspect_sites(code_image, repo, release)
    missing_source_tokens = audit_required_source_tokens(repo)
    guard_policy_failures = audit_vr_guard_policy(repo, rows)
    write_report(repo / args.report, code_image, image_base, rows, missing_source_tokens, guard_policy_failures, args.manifest)
    write_manifest(repo / args.manifest, code_image, image_base, rows, missing_source_tokens, guard_policy_failures)

    active = [row for row in rows if row["active"]]
    vr_default_active = [row for row in rows if row["vr_default_active"]]
    vr_source_blocked = [row for row in rows if row.get("vr_source_blocked", False)]
    vr_resolution_gated = [row for row in rows if row.get("vr_resolution_flag")]
    semantic_manual_review = [
        row
        for row in active
        if "manual-review-required" in semantic_validation_status(row)
        or semantic_validation_status(row).startswith("blocked-")
    ]
    default_enablement_allowed_rows = [row for row in rows if default_enablement_allowed(row)]
    call_mismatch = [row for row in active if row["expect"] == "call" and row["classification"] != "candidate call"]
    call_mismatch_nearby = [row for row in call_mismatch if row.get("nearest_call_delta") is not None]
    vr_call_mismatch = [
        row
        for row in vr_default_active
        if row["expect"] == "call" and row["classification"] != "candidate call"
    ]
    safety_failures = collect_safety_failures(code_image, rows, missing_source_tokens, guard_policy_failures)
    print(f"Steam CEG detected: {'yes' if code_image.ceg_detected else 'no'}")
    print(f"Decrypted for analysis: {'yes' if code_image.decrypted else 'no'}")
    print(f"Validation status: {code_image.validation_status}")
    print(f"Tracked inline patch sites: {len(rows)}")
    print(f"Missing source addend tokens: {len(missing_source_tokens)}")
    print(f"VR guard policy failures: {len(guard_policy_failures)}")
    print(f"Active upstream inline patch sites: {len(active)}")
    print(f"Active default VR inline patch sites: {len(vr_default_active)}")
    print(f"VR resolution-gated inline patch sites: {len(vr_resolution_gated)}")
    print(f"Source-blocked VR inline patch sites: {len(vr_source_blocked)}")
    print(f"Active sites still requiring manual semantic validation: {len(semantic_manual_review)}")
    print(f"Default-enableable inline patch sites: {len(default_enablement_allowed_rows)}")
    print(f"Active upstream call-site opcode mismatches: {len(call_mismatch)}")
    print(f"Active upstream call-site mismatches with nearby call opcode hints: {len(call_mismatch_nearby)}")
    print(f"Active default VR call-site opcode mismatches: {len(vr_call_mismatch)}")
    print(f"Audit safety failures: {len(safety_failures)}")
    if vr_source_blocked:
        print("Source-blocked VR inline patch sites:")
        for row in vr_source_blocked:
            print(f"- {row['name']}: {row.get('vr_resolution_flag', '')}")
    if guard_policy_failures:
        print("VR guard policy failures:")
        for failure in guard_policy_failures:
            print(f"- {failure}")
    for failure in safety_failures:
        print(f"ERROR: {failure}")
    exit_code = 1 if safety_failures else 0
    try:
        code_image.pe.close()
    except Exception:
        pass
    if code_image.temp_dir is not None:
        code_image.temp_dir.cleanup()
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
