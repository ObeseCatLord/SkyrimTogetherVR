# Final FBT Integration Senior Review Brief

Date: 2026-07-14

## Goal and Scale

Prevent another Windows rebuild cycle by reviewing the completed first-stage
SkyrimVR-FBT networking implementation before the only final compile/build.
This is a community mod port with matched client/server deployment, not a
general untrusted protocol platform. Correctness, callback ABI, and crash
avoidance matter more than backwards wire compatibility.

## Verified Evidence

- [verified: source inspection] `VRPoseUpdate` is fixed-order and now serializes
  `VRBodyPoseData` before `VRVrikData` in
  `Code/encoding/Structs/VRPoseUpdate.{h,cpp}`.
- [verified: source inspection] shared `IsVRPoseUpdateSafe`,
  `IsVRBodyPoseDataSafe`, and `HasAnyVRPosePayload` are called at both server
  ingress and client remote-cache ingress.
- [verified: source inspection] body format 1 carries pelvis local
  translation/rotation and six limb local rotations, plus capture sequence and
  root generation. Spine is omitted.
- [verified: supplied FBT source inspection] FBT replaces VRIK's `updateBody`
  wrapper, calls VRIK's real update, then runs `OnPostVrikUpdate`; the FBT body
  pass computes local transforms after world-space solves.
- [verified: source inspection] the HIGGS bridge registers
  `AddPostVrikPostHiggsCallback` and calls a launcher-relative body callback
  through endpoint ABI 3.
- [verified: source inspection] endpoint mapping is published atomically;
  callback resolution validates magic, ABI, size, PID, state, image base, RVA,
  executable page, epoch, sequence, thread id, and reserved fields.
- [verified: source inspection] HIGGS callback does not log, write files, send
  network data, allocate explicitly, access EnTT, or look up remote actors.
- [verified: source inspection] producer/consumer use nonblocking SRW mailbox
  operations; stale data is invalid after 250 ms.
- [verified: source inspection] `CharacterService` only reports remote body
  validity/safety/sequence. It performs no body bone writes.
- [verified: installed files] local FBT profile matches installed VRIK PE
  timestamp `0x69277C5C`, wrapper RVA `0x48B0`, body update `0xE3B0`, instance
  `0x10D298`, walk `0x180D0`; mismatch override is disabled.
- [verified: archive listing] supplied FBT source contains no license or build
  project, so it is not vendored or redistributed.
- [unverified: runtime] callback ordering and valid body capture have not yet
  been proven in-game. Evidence keys are `bodyCapture.successCount` and
  `local.body.valid`.
- [unverified: compile] no compile or test has been run since this code was
  written, by explicit user instruction.

## Environment Facts

| Fact | Value |
|---|---|
| Repository | `/home/obesecatlord/Documents/SkyrimModding/SkyrimTogetherVR` |
| Platform | Windows x64/MSVC client and bridge; Linux/Windows server |
| Runtime | Skyrim VR 1.4.15, SKSEVR |
| Local mods | VRIK, HIGGS, PLANCK, SkyrimVR-FBT |
| Build target | Avatar-sync/gameplay matched client plus exact same-revision server |
| Final build | WinBoat SSH, only after all code/review changes |
| Protocol policy | Matched client/server build required |
| Remote body rendering | Deliberately disabled |

## Decisions to Review

1. **Body capture callback architecture.** Current lean: keep direct
   HIGGS-to-launcher callback plus nonblocking mailbox. Rejected file polling
   because it cannot preserve frame phase and would add I/O to the producer.
2. **Endpoint lifecycle.** Current lean: mapping views and interned node names
   are process-lifetime; endpoint state retires callbacks without unloading
   shared storage. Rejected callback teardown waits because they can deadlock
   during process shutdown.
3. **Validation placement.** Current lean: shared encoding-layer pure validators
   called by both server and client. Rejected server-only trust because a bad or
   mismatched server should not feed actor consumers unchecked values.
4. **Wire compatibility.** Current lean: matched builds only; nested body version
   fails closed but does not negotiate future layouts. Rejected compatibility
   negotiation in this stage because all protocol messages are fixed-order and
   deployment already packages matched artifacts.
5. **Remote application.** Current lean: relay/cache/telemetry only. Rejected
   HIGGS callback writes because it is local-player phase, not an actor-specific
   remote animation barrier.

Possible overlap: decisions 1 and 2 are one callback ownership/lifetime design;
merge them if that improves the critique.

## Requested Review

Verify the claims against the actual files. Return prioritized findings with
exact file/line evidence. Focus on compile failures, ABI/layout mistakes,
serialization desynchronization, invalid-state semantics, callback races or
teardown UAFs, MSVC issues, and any path that could mutate or corrupt a remote
actor. Recommend the smallest concrete fixes needed before the one final build.

Do not edit files and do not run builds/tests. Do not re-review unrelated
Skyrim address mappings, connection startup, Papyrus migration, controller
bindings, or gameplay relays except where the new body schema directly breaks
them. Maximum 15 findings.

