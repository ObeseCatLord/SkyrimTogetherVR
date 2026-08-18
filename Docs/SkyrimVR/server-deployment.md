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
- Image tag: `skyrim-together-vr-server:70ad5d1d-arm64`.
- Image ID:
  `sha256:da3448ba745089a4633b0c3873512e411b44c316f6a5b5f9c949072edf366aee`.
- Runtime executable SHA-256:
  `ce7b46ad509d200e2a341cc74877085fe088dc1504e3d4273a31944cbb321bd6`.
- Runtime core SHA-256:
  `21c3c1c54b7403f940bfbd95dcdb4027ee2f76bb68db966e645ad521524ec626`.
- Server deployment revision: `70ad5d1d`; the matching client build is
  `70ad5d1d0225e18d1cb8d1cc625e80ee7062e9bc` and reports
  `stvr-v0.1.0-alpha.1-81-g70ad5d1d`.
- No server password is currently configured; maximum players: 8; auto-party join and experience synchronization enabled;
  SKSE and MO2 allowed; mod checking disabled; server listing non-public.
- No `loadorder.txt` is installed, which is currently non-blocking only because
  mod checking is disabled.

The client and server use the exact `70ad5d1d` source revision. The current
deployment has one container and zero restarts, listens on UDP port 26099, and
retains the existing persistent config, Data, and log mounts. The earlier
`a4b90e01` Linux/Monado single-client bootstrap result remains documented in
`runtime-connection-result-20260811-a4b90e01.md`, but it is not runtime proof
for this newer deployment. Run fresh one-client and two-client tests before
making gameplay-parity claims. Rebuild and redeploy whenever shared message
definitions, encoding, or server code changes.

The existing test server currently has an empty `sPassword`. If a password is
configured later, keep it outside the repository and handoff and obtain it
privately from the operator.
