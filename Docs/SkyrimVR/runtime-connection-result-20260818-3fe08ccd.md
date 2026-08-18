# Exact Gameplay-Package Connection Result (`3fe08ccd`)

## Result

On 2026-08-18, the exact audited Windows gameplay package built from
`3fe08ccd99b0d4cfa14c5dab872fa9c37f67d6c8` was installed from the private
local-agent handoff and connected through XRizer/Monado to the matching ARM64
Foundry server. This is a successful one-client bootstrap and current-cell
acceptance result. It is not two-client gameplay-parity evidence.

The private handoff source head was `4c631880`. Its package/evidence pair and
all 14,375 payload files passed the archive audit, ZIP integrity check, local
sidecar verification, resumable Foundry upload, and remote sidecar verification.
The installed runtime hashes were:

- XRizer: `b278c4695f15bba7c554aaac5303520247cc8ab3bcae3f8b55e934e2b114ccaf`
- OpenComposite: `a703fdd1eaff092d28d91798b0ad1afb1611523da1456563dd51f2892b471751`

## Automated Flow

`devbench_new_game.py --launch-game --vm-update-mode active --connect
incidentalstoat.xyz:26099` completed the no-save flow through the handoff-only
test install:

1. Selected New Game.
2. Sent the bounded XRizer Grip, visible confirmation Trigger, and hidden
   default-name Trigger stages.
3. Finalized the player and entered `RealmLorkhan`.
4. Accepted only the allowlisted Realm introduction.
5. Observed two resumed SKSE task checkpoints before connection.
6. Authenticated, synchronized the current interior cell, completed the 62/62
   local assignment bootstrap, and held assignment state across two later task
   checkpoints.

The automation reported player `1`, connection generation `1`, interior-cell
request count `1`, and stable cadence sequences `203,238`. The final client
status reported `online=1`, session `184384451887732`, lifecycle `ready`, and
zero assignment-bootstrap failures or ring drops.

The no-launch collector captured the still-live client with the explicit
`--gameplay-bootstrap` profile. The resulting archive is
`SkyrimTogetherVR-runtime-evidence-gameplay-bootstrap-3fe08ccd-20260818.zip`
with SHA-256
`457a449e3ea903fc2d5c3c8bda01a07bfa481d5dc2b691f11040da6e16c69d08`.
The offline archive audit passed with 21 entries, 16 required checks passed,
zero failed, and 16 full-gameplay checks correctly marked not required.

## Server Correlation

Foundry ran exactly one `skyrim-together-vr` container from image
`skyrim-together-vr-server:3fe08ccd-arm64`, with zero restarts and UDP 26099
listening. The server accepted the same client session and generation:

- player ID: `1`
- protocol revision: `14`
- requested/negotiated capabilities: `0xbc2e0f`
- client session nonce: `184384451887732`
- server instance nonce: `9726510627282421975`
- connection attempt/generation: `1/1`

The image embeds `stvr-v0.1.0-alpha.1-84-g3fe08ccd`. Its runtime executable
SHA-256 is `8965dbe5d7a9e5f579587240a6de22917791602642193cfdd88d293c450ccf89`;
the server core SHA-256 is
`882fbbb50a45e9478fc9793d4cf96f0e37101824b4b3044c2d53e14137f01629`.

## Presentation State

During the accepted run, a black headset view did not indicate a networking or
process failure. The gameplay process remained active, `Main::Draw` continued
at approximately 60 Hz, XRizer's OpenXR session remained `VISIBLE` and
`FOCUSED`, Monado retained the Skyrim client, and bridge-health counters kept
advancing online with no event- or command-ring drops. Those observations do
not prove that the submitted game texture was nonblack. Without a contemporaneous
menu-state probe or captured eye texture, the remaining cause is classified as
an XR presentation or Skyrim render/fade-state issue rather than attributed to
the server connection.

## Scope

The strict `--gameplay` evidence profile remains intentionally red for this
run because it requires a second remote player and deliberate exercise of all
movement, equipment, activation, magic, combat, projectile, grab, HIGGS, and
save/load lanes. Shutdown evidence is also unavailable while the accepted
client remains running. Those failures must not be presented as failures of
the proven connection/bootstrap path or as gameplay-parity proof.
