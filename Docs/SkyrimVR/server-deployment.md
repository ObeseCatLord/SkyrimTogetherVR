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

For the existing Foundry ARM64 host, incremental transfer, native build, safe
replacement, verification, and rollback are automated:

```bash
Tools/SkyrimVR/server/deploy_foundry_server.sh
```

The command uses incremental SSH transfer and does not require manually making
or copying a source archive. It replaces only the `skyrim-together-vr`
container and preserves the existing config, Data, and log directories. One
bounded source cache is retained under `/home/ubuntu/.cache/skyrim-together-vr`
so later deployments transfer only changed files.

If an image build succeeded and a later deployment check failed, the exact
already-built image can be reactivated without another compile after correcting
the check:

```bash
Tools/SkyrimVR/server/deploy_foundry_server.sh --activate-image \
  skyrim-together-vr-server:<commit>-arm64
```

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
- Image tag: `skyrim-together-vr-server:eab8067f-arm64`.
- Image ID:
  `sha256:0792e4da52f567719886ec5363def0db629d65179ddcfb08008695e37737c077`.
- Runtime executable SHA-256:
  `bb1126a4b4b2195b8914ca9a9e8d27e0c30c4cb887ef34dd1367ab38df880a13`.
- Server deployment revision: `eab8067f547b7ac1c8cf8e66c4402bf5630d588d`,
  reports `stvr-v0.1.0-alpha.1-108-geab8067f`, and requires gameplay
  protocol revision 21. A matching client has not yet been built or deployed.
- No server password is currently configured; maximum players: 8; auto-party join and experience synchronization enabled;
  SKSE and MO2 allowed; mod checking disabled; server listing non-public.
- No `loadorder.txt` is installed, which is currently non-blocking only because
  mod checking is disabled.

The current deployment has one container and zero restarts, listens on UDP port
26099, and retains the existing persistent config, Data, and log mounts. It is
server-build and startup proof only. Build the client from the matching network
revision before connection testing. Rebuild and redeploy whenever shared
message definitions, encoding, or server code changes.

The existing test server currently has an empty `sPassword`. If a password is
configured later, keep it outside the repository and handoff and obtain it
privately from the operator.
