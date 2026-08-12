# SkyrimTogetherVR Runtime Connection Result

Date: 2026-08-11
Build: `a4b90e0129197039f2a7f94170caf618c8ab8965`
Network version: `stvr-v0.1.0-alpha.1-68-ga4b90e01`

## Windows Package

- Gameplay build passed `3922` assertions in `48` cases.
- Package and evidence audits reported zero failures.
- Installed gameplay executable SHA-256:
  `88bd788f9ff1ba6100c02bef7327e3aca36ade377ed58ca1b51e8b35efad426b`
- Installed gameplay bridge SHA-256:
  `0f23bd53fa2ba79a27663af37b86020356a167b2a4474f2b2a06f401d8e2ac91`

## Foundry Deployment

- Endpoint: `incidentalstoat.xyz:26099`.
- Exactly one `skyrim-together-vr` container was present; zero restarts.
- Image: `skyrim-together-vr-server:a4b90e01-arm64`.
- Image ID: `sha256:2055ddf7e05527378e1e901a0948db6cd21b369c37f77796db265f193277244f`.
- Server executable SHA-256:
  `a45f474ee0d4509cd307c389b0aff5a742c6b0043dca7cab0a8ae51edcf0744b`
- Server library SHA-256:
  `2ea02772fb7324eb39730ee0c8178976af292d8519fe5663b8f413e32590870f`

## Live Result

A fresh Linux/Monado New Game reached RealmLorkhan with player `Shezarrine`.
`SkyrimTogether.esp`, VRIK, HIGGS, and PLANCK were loaded. The Fader Menu
remained open, but lifecycle stabilized at `ready epoch=3` with
`bridgeLifecycleEpoch=2`, the intended lifecycle fix.

Automation initially false-failed while `playercell` was at epoch 2; it
advanced to epoch 3 and the atomic command was issued afterward. The client
became online as `playerId=1`, generation `1`. `interiorCellRequestCount=1`
and `lastCell` was server mod ID `8` / base ID `107974`; translation failures
were zero.

The avatar connected and was assigned ID 1 as `bootstrap_ready` with `62/62`
records and no bootstrap, rejected, drop, or critical failures. The server
admitted `Skyrim VR Player` with 10 mods. Five initial no-routable-character
drops stopped immediately after assignment. The connection remained stable
and the game was deliberately left running; no shutdown audit is claimed.

This proves one client connection, authentication, cell synchronization, and
avatar bootstrap. It does not prove a two-client remote avatar or deliberate
per-lane gameplay replication.
