#!/usr/bin/env python3

# CommonLib NG's RELOCATION_ID(se, ae) selects the first ID for Skyrim VR.
# These are exact singleton/global semantic matches. Keep them curated rather
# than importing every CommonLib function pair: several same-numbered function
# pairs in this project have different VR semantics and require separate proof.
VALIDATED_COMMONLIB_VR_ALIASES = {
    400269: {"vr_id": 514141, "offset": 0x1F82AD8, "name": "TESDataHandler::Singleton"},
    400320: {"vr_id": 514172, "offset": 0x1F831C8, "name": "BGSCreatedObjectManager::Singleton"},
    400327: {"vr_id": 514178, "offset": 0x1F83200, "name": "UI::Singleton"},
    400443: {"vr_id": 514283, "offset": 0x1F850E8, "name": "SubtitleManager::Singleton"},
    400447: {"vr_id": 514287, "offset": 0x1F85108, "name": "Calendar::Singleton"},
    400475: {"vr_id": 514315, "offset": 0x1F889D8, "name": "SkyrimVM::Singleton"},
    400636: {"vr_id": 514494, "offset": 0x2F896D8, "name": "ActorEquipManager::Singleton"},
    400802: {"vr_id": 514642, "offset": 0x2F8A888, "name": "PlayerCamera::Singleton"},
    400863: {"vr_id": 514705, "offset": 0x2F8AAA0, "name": "ControlMap::Singleton/local input-manager shim"},
    400864: {"vr_id": 514706, "offset": 0x2F8AAA8, "name": "PlayerControls::Singleton"},
    401263: {"vr_id": 515124, "offset": 0x2FC52E8, "name": "MenuControls::Singleton"},
    403330: {"vr_id": 516851, "offset": 0x2FEB200, "name": "BGSSaveLoadGame::Singleton"},
    404238: {"vr_id": 517711, "offset": 0x2FFFDEA, "name": "PlayerCharacter::GodMode"},
    411155: {"vr_id": 524557, "offset": 0x3175FE0, "name": "INISettingCollection::Singleton"},
    411347: {"vr_id": 524728, "offset": 0x317E790, "name": "RendererData::Singleton"},
    411348: {"vr_id": 524729, "offset": 0x317E798, "name": "RendererData::D3D11Device"},
}

REQUIRED_VR_ADDRESS_ALIAS_ROWS = frozenset(
    (metadata["vr_id"], desktop_id)
    for desktop_id, metadata in VALIDATED_COMMONLIB_VR_ALIASES.items()
)
