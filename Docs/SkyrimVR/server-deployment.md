# Skyrim Together VR Server Deployment

The server is the project server target built from this repository. Do not use
the public `tiltedphoques/st-reborn-server:latest` image when validating VR port
changes; that image is the upstream desktop release and may not contain the VR
relay services in this branch.

## Build and Run with Docker

From a clean clone with submodules initialized:

```bash
git clone --recursive https://github.com/ObeseCatLord/SkyrimTogetherVR.git
cd SkyrimTogetherVR
Tools/SkyrimVR/server/build_server_image.sh \
  skyrim-together-vr-server:stvr-v0.1.0-alpha.1
```

The helper requires a clean source tree. It uses the Dockerfile's xmake cache
mounts when BuildKit/buildx is available. On older Docker hosts without the
buildx component, it creates a temporary deployment-only Dockerfile that omits
only those optional cache mounts and runs the same xmake build. The tracked
Dockerfile and source tree are not modified. The Dockerfile builds the
`SkyrimServerRunner` target and its `SkyrimTogetherServer` dependency
explicitly; unrelated Linux test targets are not part of the runtime image
build.

Create persistent directories and start with the provided Compose example:

```bash
mkdir -p "$HOME/stvr-server"/{config,Data,logs}
export STVR_SOURCE_DIR="$PWD"
export STVR_SERVER_ROOT="$HOME/stvr-server"
export STVR_SERVER_IMAGE="skyrim-together-vr-server:stvr-v0.1.0-alpha.1"
docker compose -f Tools/SkyrimVR/server/docker-compose.vr.example.yml up -d
docker logs -f skyrim-together-vr
```

The first run creates `config/STServer.ini`. Stop the container, edit that file,
and restart. Relevant settings are:

```ini
[GameServer]
uPort=26099
uMaxPlayerCount=8
sServerName=Skyrim Together VR Alpha
sPassword=
sAdminPassword=

[Gameplay]
bAutoPartyJoin=true
bEnableXpSync=true

[ModPolicy]
bEnableModCheck=false
bAllowSKSE=true
bAllowMO2=true

[LiveServices]
bAnnounceServer=false
```

Use a nonempty private password on an Internet-facing test server. Do not
commit the resulting INI. With mod checking disabled, every tester must still
use a coordinated load order. If mod checking is enabled, populate
`Data/loadorder.txt` before startup.

Open the selected **UDP** port in the host firewall and provider firewall. For
an iptables host, inspect before changing it:

```bash
sudo iptables -C INPUT -p udp --dport 26099 -j ACCEPT || \
  sudo iptables -I INPUT -p udp --dport 26099 -j ACCEPT
```

Persist firewall rules using the host distribution's normal mechanism. Do not
publish a TCP-only rule; the game protocol uses UDP.

Useful operations:

```bash
docker ps --filter name=skyrim-together-vr
docker logs --tail 200 skyrim-together-vr
docker restart skyrim-together-vr
docker stop -t 10 skyrim-together-vr
```

Keep exactly one container bound to the test port. A stale duplicate can admit
or reject clients independently and invalidate results.

## Existing Test Server

The current shared endpoint is `incidentalstoat.xyz:26099/udp`.

- Container: `skyrim-together-vr`, restart policy `unless-stopped`, host
  networking, Linux ARM64.
- Image tag: `skyrim-together-vr-server:a7b71d90`.
- Image ID:
  `sha256:2d5757bb014826ed2cfc360ed036804f8edda1d9006a6cde8f09d6c4c6f8650f`.
- Runtime executable SHA-256:
  `23c6ff365a390138faa6d2d4b4286c10408b47197a5d4fc89dd89cb36c4c6c21`.
- Runtime core SHA-256:
  `4360245debe4dbaf28c1094245d85a70f7de37f5431ddd96ec222cff8b76287d`.
- Server binary source revision: `a7b71d900a72c44e8e31436a245fe448b97d0daa`;
  startup reports `stvr-v0.1.0-alpha.1-54-ga7b71d90`.
- Packaged client binary source revision:
  `a7b71d900a72c44e8e31436a245fe448b97d0daa`.
- No server password is currently configured; maximum players: 8; auto-party join and experience synchronization enabled;
  SKSE and MO2 allowed; mod checking disabled; server listing non-public.
- No `loadorder.txt` is installed, which is currently non-blocking only because
  mod checking is disabled.

The client and server binaries use the same `a7b71d90` source and exact network
version. The container was recreated through the existing Compose project on
2026-07-16 and verified with one container, zero restarts, host networking, UDP
26099 listening, and the existing public-zone `26001-27000/udp` firewall
allowance. A Linux/Monado gameplay client completed authentication as player 1
with 10 plugins and synchronized its Realm of Lorkhan interior cell. See
`runtime-connection-result-20260716-a7b71d90.md`. Rebuild and redeploy whenever
shared message definitions, encoding, or server code changes.

The existing test server currently has an empty `sPassword`. If a password is
configured later, keep it outside the repository and handoff and obtain it
privately from the operator.
