# Active Lifecycle Address Crash: Senior Review Disposition

| Senior recommendation | Disposition | Implementation |
|---|---|---|
| Select VR UI singleton ID 514178 directly and guard the returned pointer. | Adopted | `UI::Get()` now keeps desktop ID 400327 only for non-VR builds, checks relocation storage, and rejects/logs an unreadable VR object before menu calls. |
| Curate and require all 16 exact CommonLib singleton/global aliases. | Adopted | `audit_addresses.py` owns the verified desktop-to-VR map and expected release RVAs. Generation fails if an exact row or source RVA is absent. Built-package and smoke audits require the same rows. |
| Apply project address overlays after binary as well as CSV base databases. | Adopted | `VersionDb` now has one VR overlay path called after either base format. A binary-library regression test proves override and alias precedence plus reverse-map cleanup. |
| Generalize the readable-page helper and guard Discord's player access. | Adopted | `VRMemorySafety.h` validates complete ranges across committed readable regions. Player, body-pose, UI, and Discord paths use it. |
| Make ModManager access and accepted-mod handling null-safe. | Adopted | `ModManager::Get()` checks relocation storage and `ModSystem::HandleMods()` aborts with an error if the manager is unavailable. Existing discovery/authentication null checks remain. |
| Treat pair 514705/400863 as ControlMap rather than BSInputEnableManager. | Adopted | Curated metadata records `ControlMap::Singleton/local input-manager shim`; no unsupported ABI claim was added. |
| Add a default-path relocation manifest. | Adapted | Existing bring-up and service audits already cover the proven hook path. This change adds exact UI/address/load-order/guard assertions to the readiness and package audits instead of introducing a second manifest format. |
| Convert ActorMediator ID 403567 to CommonLib ID 517045. | Deferred | CommonLib pairs 517045 with desktop ID 403553, not the project's 403567. It is outside the connection-only path and requires PDB/disassembly proof before alteration. |
| Blanket-convert lower CommonLib function pairs. | Rejected | Runtime evidence shows numeric/function pair assumptions are not uniformly valid for this codebase. Functions remain individually validated. |

## Resulting Build Gate

Before the next Windows build, the address audit must regenerate all helper artifacts with the 16 curated aliases, source/readiness audits must pass, and the VersionDb tests must compile and pass. After deployment, the default connection-only target must reach lifecycle `ready`, status `online`, and a server admission log before any avatar/gameplay target is tested.
