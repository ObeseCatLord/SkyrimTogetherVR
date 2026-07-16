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
- Image tag: `skyrim-together-vr-server:3cf4aa0e`.
- Image ID:
  `sha256:3f0967165e0fd39f7da68d253eacbc61696bc9bb130bd51e07610d4206856fff`.
- Runtime executable SHA-256:
  `3a54131f667b8e7a5f9e9b70f716c51944e2b1a218d18d88846fc1c75c329246`.
- Runtime core SHA-256:
  `06a6fdbfa1d5512862247fc83b04de9f214fbf3cad7fe2c3f4e85d714a6ceb51`.
- Server binary source revision: `3cf4aa0e`; startup reports
  `stvr-v0.1.0-alpha.1-39-g3cf4aa0e`.
- Packaged client binary source revision: `3cf4aa0e`.
- No server password is currently configured; maximum players: 8; auto-party join and experience synchronization enabled;
  SKSE and MO2 allowed; mod checking disabled; server listing non-public.
- No `loadorder.txt` is installed, which is currently non-blocking only because
  mod checking is disabled.

The client and server binaries use the same `3cf4aa0e` source and exact network
version. The container was recreated through the existing Compose project on
2026-07-16 and verified with one container, zero restarts, host networking, UDP
26099 listening, and the existing public-zone `26001-27000/udp` firewall
allowance. The first exact-version runtime attempt was admitted by this server;
the remaining failure was a client-side post-authentication PlayerService call.
Rebuild and redeploy whenever shared message definitions, encoding, or server
code changes.

The existing test server currently has an empty `sPassword`. If a password is
configured later, keep it outside the repository and handoff and obtain it
privately from the operator.
