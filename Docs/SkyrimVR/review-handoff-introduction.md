# Review Agent Introduction

The goal of this project is to port Skyrim Together Reborn from Skyrim Special
Edition to Skyrim VR 1.4.15 while preserving an upstream-comparable codebase.
The `original-skyrim-together` branch is the imported desktop baseline and
`main` is the VR port. Review changes by comparing those branches, then use the
tagged source and packaged binary revisions in `RELEASE-MANIFEST.json` to avoid
confusing documentation-only changes with the Windows-built DLLs.

The port uses CommonLibSSE-NG and the Skyrim VR Address Library where they fit,
plus a narrow project translation/override layer for identifiers and ABIs that
cannot be consumed unchanged. VR-specific code is isolated behind runtime and
build gates. VRIK, HIGGS, and PLANCK integrations use dedicated bridge DLLs so
the core client does not take ownership of third-party internals. Networking
protocol changes are kept shared with the server; the current alpha server is
older only in client/submodule lifecycle code and is intentionally documented
as wire-compatible.

This handoff contains:

- a source snapshot with initialized submodule working trees;
- a Git bundle with `main`, `original-skyrim-together`, and the release tag;
- the exact stripped runtime ZIP built on Windows;
- the Windows build manifest, release manifest, hashes, server guide, Linux
  Monado launchers, XRizer/Monado patches, and test guidelines.

It intentionally excludes SkyrimVR.exe, SKSEVR, Address Library, FUS and mod
archives, credentials, raw session logs, and private server configuration.

Review priorities are ABI/address correctness, startup/shutdown ownership,
thread affinity and reentrancy, connection lifecycle, malformed/stale handoff
state, archive safety, and failure behavior when VRIK/HIGGS/PLANCK or Linux VR
runtime components are missing. Treat a successful connection as a narrow
milestone: multiplayer gameplay replication and clean shutdown still require
fresh runtime evidence before promotion beyond alpha.

Start with:

1. `Docs/SkyrimVR/releases/stvr-v0.1.0-alpha.1.md`
2. `Docs/SkyrimVR/linux-monado-prerelease-guide.md`
3. `Docs/SkyrimVR/server-deployment.md`
4. `Docs/SkyrimVR/windows-agent-build-guide.md`
5. `Docs/SkyrimVR/final-handoff-checklist.md`
