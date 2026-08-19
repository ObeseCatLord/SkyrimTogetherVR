#!/usr/bin/env python3

# CommonLib NG's RELOCATION_ID(se, ae) selects the first ID for Skyrim VR.
# These are exact singleton/global semantic matches. Keep them curated rather
# than importing every CommonLib function pair: several same-numbered function
# pairs in this project have different VR semantics and require separate proof.
VR_ADDRESS_CONTRACT_RUNTIME = {
    "version": "1.4.15.0",
    "executable_sha256": "6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971",
}

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
    "PlayerCharacter::DropObject": {
        "vtable_rva": 0x16E2230,
        "slot": 0xCD,
        "target_rva": 0x6C00F0,
        "prologue": "40 55 56 57 41 54 41 55 41 56 41 57 48 81 ec a0 00 00 00",
        "source": "exact_vr_disassembly",
        "signature": "RE::ObjectRefHandle*(RE::PlayerCharacter*, RE::ObjectRefHandle*, RE::TESBoundObject*, RE::ExtraDataList*, int32_t, RE::NiPoint3*, RE::NiPoint3*)",
        "ownership": "all arguments and the result handle are engine-owned for the synchronous original call",
        "evidence": "PlayerCharacter vtable RVA 0x16E2230 slot 0xCD resolves to RVA 0x06C00F0; DropHooks validates the direct target and 19-byte entry fingerprint before detouring",
    },
}

# Exact bridge-owned targets intentionally resolved by RVA or the pinned direct
# VR Address Library ID.  They are not aliases and must not be promoted into
# VALIDATED_COMMONLIB_VR_ALIASES without independent semantic proof.
VALIDATED_VR_DIRECT_RVA_TARGETS = {
    "TESObjectREFR::ActivateRef": {
        "rva": 0x2A8300,
        "prologue": "48 8b c4 4c 89 48 20 44 88 40 18 55 56 57 48 8d 68 b1 48 81 ec c0 00 00 00 48 c7 45 17 fe ff ff",
        "source": "exact_vr_disassembly",
        "signature": "bool(RE::TESObjectREFR*, RE::TESObjectREFR*, uint8_t, RE::TESBoundObject*, int32_t, bool)",
        "evidence": "ActivationHooks uses the direct RVA and a 32-byte fingerprint; the curated ID 19796 override resolves to the same body",
    },
    "Calendar::Update": {
        "address_id": 35402,
        "rva": 0x5AD8F0,
        "prologue": "40 53 48 81 ec 80 00 00 00 48 8b 41 30 48 8b d9 0f 29 74 24 70 0f 28 f1",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::Calendar*, float)",
        "evidence": "CalendarHooks verifies direct VR ID 35402, RVA 0x05AD8F0, and a 24-byte fingerprint; the float is elapsed real seconds",
    },
    "ActorEquipManager::EquipObject": {
        "rva": 0x642E30,
        "prologue": "40 56 57 41 54 41 57 48 83 ec 38 48 3b 15 66 18 98 02",
        "source": "exact_vr_disassembly",
        "signature": "RE::AIProcess*(RE::ActorEquipManager*, RE::Actor*, void*, void*)",
    },
    "ActorEquipManager::UnequipObject": {
        "rva": 0x6436C0,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 54 41 56 41 57 48 83 ec 20 48 3b 15 c3 0f 98 02",
        "source": "exact_vr_disassembly",
        "signature": "RE::AIProcess*(RE::ActorEquipManager*, RE::Actor*, void*, void*)",
    },
    "ActorEquipManager::EquipSpell": {
        "rva": 0x642B80,
        "prologue": "40 56 57 41 54 41 57 48 83 ec 38 48 3b 15 16 1b 98 02",
        "source": "exact_vr_disassembly",
        "signature": "RE::AIProcess*(RE::ActorEquipManager*, RE::Actor*, void*, void*)",
    },
    "ActorEquipManager::UnequipSpell": {
        "rva": 0x643470,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 54 41 56 41 57 48 83 ec 20 48 3b 15 13 12 98 02",
        "source": "exact_vr_disassembly",
        "signature": "RE::AIProcess*(RE::ActorEquipManager*, RE::Actor*, void*, void*)",
    },
    "ActorEquipManager::EquipShout": {
        "rva": 0x6430E0,
        "prologue": "40 56 57 41 54 41 55 48 83 ec 38 48 3b 15 b6 15 98 02",
        "source": "exact_vr_disassembly",
        "signature": "RE::AIProcess*(RE::ActorEquipManager*, RE::Actor*, void*, void*)",
    },
    "ActorEquipManager::UnequipShout": {
        "rva": 0x643910,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 54 41 55 41 56 48 83 ec 20 48 3b 15 73 0d 98 02",
        "source": "exact_vr_disassembly",
        "signature": "RE::AIProcess*(RE::ActorEquipManager*, RE::Actor*, void*, void*)",
    },
    "ActorEquipManager::UnequipSpellSynchronously": {
        "rva": 0x641350,
        "prologue": "48 85 d2 74 56 48 89 5c 24 08 48 89 74 24 10 57",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::ActorEquipManager*, RE::Actor*, RE::SpellItem*, const RE::BGSEquipSlot*)",
    },
    "ActorEquipManager::UnequipShoutSynchronously": {
        "rva": 0x641430,
        "prologue": "48 83 ec 38 48 85 d2 74 1d 4d 85 c0 74 18 4c 8d",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::ActorEquipManager*, RE::Actor*, RE::TESShout*)",
    },
    "SummonCreatureEffect::Factory": {
        "rva": 0x569920,
        "prologue": "40 56 57 41 56 48 83 ec 30 48 c7 44 24 20 fe ff ff ff 48 89 5c 24 50 48 89 6c 24 58 49 8b f0 48 8b ea 4c 8b f1",
        "extent": 0xB1,
        "source": "exact_vr_disassembly",
        "signature": "RE::ActiveEffect*(RE::Actor*, RE::MagicItem*, RE::Effect*)",
        "evidence": "registration thunk RVA 0x00902B0 loads the factory at +0xE and archetype 0x12 at +0x21, with helper RVA 0x0556930",
    },
    "TESObjectREFR::SetTemporary": {
        "rva": 0x1A4A50,
        "prologue": "40 53 48 83 ec 20 33 d2 48 8b d9",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::TESObjectREFR*)",
        "evidence": "RemoteSaveExclusion validates the direct target before dispatch and verifies RecordFlags::kTemporary after the engine call",
    },
    "Sky::ForceWeather": {
        "rva": 0x3C48C0,
        "prologue": "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 41 0f b6 d8 48 8b f2 48 8b f9",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::Sky*, RE::TESWeather*, bool)",
    },
    "Sky::ReleaseWeatherOverride": {
        "rva": 0x3C4970,
        "prologue": "48 83 79 60 00 74 12 81 89 dc 01 00 00 00 00 20 00 48 c7 41 60 00 00 00 00",
        "source": "exact_vr_disassembly",
        "signature": "void(RE::Sky*)",
    },
}

# These numeric VR rows are known false semantic matches. VersionDb removes
# them after loading raw rows, project overrides, and generated aliases.
KNOWN_FALSE_VR_ADDRESS_IDS = {
    34989: {"rva": 0x598B30, "claimed_name": "SummonCreatureEffect factory"},
    37577: {"rva": 0x62C830, "claimed_name": "Actor::IsFleeing"},
    39643: {"rva": 0x6D96F0, "claimed_name": "Dialogue process response"},
    40454: {"rva": 0x709970, "claimed_name": "PlayerCharacter::DropObject"},
    42704: {"rva": 0x767A40, "claimed_name": "ActorKnowledge::UpdateDetectionState"},
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
    19796: {
        "offset": 0x2A8300,
        "prologue": "48 8b c4 4c 89 48 20 44 88 40 18 55 56 57 48 8d",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "TESObjectREFR::ActivateRef",
        "signature": "bool(RE::TESObjectREFR*, RE::TESObjectREFR*, uint8_t, RE::TESBoundObject*, int32_t, bool)",
        "ownership": "target, activator, and optional object are borrowed for the synchronous activation",
        "evidence": "CommonLib VR SE-side ID 19369 resolves to RVA 0x02A8300; exact entry bytes and six-argument recursive call shape disprove the raw VR numeric-ID row at 0x02B8310",
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
    34370: {
        "offset": 0x54F500,
        "prologue": "40 57 48 83 ec 50 48 8b f9 e8 a2 ea 01 00 48 8b",
        "source": "database",
        "status": "exact_vr_vtable_disassembly",
        "name": "InvisibilityEffect::Finish",
        "signature": "void(RE::InvisibilityEffect*)",
        "ownership": "the effect is engine-owned for the synchronous virtual call",
        "evidence": "InvisibilityEffect vtable RVA 0x16B0AE0 slot 0x15 resolves to RVA 0x054F500; exact entry bytes and base-Finish/target-actor behavior disprove raw ID 34370 at 0x0571420",
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
    38894: {
        "offset": 0x640A90,
        "prologue": "48 85 d2 0f 84 2f 01 00 00 57 48 83 ec 50 48 c7",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::EquipObject",
        "signature": "void(RE::ActorEquipManager*, RE::Actor*, RE::TESBoundObject*, RE::ExtraDataList*, uint32_t, RE::BGSEquipSlot*, bool, bool, bool, bool)",
        "ownership": "manager, actor, object, extra data, and slot are borrowed for synchronous equip",
        "evidence": "CommonLib VR ID 37938 and stock Papyrus/combat-AI callers converge on RVA 0x0640A90; raw desktop ID 38894 at 0x0688EC0 is unrelated",
    },
    38928: {
        "offset": 0x642B80,
        "prologue": "40 56 57 41 54 41 57 48 83 ec 38 48 3b 15 16 1b",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::EquipSpellInternal",
    },
    38929: {
        "offset": 0x642E30,
        "prologue": "40 56 57 41 54 41 57 48 83 ec 38 48 3b 15 66 18",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::EquipObjectInternal",
    },
    38930: {
        "offset": 0x6430E0,
        "prologue": "40 56 57 41 54 41 55 48 83 ec 38 48 3b 15 b6 15",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::EquipShoutInternal",
    },
    38933: {
        "offset": 0x643470,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::UnequipSpellInternal",
    },
    38934: {
        "offset": 0x6436C0,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::UnequipObjectInternal",
    },
    38935: {
        "offset": 0x643910,
        "prologue": "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48",
        "source": "database",
        "status": "exact_vr_disassembly",
        "name": "ActorEquipManager::UnequipShoutInternal",
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
