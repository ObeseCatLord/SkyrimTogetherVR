# Linux Monado Connection Result (c18ca1d8)

## Result

On 2026-08-18, the isolated local-agent handoff install launched through the
bundled XRizer runtime, reached the Skyrim VR gameplay update owner, and
authenticated to the matching Foundry server.

- Client source: `c18ca1d8f3ed6b61c82a87c763c3c1c1899740e4`
- Network version: `stvr-v0.1.0-alpha.1-83-gc18ca1d8`
- Client status: `online=1`, player ID `1`, lifecycle `ready`, generation `1`
- Protocol revision: `14`
- Negotiated capability mask: `0xbc2e0f`
- Server image: `skyrim-together-vr-server:c18ca1d8-arm64`
- Server image ID: `sha256:db94654b0a0f621af2b17c27c11fc7dc7ff5903157f7cc9314c4388fd680ef36`
- Server restarts during the result: `0`

The client and server reported the same client session nonce and server
instance nonce. The server changed its title from zero to one player and logged
`STVR auth accepted`; the client bridge health reported no rejected commands,
stale commands, or event/command ring drops.

## Fresh Character Follow-up

A later isolated no-save run on the same c18 client/server pair reproduced and
fixed the character-creation regression. XRizer's direct controller command
emits one intentional press, while the predecessor Qwerty automation held
Trigger for 500 ms across both Skyrim VR stages. The corrected sequence is:

1. Grip opens the RaceSex completion dialog.
2. Trigger accepts its visible OK button.
3. A second Trigger accepts Skyrim VR's hidden default-name stage.

The state-gated automation completed that sequence, finalized `Prisoner`,
entered `RealmLorkhan`, accepted the allowlisted Realm introduction, and
authenticated as player ID `3` with connection generation `3`. The current
cell was accepted as an interior cell with zero worldspace translation
failures. A stale `Fader Menu` caused a black presentation after loading; once
stable Realm/player state was proven, closing that menu restored the HUD. The
automation now performs the same exact-state cleanup and refuses mixed menu
states.

The runtime used for this follow-up has SHA-256
`432b1676c1c314e6da16dcd9bad54259657ae013a897000b367a111093d509cb`.
That hash records the exact host-built binary used for this historical run.
The portable handoff rebuild uses the same reviewed source and compatibility
patch but is built in the pinned Bullseye container; its SHA-256 is
`b278c4695f15bba7c554aaac5303520247cc8ab3bcae3f8b55e934e2b114ccaf`
and its maximum glibc requirement is `GLIBC_2.31` or older. Runtime acceptance
of that exact portable binary is tracked separately from this `c18ca1d8` run.
A full OpenVR call trace contained no `ShowKeyboard`,
`ShowKeyboardForOverlay`, or `GetKeyboardText` call during RaceSex, confirming
that the second engine/controller stage, not an XRizer keyboard callback, is
what completes this Skyrim VR transaction.

The strict automation initially rejected final avatar readiness even though
network authentication, cell synchronization, and the 62-record assignment
bootstrap were complete. The verifier incorrectly compared transport player ID
`3` with canonical server entity ID `2097153`; those are intentionally distinct
identifier domains. The corrected gate requires each ID to be nonzero while
retaining generation, bridge-epoch, assignment, bootstrap, and failure checks.

## Regression And Fix

The preceding Linux crash at `SkyrimTogetherVRGameplay.exe+0xC53E1B` occurred
before Together gameplay code ran. The bundled XRizer shared object contained
unresolved unversioned `std::experimental::filesystem` references. Proton could
not load the OpenVR runtime, so Skyrim failed while initializing VR.

The portable-runtime build now links the required filesystem support, runs
`ldd -r`, and rejects unresolved symbols. The launcher also performs a static
ELF-symbol rejection for this known bad runtime family before starting Proton.
The reviewed XRizer payload used by this result has SHA-256
`f6d1febfbc6e76707c040fb09e4c4914d0a150cecab9bccddd97e4284dcc8205`.

The Monado manager also had a false-negative listener check: an early `awk`
exit caused `ss` to receive `SIGPIPE` under `pipefail`. The listener parser now
consumes the complete stream, and its fixture produces data after the matching
line to prevent regression.

## Scope

This is one-client bootstrap and authentication proof. It does not prove
two-client actor, inventory, combat, quest, object, VRIK, HIGGS, PLANCK, FBT,
reconnect, save/load, or long-session behavior. Those remain separate
acceptance gates.
