# SkyrimTogetherVR Code Review Handoff

Date: July 14, 2026

This handoff covers the Skyrim VR port of Tilted Online / Skyrim Together
Reborn. The port is VR-only and targets Skyrim VR 1.4.15 with SKSEVR, the VR
Address Library, VRIK, HIGGS, PLANCK, and optional SkyrimVR-FBT.

## Review Baseline

- Review source head: `15fd7280` plus this handoff metadata update
- Built client revision: `220bc4bca709bf1fa208984d1fd4b2b5b773210a`
- Branch: `main`
- Upstream comparison branch: `original-skyrim-together`
- Windows package: clean `releasedbg` default build from the built client revision
- Server: Linux ARM64 Docker image built from compatible revision `99d1e1d1`
- Protocol rule: the extended VR pose schema does not negotiate with an older
  Skyrim Together server. The client changes after `99d1e1d1` do not change
  `Code/server`, `Code/encoding`, or the VR wire schema.

## Port Approach

The port keeps the original client/server architecture and adds VR-specific
boundaries instead of translating every desktop offset at runtime:

- SKSEVR loading and a VR launcher path start the normal client lifecycle.
- VR Address Library IDs and audited overrides resolve Skyrim VR code sites.
- Semantic runtime-layout accessors isolate verified VR structure differences.
- VRIK, HIGGS, PLANCK, tick, and early-load bridge DLLs provide narrow process
  boundaries where the original flat client cannot own VR plugin callbacks.
- A file-backed companion interface replaces the desktop-only in-game overlay
  for connection and diagnostics.
- VR movement, pose, equipment, activation, magic, combat, projectile, grab,
  HIGGS, PLANCK, save/load, and player-cell data use explicit validated lanes.
- Startup and gameplay mutations remain fail-closed where VR ownership or an
  address has not been proven.
- A VM-owner lifecycle gate now keeps transport and gameplay updates suspended
  through the main menu, character creation, loading, and unstable player/cell
  transitions. See `startup-lifecycle-final-build-20260714.md`.

## Full-Body Tracking

The current revision adds a versioned `VRBodyPoseData` payload for pelvis and
leg state. HIGGS captures the final local player skeleton in its
post-VRIK/post-HIGGS callback, after SkyrimVR-FBT has run in the installed
plugin order. Capture is nonblocking, stale after 250 ms, and validated on
both client and server ingress.

Remote body data is relayed, cached, and exposed in runtime evidence. It is
not yet written to remote pelvis or leg bones. Applying those transforms
requires an actor-specific post-animation ownership point that does not race
VRIK or PLANCK. This is a deliberate safety boundary, not a claim of rendered
remote FBT. Head, hands, VRIK state, and existing avatar pose paths remain
independent when FBT is unavailable.

The supplied SkyrimVR-FBT source archive has no redistribution license. It is
included only in this private review package and must not be published. The
repository records only the interoperability profile and behavior needed by
the port.

## Verification Completed

- All 27 no-launch readiness gates passed, including address, inline-patch,
  game-file, FUS native DLL, HIGGS, PLANCK, FBT, and handoff audits.
- The Linux protocol tests passed all 136 assertions in 5 test cases.
- The Windows default package built from a clean checkout of the built client
  revision, including the launcher, four bridge DLLs, EarlyLoad, TPProcess,
  CEF payload, and eight regenerated Papyrus scripts.
- Package-only and strict post-install audits passed with SKSEVR, VR Address
  Library, VRIK, HIGGS, PLANCK, and FBT present.
- The gameplay package is installed in the local Skyrim VR directory.
- The compatible server image was built and deployed separately; see
  `final-build-deployment-20260714.md` for its container evidence and
  `startup-lifecycle-final-build-20260714.md` for the client compatibility
  statement.

No game was launched as part of these checks. Runtime acceptance remains the
user's VR test.

## Review Priorities

1. Validate the fixed-order VR pose wire change and the client/server wire
   compatibility statement.
2. Review `VRBodyPoseData` validation, sequence handling, mailbox lifetime,
   and the tick/HIGGS endpoint ABI version 3 boundary.
3. Verify the local skeleton capture ordering against the supplied FBT source,
   installed VRIK build, and HIGGS callback implementation.
4. Review the remaining remote-avatar ownership boundary before adding any
   pelvis or leg writes.
5. Recheck translated Address Library aliases and every enabled gameplay hook
   against Skyrim VR 1.4.15.
6. Confirm PLANCK remains authoritative for active ragdolls and HIGGS remains
   authoritative for local held-object physics.
7. Review VM-update callback ownership, lifecycle epoch invalidation, retained
   reconnect state, and TiltedConnect owner-thread enforcement.

## Runtime Acceptance

The first matched-client run should establish:

- client connection and assignment without bootstrap or first-tick crashes;
- lifecycle transition to `ready` only after a stable post-character player
  and cell, with one VM-update owner thread;
- VRIK/HIGGS/PLANCK bridges loaded with no endpoint fault;
- increasing local HMD/hand pose sequence;
- increasing FBT body capture attempts and successes when FBT is loaded;
- `local.body.valid=1` with an increasing body sequence;
- increasing remote body sequence with two matched clients;
- no invalid transform, stale payload, or wrong-owner writes in evidence.

Use the installed companion and packaged evidence collectors after the user
has run Skyrim VR. Do not infer runtime success from build or deployment
success alone.

## Sensitive And Proprietary Material

The private archive may contain locally supplied third-party archives and a
local `SkyrimVR.exe` solely for binary comparison. Do not redistribute them.
The archive excludes SSH keys, server passwords/configuration, Codex telemetry,
shell histories, and raw session logs.
