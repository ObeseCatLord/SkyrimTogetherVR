#!/usr/bin/env python3
import pathlib
import re
import sys


AUDITED_FILES = (
    "Code/client/Games/Skyrim/TESObjectREFR.cpp",
    "Code/client/Games/Animation.cpp",
    "Code/client/Games/References.cpp",
    "Code/client/Games/Skyrim/Actor.cpp",
    "Code/client/Games/Skyrim/PlayerCharacter.cpp",
    "Code/client/Games/Skyrim/Combat/CombatController.cpp",
    "Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.cpp",
    "Code/client/Games/Skyrim/EquipManager.cpp",
    "Code/client/Games/Skyrim/Projectiles/Projectile.cpp",
    "Code/client/Games/Skyrim/Magic/ActorMagicCaster.cpp",
    "Code/client/Games/Skyrim/Magic/MagicTarget.cpp",
    "Code/client/Games/Skyrim/Magic/EffectItem.cpp",
    "Code/client/Games/Skyrim/Effects/InvisibilityEffect.cpp",
    "Code/client/Games/Skyrim/Forms/MagicItem.cpp",
    "Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp",
    "Code/client/Games/Skyrim/Sky/Sky.cpp",
    "Code/client/ModCompat/BehaviorVar.cpp",
    "Code/client/Systems/AnimationSystem.cpp",
    "Code/client/Services/Generic/DiscoveryService.cpp",
    "Code/client/Services/Generic/TransportService.cpp",
    "Code/client/Services/Generic/PlayerService.cpp",
    "Code/client/Services/Generic/DiscordService.cpp",
    "Code/client/Services/Generic/QuestService.cpp",
    "Code/client/Services/Generic/CharacterService.cpp",
    "Code/client/Services/Generic/ObjectService.cpp",
    "Code/client/Services/Generic/WeatherService.cpp",
    "Code/client/Services/Generic/MagicService.cpp",
    "Code/client/Services/Generic/CombatService.cpp",
    "Code/client/Services/Generic/ActorValueService.cpp",
    "Code/client/Services/Generic/OverlayService.cpp",
    "Code/client/Services/Debug/Views/AnimDebugView.cpp",
    "Code/client/Services/Debug/Views/CellView.cpp",
    "Code/client/Services/Debug/Views/CombatView.cpp",
    "Code/client/Services/Debug/Views/ContainerDebugView.cpp",
    "Code/client/Services/Debug/Views/EntitiesView.cpp",
    "Code/client/Services/Debug/Views/FormDebugView.cpp",
    "Code/client/Services/Debug/Views/PlayerDebugView.cpp",
    "Code/client/Services/Debug/Views/QuestDebugView.cpp",
    "Code/client/Services/Debug/Views/SkillView.cpp",
    "Code/client/Services/Debug/Views/WeatherView.cpp",
    "Code/client/Games/Misc/SubtitleManager.cpp",
    "Code/client/Games/SaveLoad.cpp",
    "Code/client/Games/Skyrim/SaveLoad.cpp",
    "Code/client/Games/Forms.cpp",
)

FORBIDDEN_PATTERNS = (
    re.compile(r"\bformID\b"),
    re.compile(r"\bformType\b"),
    re.compile(r"(?:->|\.)flags\b"),
    re.compile(r"(?:->|\.)fullName\b"),
    re.compile(r"(?:->|\.)listOfEffects\b"),
    re.compile(r"(?:->|\.)keywordForm\b"),
    re.compile(r"(?:->|\.)currentProcess\b"),
    re.compile(r"(?:->|\.)middleProcess\b"),
    re.compile(r"(?:->|\.)baseForm\b"),
    re.compile(r"(?:->|\.)extraData\b"),
    re.compile(r"(?:->|\.)actorState\b"),
    re.compile(r"(?:->|\.)actorValueOwner\b"),
    re.compile(r"(?:->|\.)magicTarget\b"),
    re.compile(r"(?:->|\.)lockLevel\b"),
)

FILE_FORBIDDEN_PATTERNS = {
    "Code/client/Games/Skyrim/TESObjectREFR.cpp": (
        re.compile(r"\bpExtraCount->count\b"),
        re.compile(r"\bpExtraCharge->fCharge\b"),
        re.compile(r"\bpExtraHealth->fHealth\b"),
        re.compile(r"\bpExtraEnchantment->(?:pEnchantment|usCharge|bRemoveOnUnequip)\b"),
        re.compile(r"\bpExtraPoison->(?:pPoison|uiCount)\b"),
        re.compile(r"\bpExtraSoul->cSoul\b"),
        re.compile(r"\bpExtraText(?:DisplayData)?->(?:DisplayName|pDisplayNameText|pOwnerQuest|iOwnerInstance|fTemperFactor|usCustomNameLength)\b"),
        re.compile(r"\bpEffectItem->(?:data|pEffectSetting|fRawCost)\b"),
    ),
    "Code/client/Games/Skyrim/Magic/EffectItem.cpp": (
        re.compile(r"(?:->|\.)pEffectSetting\b"),
        re.compile(r"(?:->|\.)fRawCost\b"),
        re.compile(r"(?:->|\.)data\b"),
    ),
    "Code/client/Games/Skyrim/Forms/MagicItem.cpp": (
        re.compile(r"\bpEffect->pEffectSetting\b"),
    ),
    "Code/client/Games/Skyrim/Magic/MagicTarget.cpp": (
        re.compile(r"\bpEffectItem->pEffectSetting\b"),
        re.compile(r"\bapThis->fMagnitude\b"),
        re.compile(r"\barData\.(?:pCaster|pSpell|pEffectItem|fMagnitude|bDualCast)\b"),
    ),
    "Code/client/Games/Skyrim/Magic/ActorMagicCaster.cpp": (
        re.compile(r"\bapThis->(?:pCasterActor|pCurrentSpell|hDesiredTarget|eCastingSource)\b"),
    ),
    "Code/client/Games/Skyrim/Effects/InvisibilityEffect.cpp": (
        re.compile(r"\bapThis->pTarget\b"),
    ),
    "Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp": (
        re.compile(r"\beffectItem\.data\b"),
        re.compile(r"\beffectItem\.fRawCost\b"),
        re.compile(r"\beffectItem\.pEffectSetting\b"),
    ),
    "Code/client/Games/Skyrim/Actor.cpp": (
        re.compile(r"\bpLeveledListA\b"),
        re.compile(r"\bpLeveledEntries->pForm\b"),
        re.compile(r"\bCast<TESLevItem>\b"),
        re.compile(r"\bpChanges->entries\b"),
        re.compile(r"\bcasters\s*\["),
        re.compile(r"\bpValueModEffect->actorValueIndex\b"),
    ),
    "Code/client/Games/Skyrim/EquipManager.cpp": (
        re.compile(r"\bapData->(?:count|pSlot|pEquipSlot|bQueueEquip|bForceEquip|bPlaySound|bApplyNow|pExtraDataList|pSlotToReplace)\b"),
    ),
    "Code/client/Games/SaveLoad.cpp": (
        re.compile(r"(?:->|\.)buffer\b"),
        re.compile(r"(?:->|\.)capacity\b"),
        re.compile(r"(?:->|\.)position\b"),
        re.compile(r"(?:->|\.)changeFlags\b"),
        re.compile(r"(?:->|\.)loadFlag\b"),
        re.compile(r"(?:->|\.)maybeMoreFlags\b"),
        re.compile(r"(?:->|\.)unk1C\b"),
    ),
    "Code/client/Games/Skyrim/SaveLoad.cpp": (
        re.compile(r"\bapData->flags\b"),
        re.compile(r"\bapData->saveName\b"),
    ),
    "Code/client/Games/Forms.cpp": (
        re.compile(r"\b(?:saveBuffer|loadBuffer)\.(?:buffer|capacity|position|changeFlags|formId|form|loadFlag|maybeMoreFlags|unk1C)\b"),
    ),
    "Code/client/Services/Generic/MagicService.cpp": (
        re.compile(r"\bacEvent\.pCaster->pCasterActor\b"),
        re.compile(r"\bpActor->casters\b"),
        re.compile(r"\bdata\.(?:pCaster|pSpell|pEffectItem|fMagnitude|fUnkFloat1|eCastingSource|bDualCast)\b"),
        re.compile(r"\bpSpell(?:Item)?->eCastingType\b"),
    ),
    "Code/client/Services/Generic/ObjectService.cpp": (
        re.compile(r"\bpLoadedCellData->encounterZone\b"),
    ),
    "Code/client/Games/Skyrim/Forms/TESObjectCELL.cpp": (
        re.compile(r"\bpReferenceData->(?:refArray|capacity)\b"),
    ),
    "Code/client/Games/Skyrim/Projectiles/Projectile.cpp": (
        re.compile(r"\bpSpell->eCastingType\b"),
        re.compile(r"\barData\.(?:origin|projectileBase|shooter|weaponSource|ammoSource|angleZ|angleX|parentCell|spell|castingSource|area|power|scale|alwaysHit|noDamageOutsideCombat|autoAim|chainShatter|deferInitialization|forceConeOfFire)\b"),
    ),
    "Code/client/Services/Generic/CombatService.cpp": (
        re.compile(r"\blaunchData\.(?:origin|projectileBase|shooter|weaponSource|ammoSource|angleZ|angleX|parentCell|spell|castingSource|area|power|scale|alwaysHit|noDamageOutsideCombat|autoAim|chainShatter|useOrigin|deferInitialization|forceConeOfFire)\b"),
    ),
    "Code/client/Services/Debug/Views/PlayerDebugView.cpp": (
        re.compile(r"->pCurrentSpell\b"),
    ),
    "Code/client/Services/Generic/OverlayService.cpp": (
        re.compile(r"\bapActor->healthModifiers\b"),
    ),
    "Code/client/Services/Debug/Views/FormDebugView.cpp": (
        re.compile(r"\bpEffect->(?:pSpell|fElapsedSeconds|fDuration|fMagnitude|uiFlags)\b"),
    ),
    "Code/client/Services/Debug/Views/SkillView.cpp": (
        re.compile(r"\bpSkills->(?:xp|levelThreshold|skills|legendaryLevels)\b"),
    ),
}

ALLOWED_LINES = (
    "if (arData.parentCell)",
    "Event.ParentCellID = arData.parentCell->GetFormIdData();",
    "params.flags = DiscordCreateFlags_NoRequireDiscord;",
    "return changeFlags.flags;",
)

NIAVOBJECT_HEADER = "Code/client/Games/Skyrim/NetImmerse/NiAVObject.h"

REQUIRED_NIAVOBJECT_HEADER_TOKENS = (
    "These ABI placeholders preserve vtable shape only.",
    "Do not call them from",
    "virtual void PerformOp(PerformOpFunc& aFunc);",
    "virtual void AttachProperty(NiAlphaProperty* apProperty);",
    "virtual void SetMaterialNeedsUpdate(bool aNeedsUpdate);",
    "virtual void SetDefaultMaterialNeedsUpdateFlag(bool aFlag);",
    "virtual NiAVObject* GetObjectByName(const BSFixedString& acName);",
    "GetObjectByName is the only NiAVObject add-on virtual currently used",
)

UNVALIDATED_NIAVOBJECT_VIRTUAL_CALL_PATTERNS = (
    re.compile(r"(?:->|\.)Unk_VRFunc\s*\("),
    re.compile(r"(?:->|\.)PerformOp\s*\("),
    re.compile(r"(?:->|\.)AttachProperty\s*\("),
    re.compile(r"(?:->|\.)SetMaterialNeedsUpdate\s*\("),
    re.compile(r"(?:->|\.)SetDefaultMaterialNeedsUpdateFlag\s*\("),
)

NIAVOBJECT_VIRTUAL_CALL_SCAN_SUFFIXES = (".cpp", ".h", ".hpp")

NITOBJECTARRAY_HEADER = "Code/client/Games/Skyrim/NetImmerse/NiTObjectArray.h"

REQUIRED_NITOBJECTARRAY_HEADER_TOKENS = (
    "T* _data;",
    "uint16_t _capacity;",
    "uint16_t _freeIdx;",
    "uint16_t _size;",
    "uint16_t _growthSize;",
    "Iterator end() { return Iterator(_data + _capacity); }",
    "inline bool Empty() const noexcept { return _capacity == 0; }",
    "static_assert(offsetof(NiTObjectArray<uintptr_t>, _data) == 0x8);",
    "static_assert(offsetof(NiTObjectArray<uintptr_t>, _capacity) == 0x10);",
    "static_assert(offsetof(NiTObjectArray<uintptr_t>, _freeIdx) == 0x12);",
    "static_assert(offsetof(NiTObjectArray<uintptr_t>, _size) == 0x14);",
    "static_assert(offsetof(NiTObjectArray<uintptr_t>, _growthSize) == 0x16);",
)

FORBIDDEN_NITOBJECTARRAY_HEADER_TOKENS = (
    "T* data;",
    "uint16_t length;",
    "uint16_t unk1;",
    "uint16_t size;",
    "uint16_t capacity;",
)

NINODE_HEADER = "Code/client/Games/Skyrim/NetImmerse/NiNode.h"

REQUIRED_NINODE_HEADER_TOKENS = (
    "NiTObjectArray<NiPointer<NiAVObject>>& GetChildrenData()",
    "const NiTObjectArray<NiPointer<NiAVObject>>& GetChildrenData() const",
    "NiTObjectArray<NiPointer<NiAVObject>> children;",
    "static_assert(offsetof(NiNode, children) == 0x138);",
    "static_assert(sizeof(NiNode) == 0x150);",
    "static_assert(offsetof(NiNode, children) == 0x110);",
    "static_assert(sizeof(NiNode) == 0x128);",
)

NINODE_CHILD_ACCESS_PATTERN = re.compile(r"(?:->|\.)children\b")

TESCAMERA_HEADER = "Code/client/Games/Skyrim/Camera/TESCamera.h"
PLAYERCAMERA_HEADER = "Code/client/Games/Skyrim/Camera/PlayerCamera.h"
NICAMERA_HEADER = "Code/client/Games/Skyrim/NetImmerse/NiCamera.h"
NICAMERA_CPP = "Code/client/Games/Skyrim/NetImmerse/NiCamera.cpp"
HUDMENUUTILS_CPP = "Code/client/Games/Skyrim/Interface/Menus/HUDMenuUtils.cpp"

REQUIRED_TESCAMERA_HEADER_TOKENS = (
    "using CommonLibCameraOffsets = Skyrim::RuntimeLayout::TESCameraCommonLibNgOffsets;",
    "virtual void SetCameraRoot(NiPointer<NiNode> acRoot)",
    "NiPointer<NiNode> cameraRoot;",
    "TESCameraState* currentState;",
    "bool enabled;",
    "NiNode* GetCameraRoot()",
    "static_assert(offsetof(TESCamera, cameraRoot) == TESCamera::CommonLibCameraOffsets::CameraRoot);",
    "static_assert(sizeof(TESCamera) == TESCamera::CommonLibCameraOffsets::Size);",
)

REQUIRED_PLAYERCAMERA_HEADER_TOKENS = (
    "using CommonLibPlayerCameraOffsets = Skyrim::RuntimeLayout::PlayerCameraCommonLibNgOffsets;",
    "Keep it opaque so callers cannot accidentally use",
    "std::uint8_t playerCameraRuntimeData[CommonLibPlayerCameraOffsets::Size - CommonLibPlayerCameraOffsets::BaseSize];",
    "static_assert(sizeof(TESCamera) == PlayerCamera::CommonLibPlayerCameraOffsets::BaseSize);",
    "static_assert(sizeof(PlayerCamera) == PlayerCamera::CommonLibPlayerCameraOffsets::Size);",
)

FORBIDDEN_CAMERA_HEADER_TOKENS = (
    "NiNode* cameraNode;",
    "TESCameraState* state;",
    "float rotZ;",
    "float rotX;",
    "float zoom;",
    "bool unk;",
)

CAMERA_DIRECT_FIELD_ACCESS_PATTERN = re.compile(r"(?:->|\.)(?:cameraRoot|currentState|enabled|playerCameraRuntimeData)\b")

REQUIRED_NICAMERA_HEADER_TOKENS = (
    "using CommonLibCameraOffsets = Skyrim::RuntimeLayout::NiCameraCommonLibNgOffsets;",
    "struct NiFrustum",
    "static_assert(sizeof(NiFrustum) == 0x1C);",
    "struct RuntimeData2",
    "float worldToCam[4][4];",
    "NiFrustum* viewFrustumPtr;",
    "std::uint8_t unknownArrays[0x48];",
    "RuntimeData2 runtimeData2;",
    "GetWorldToCameraMatrixData()",
    "GetPortData()",
    "static_assert(offsetof(NiCamera, worldToCam) == NiCamera::CommonLibCameraOffsets::VRRuntimeData + NiCamera::CommonLibCameraOffsets::WorldToCam);",
    "static_assert(offsetof(NiCamera, runtimeData2) == NiCamera::CommonLibCameraOffsets::VRRuntimeData2);",
    "static_assert(sizeof(NiCamera) == NiCamera::CommonLibCameraOffsets::VRSize);",
    "static_assert(offsetof(NiCamera, worldToCam) == NiCamera::CommonLibCameraOffsets::SERuntimeData + NiCamera::CommonLibCameraOffsets::WorldToCam);",
    "static_assert(offsetof(NiCamera, runtimeData2) == NiCamera::CommonLibCameraOffsets::SERuntimeData2);",
    "static_assert(sizeof(NiCamera) == NiCamera::CommonLibCameraOffsets::SESize);",
)

REQUIRED_NICAMERA_CPP_TOKENS = (
    "using TWorldPtToScreenPt3 = bool(const float (*)[4], const NiRect<float>&, const NiPoint3&, float&, float&, float&, float);",
    "return WorldPtToScreenPt3(GetWorldToCameraMatrixData(), GetPortData(), in, out.x, out.y, out.z, tolerance);",
    "if (!s_WorldPtToScreenPt3 || !acMatrix)",
    "return s_WorldPtToScreenPt3(acMatrix, acPort, acPoint, aXOut, aYOut, aZOut, aZeroTolerance);",
    "POINTER_SKYRIMSE(TWorldPtToScreenPt3, s_w2s, 70640);",
)

REQUIRED_HUDMENUUTILS_TOKENS = (
    "if (!CameraWorldToCam || !CameraPort)",
    "return NiCamera::WorldPtToScreenPt3(*CameraWorldToCam, *CameraPort, aWorldPt, aScreenPt.x, aScreenPt.y, aScreenPt.z, 1e-5f);",
)

FORBIDDEN_NICAMERA_HEADER_TOKENS = (
    "NiNode* parent;",
    "NiAVObject* unk;",
    "float* matrix",
    "const NiRect<float>* port",
    "const NiPoint3* p_in",
    "float* x_out",
)

FORBIDDEN_NICAMERA_CPP_TOKENS = (
    "POINTER_SKYRIMSE(TW2S",
    "70639",
    "&out.y, &out.y",
    "float* matrix",
    "const NiRect<float>* port",
    "const NiPoint3* p_in",
    "float* x_out",
)

FORBIDDEN_HUDMENUUTILS_TOKENS = (
    "reinterpret_cast<float*>(CameraWorldToCam)",
    "CameraPort, &aWorldPt",
)


def repo_root():
    return pathlib.Path(__file__).resolve().parents[2]


def strip_cpp_comment(line, in_block_comment):
    result = []
    index = 0
    while index < len(line):
        if in_block_comment:
            end = line.find("*/", index)
            if end == -1:
                return "", True
            index = end + 2
            in_block_comment = False
            continue

        line_comment = line.find("//", index)
        block_comment = line.find("/*", index)
        if line_comment != -1 and (block_comment == -1 or line_comment < block_comment):
            result.append(line[index:line_comment])
            return "".join(result), False

        if block_comment != -1:
            result.append(line[index:block_comment])
            index = block_comment + 2
            in_block_comment = True
            continue

        result.append(line[index:])
        return "".join(result), False

    return "".join(result), in_block_comment


def strip_cpp_string_literals(line):
    result = []
    index = 0
    in_string = False
    in_char = False
    escaped = False

    while index < len(line):
        char = line[index]

        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            result.append(" ")
            index += 1
            continue

        if in_char:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == "'":
                in_char = False
            result.append(" ")
            index += 1
            continue

        if char == '"':
            in_string = True
            result.append(" ")
        elif char == "'":
            in_char = True
            result.append(" ")
        else:
            result.append(char)

        index += 1

    return "".join(result)


def audit_niavobject_virtual_boundary(root):
    failures = []

    header = root / NIAVOBJECT_HEADER
    header_text = header.read_text(encoding="utf-8", errors="replace") if header.exists() else ""
    for token in REQUIRED_NIAVOBJECT_HEADER_TOKENS:
        if token not in header_text:
            failures.append(f"{NIAVOBJECT_HEADER}: missing boundary token `{token}`")

    code_root = root / "Code" / "client"
    for path in code_root.rglob("*"):
        if path.relative_to(root).as_posix() == NIAVOBJECT_HEADER:
            continue
        if path.suffix not in NIAVOBJECT_VIRTUAL_CALL_SCAN_SUFFIXES:
            continue

        in_block_comment = False
        relative_file = path.relative_to(root).as_posix()
        for line_number, raw_line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code, in_block_comment = strip_cpp_comment(raw_line, in_block_comment)
            line = strip_cpp_string_literals(code).strip()
            if not line:
                continue

            for pattern in UNVALIDATED_NIAVOBJECT_VIRTUAL_CALL_PATTERNS:
                if pattern.search(line):
                    failures.append(f"{relative_file}:{line_number}: unvalidated NiAVObject virtual call: {line}")
                    break

    return failures


def audit_nitobjectarray_layout(root):
    failures = []

    header = root / NITOBJECTARRAY_HEADER
    header_text = header.read_text(encoding="utf-8", errors="replace") if header.exists() else ""
    for token in REQUIRED_NITOBJECTARRAY_HEADER_TOKENS:
        if token not in header_text:
            failures.append(f"{NITOBJECTARRAY_HEADER}: missing layout token `{token}`")
    for token in FORBIDDEN_NITOBJECTARRAY_HEADER_TOKENS:
        if token in header_text:
            failures.append(f"{NITOBJECTARRAY_HEADER}: stale field token `{token}`")

    return failures


def audit_ninode_layout_boundary(root):
    failures = []

    header = root / NINODE_HEADER
    header_text = header.read_text(encoding="utf-8", errors="replace") if header.exists() else ""
    for token in REQUIRED_NINODE_HEADER_TOKENS:
        if token not in header_text:
            failures.append(f"{NINODE_HEADER}: missing layout token `{token}`")

    code_root = root / "Code" / "client"
    for path in code_root.rglob("*"):
        if path.relative_to(root).as_posix() == NINODE_HEADER:
            continue
        if path.suffix not in NIAVOBJECT_VIRTUAL_CALL_SCAN_SUFFIXES:
            continue

        in_block_comment = False
        relative_file = path.relative_to(root).as_posix()
        for line_number, raw_line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code, in_block_comment = strip_cpp_comment(raw_line, in_block_comment)
            line = strip_cpp_string_literals(code).strip()
            if not line:
                continue

            if NINODE_CHILD_ACCESS_PATTERN.search(line):
                failures.append(f"{relative_file}:{line_number}: direct NiNode children access: {line}")

    return failures


def audit_camera_layout_boundary(root):
    failures = []

    tes_camera_header = root / TESCAMERA_HEADER
    tes_camera_text = tes_camera_header.read_text(encoding="utf-8", errors="replace") if tes_camera_header.exists() else ""
    for token in REQUIRED_TESCAMERA_HEADER_TOKENS:
        if token not in tes_camera_text:
            failures.append(f"{TESCAMERA_HEADER}: missing layout token `{token}`")
    for token in FORBIDDEN_CAMERA_HEADER_TOKENS:
        if token in tes_camera_text:
            failures.append(f"{TESCAMERA_HEADER}: stale camera field token `{token}`")

    player_camera_header = root / PLAYERCAMERA_HEADER
    player_camera_text = player_camera_header.read_text(encoding="utf-8", errors="replace") if player_camera_header.exists() else ""
    for token in REQUIRED_PLAYERCAMERA_HEADER_TOKENS:
        if token not in player_camera_text:
            failures.append(f"{PLAYERCAMERA_HEADER}: missing layout token `{token}`")
    for token in FORBIDDEN_CAMERA_HEADER_TOKENS:
        if token in player_camera_text:
            failures.append(f"{PLAYERCAMERA_HEADER}: stale camera field token `{token}`")

    code_root = root / "Code" / "client"
    allowed_files = {TESCAMERA_HEADER, PLAYERCAMERA_HEADER}
    for path in code_root.rglob("*"):
        relative_file = path.relative_to(root).as_posix()
        if relative_file in allowed_files:
            continue
        if path.suffix not in NIAVOBJECT_VIRTUAL_CALL_SCAN_SUFFIXES:
            continue

        in_block_comment = False
        for line_number, raw_line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code, in_block_comment = strip_cpp_comment(raw_line, in_block_comment)
            line = strip_cpp_string_literals(code).strip()
            if not line:
                continue

            if CAMERA_DIRECT_FIELD_ACCESS_PATTERN.search(line):
                failures.append(f"{relative_file}:{line_number}: direct camera layout access: {line}")

    return failures


def audit_nicamera_projection_boundary(root):
    failures = []

    ni_camera_header = root / NICAMERA_HEADER
    ni_camera_text = ni_camera_header.read_text(encoding="utf-8", errors="replace") if ni_camera_header.exists() else ""
    for token in REQUIRED_NICAMERA_HEADER_TOKENS:
        if token not in ni_camera_text:
            failures.append(f"{NICAMERA_HEADER}: missing layout token `{token}`")
    for token in FORBIDDEN_NICAMERA_HEADER_TOKENS:
        if token in ni_camera_text:
            failures.append(f"{NICAMERA_HEADER}: stale projection token `{token}`")

    ni_camera_cpp = root / NICAMERA_CPP
    ni_camera_cpp_text = ni_camera_cpp.read_text(encoding="utf-8", errors="replace") if ni_camera_cpp.exists() else ""
    for token in REQUIRED_NICAMERA_CPP_TOKENS:
        if token not in ni_camera_cpp_text:
            failures.append(f"{NICAMERA_CPP}: missing projection token `{token}`")
    for token in FORBIDDEN_NICAMERA_CPP_TOKENS:
        if token in ni_camera_cpp_text:
            failures.append(f"{NICAMERA_CPP}: stale projection token `{token}`")

    hud_utils = root / HUDMENUUTILS_CPP
    hud_utils_text = hud_utils.read_text(encoding="utf-8", errors="replace") if hud_utils.exists() else ""
    for token in REQUIRED_HUDMENUUTILS_TOKENS:
        if token not in hud_utils_text:
            failures.append(f"{HUDMENUUTILS_CPP}: missing projection token `{token}`")
    for token in FORBIDDEN_HUDMENUUTILS_TOKENS:
        if token in hud_utils_text:
            failures.append(f"{HUDMENUUTILS_CPP}: stale projection token `{token}`")

    return failures


def main():
    root = repo_root()
    failures = []
    audited_lines = 0

    for relative_file in AUDITED_FILES:
        path = root / relative_file
        in_block_comment = False
        for line_number, raw_line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code, in_block_comment = strip_cpp_comment(raw_line, in_block_comment)
            line = strip_cpp_string_literals(code).strip()
            if not line or line in ALLOWED_LINES:
                continue

            audited_lines += 1
            for pattern in FORBIDDEN_PATTERNS + FILE_FORBIDDEN_PATTERNS.get(relative_file, ()):
                if pattern.search(line):
                    failures.append(f"{relative_file}:{line_number}: {line}")
                    break

    niavobject_failures = audit_niavobject_virtual_boundary(root)
    failures.extend(niavobject_failures)
    nitobjectarray_failures = audit_nitobjectarray_layout(root)
    failures.extend(nitobjectarray_failures)
    ninode_failures = audit_ninode_layout_boundary(root)
    failures.extend(ninode_failures)
    camera_failures = audit_camera_layout_boundary(root)
    failures.extend(camera_failures)
    nicamera_failures = audit_nicamera_projection_boundary(root)
    failures.extend(nicamera_failures)

    print(f"Audited files: {len(AUDITED_FILES)}")
    print(f"Audited non-comment lines: {audited_lines}")
    print(f"NiAVObject virtual boundary failures: {len(niavobject_failures)}")
    print(f"NiTObjectArray layout failures: {len(nitobjectarray_failures)}")
    print(f"NiNode layout boundary failures: {len(ninode_failures)}")
    print(f"Camera layout boundary failures: {len(camera_failures)}")
    print(f"NiCamera projection boundary failures: {len(nicamera_failures)}")
    print(f"CommonLib layout bypasses: {len(failures)}")
    for failure in failures:
        print(f"- {failure}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
