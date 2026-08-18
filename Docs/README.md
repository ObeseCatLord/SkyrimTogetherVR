# Skyrim Together VR Documentation

The active Skyrim VR port documentation lives in [`SkyrimVR`](SkyrimVR/).

Start with these documents:

- [`porting-status.md`](SkyrimVR/porting-status.md): implemented scope, safety boundaries, and remaining work.
- [`windows-build.md`](SkyrimVR/windows-build.md): reproducible Windows/WinBoat build and package procedure.
- [`connection-only-mode.md`](SkyrimVR/connection-only-mode.md): staged targets and capability boundaries.
- [`vr-pose-replication.md`](SkyrimVR/vr-pose-replication.md): pose/VRIK relay and current CommonLib embodiment scope.
- [`linux-monado-prerelease-guide.md`](SkyrimVR/linux-monado-prerelease-guide.md): Linux and Monado install/test workflow.
- [`linux-local-multi-client.md`](SkyrimVR/linux-local-multi-client.md): isolated same-host simulated Monado clients with owned transient cgroups.
- [`server-deployment.md`](SkyrimVR/server-deployment.md): dedicated-server deployment and the existing Foundry service.
- [`runtime-connection-result-20260818-c18ca1d8.md`](SkyrimVR/runtime-connection-result-20260818-c18ca1d8.md): latest isolated Linux/Monado one-client authentication proof and XRizer regression diagnosis.
- [`final-handoff-checklist.md`](SkyrimVR/final-handoff-checklist.md): build, install, and two-client acceptance checklist.
- [`original-gameplay-parity-checklist.md`](SkyrimVR/original-gameplay-parity-checklist.md): living source/build/runtime matrix for parity with the original branch.
- [`local-agent-complete-handoff.md`](SkyrimVR/local-agent-complete-handoff.md): private all-dependencies handoff layout and local-machine test instructions.
- [`windows-gameplay-build-result-20260815-0fd7a319.md`](SkyrimVR/windows-gameplay-build-result-20260815-0fd7a319.md): exact clean Windows build, package, evidence, and handoff result for the current candidate.

Files with dated `senior-brief`, `senior-disposition`, `review`, or `result` names
are engineering records. They preserve the state and decisions at that date and
may describe superseded prototypes. The current behavior is defined by the
undated operational guides above and the source tree.
