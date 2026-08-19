# Exact Gameplay-Package Connection Result (`9eba3bb5`)

## Result

On 2026-08-19, the exact Windows gameplay package built from
`9eba3bb5d540326bc946e5811fdca3bc72118810` connected through XRizer/Monado to
the matching ARM64 Foundry server. This is successful one-client gameplay
bootstrap and current-cell evidence. It is not two-client gameplay-parity proof.

The accepted artifacts are:

- gameplay package: `SkyrimTogetherVR-gameplay-9eba3bb5-20260819150826Z.zip`
  (`ed7bb0034994e719435a082c8cb5d65a750e9b521abfac4b6a3fa09bff694ed8`)
- build evidence: `SkyrimTogetherVR-build-evidence-gameplay-20260819-152105Z.zip`
  (`0b5dac612a0d67074a3539532ffd8a2bd5523d7e3dca64929551c4aa84102e0e`)
- runtime evidence: `SkyrimTogetherVR-runtime-gameplay-bootstrap-9eba3bb5-20260819.zip`
  (`a0120be2c602958ce9bcdabcafefc237e2c6a2b9208fdd8529ad526d74295ea7`)

The Windows build passed 5,444 assertions across 217 test cases. Package and
build-evidence audits reported zero failures. The runtime archive passed all 18
required gameplay-bootstrap checks with zero failures and zero warnings.

## Runtime Flow

The isolated handoff-only install loaded the finalized Sleeping Giant Inn save
through DevBench, bypassing main-menu navigation. It then verified the exact
ten-plugin release order, a finalized Nord player, resumed SKSE task cadence,
and a stable HUD-only state before connecting to `incidentalstoat.xyz:26099`.

The client authenticated as player `1`, synchronized interior cell `0x000133C6`,
completed all 65 local avatar bootstrap records, and held assignment state
across later cadence checkpoints. The live status reported protocol revision
`17`, connection generation `1`, bridge mapping ABI `23`, capability revision
`34`, and zero bridge rejections or ring drops. The current launch produced no
critical gameplay-bridge entry and no respawn/MoveTo status-2 rejection.

## Server Correlation

Foundry ran exactly one `skyrim-together-vr` container from image
`skyrim-together-vr-server:9eba3bb5-arm64`, image ID
`sha256:8fb348622e4ab8db0c0bada8b7cc83cab457fe94134ee10667b1214fa84f0614`.
The server accepted the same connection with protocol revision `17`, requested
and negotiated capabilities `0x1ff80e0f`, and network version
`stvr-v0.1.0-alpha.1-99-g9eba3bb5`. Its runtime executable SHA-256 is
`6a20bd5846e1fafc09b446dd7e390fe6ad6f7dd20d84ccf1ee97eb57545aec7c`.

## Scope

The revised unattended New Game selector was not used for this acceptance run.
Its previous End/Home anchoring selected Quit; it now clamps to the first row
with repeated Up input and passes focused tests, but still requires a separate
live New Game/RaceSex gate. Manual New Game remains supported.

Two-client movement, inventory, combat, magic, projectiles, grabbing, save/load,
reconnect, server restart, and long-session behavior remain outside this
one-client result and require paired gameplay evidence.
