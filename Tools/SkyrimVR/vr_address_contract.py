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

# Curated virtual-slot targets whose Skyrim VR ABI and executable RVA have
# been independently established. These do not have Address Library IDs.
VALIDATED_VR_VTABLE_SLOT_TARGETS = {
    "ActiveEffect::AdjustForPerks": {
        "vtable_rva": 0x16AE840,
        "slot": 0,
        "target_rva": 0x540CC0,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::ActiveEffect*, RE::Actor*, RE::MagicTarget*)",
        "ownership": "all arguments are borrowed for the virtual call; the ActiveEffect is owned by the engine",
        "evidence": "VTABLE_ActiveEffect RVA 0x16AE840 slot 0, target RVA 0x0540CC0, exact entry bytes, and AddTarget virtual dispatch at RVA 0x0558B7D",
    },
}

# Curated function targets whose Skyrim VR ABI and executable RVA have been
# independently established. These rows override generated translations.
VALIDATED_VR_ADDRESS_OVERRIDES = {
    19075: {
        "offset": 0x27A4C0,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48",
        "source": "sse_vr",
        "status": "exact_vr_disassembly",
        "name": "TESObjectCELL::GetCOCPlacementInfo",
        "signature": "void(RE::TESObjectCELL*, RE::NiPoint3*, RE::NiPoint3*, bool)",
        "ownership": "cell and output vectors are borrowed for the synchronous call",
        "evidence": "SteamStub-decrypted SkyrimVR.exe 1.4.15 SHA256 6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971: exact entry RVA 0x027A4C0/prologue and CenterOnCell RVA 0x06BC6C0 callsite 0x06BC806 with RCX=cell, RDX=position, R8=rotation, R9B=true; disproves generated 0x0294070 NiTMap allocator",
    },
    33741: {
        "offset": 0x557830,
        "prologue": "48 89 5c 24 20 57 48 83 ec 40 48 89 6c 24 50 48",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "MagicTarget::AddTargetData::CheckAddEffect",
        "signature": "bool(RE::MagicTarget::AddTargetData*, RE::ActiveEffectFactory::CheckTargetArgs&, float)",
        "ownership": "AddTargetData and CheckTargetArgs are borrowed and remain valid only for the synchronous AddTarget call",
        "evidence": "VR Address Library ID/RVA, exact entry bytes, CommonLib typed declaration, and the verified AddTarget callsite at RVA 0x0558817",
    },
    33742: {
        "offset": 0x5579C0,
        "prologue": "40 55 53 56 57 41 56 48 8b ec 48 81 ec 80 00 00",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "MagicTarget::AddTarget",
        "signature": "bool(RE::MagicTarget*, RE::MagicTarget::AddTargetData&)",
        "ownership": "both arguments are non-owning and remain valid only for the synchronous call",
        "evidence": "VR Address Library ID/RVA, exact entry bytes, CommonLib typed declaration, and VR callee argument access",
    },
    34512: {
        "offset": 0x557070,
        "prologue": "48 81 ec 88 00 00 00 48 c7 44 24 20 fe ff ff ff",
        "source": "sse_vr",
        "status": "exact_vr_disassembly",
        "name": "MagicTarget::DispelAllSpells",
        "signature": "void(RE::MagicTarget*, bool)",
        "ownership": "MagicTarget is borrowed for the synchronous effect-list traversal",
        "evidence": "SteamStub-decrypted SkyrimVR.exe 1.4.15 SHA256 6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971: exact entry RVA 0x0557070/prologue, VTABLE___DispelEffectFunctor RVA 0x16B1DF0, force flag at visitor+8, MagicTarget active-list virtual +0x38, traversal 0x5440A0, and visitor Accept RVA 0x559C70 conditionally calling ActiveEffect::Dispel RVA 0x541100; disproves generated 0x0579DF0",
    },
    36541: {
        "offset": 0x5F0E20,
        "prologue": "48 8b c4 44 89 48 20 48 89 50 10 55 56 57 41 54",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "Actor::SpeakSound",
        "signature": "float(RE::Actor*, const char*, uint32_t*, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t, bool, uint64_t, bool, bool, bool)",
        "ownership": "resource path and handle buffer are borrowed for the synchronous call",
        "evidence": "VR Address Library ID/RVA, exact entry bytes, four direct callsites with fourteen arguments, and float return consumed from XMM0",
    },
    36690: {
        "offset": 0x6025A0,
        "prologue": "48 83 ec 28 48 8b 81 f0 00 00 00 48 85 c0 74 16",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "Actor::HasPerk",
        "signature": "bool(const RE::Actor*, RE::BGSPerk*)",
        "ownership": "actor and perk are borrowed for the synchronous query",
        "evidence": "CommonLib ID 36690, VR Address Library RVA, exact entry bytes, and the typed CommonLib declaration",
    },
    37772: {
        "offset": 0x6385F0,
        "prologue": "40 56 57 41 56 48 83 ec 20 45 32 f6 48 8b fa 48",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "Actor::RemoveSpell",
    },
    38949: {
        "offset": 0x643F20,
        "prologue": "48 89 5c 24 10 56 48 83 ec 20 48 8b f1 48 8b da",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorMediator::PerformAction",
    },
    35545: {
        "offset": 0x5B4290,
        "prologue": "48 83 ec 28 48 89 0d 1d 75 a3 02 4c 89 05 1e 75",
        "source": "database",
        "status": "exact_vr_database",
        "name": "WinMain",
    },
    35560: {
        "offset": 0x5B9330,
        "prologue": "48 8b c4 89 50 10 55 56 57 41 54 41 55 41 56 41",
        "source": "database",
        "status": "exact_vr_database",
        "name": "Main::Draw",
    },
    53926: {
        "offset": 0x12765B0,
        "source": "commonlib_vtable",
        "status": "exact_vr_vtable",
        "name": "BSScript::Internal::VirtualMachine::Update",
    },
}

REQUIRED_VR_ADDRESS_ALIAS_ROWS = frozenset(
    (metadata["vr_id"], desktop_id)
    for desktop_id, metadata in VALIDATED_COMMONLIB_VR_ALIASES.items()
)
