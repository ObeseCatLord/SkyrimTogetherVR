# Final FBT Integration Review Disposition

Date: 2026-07-14

## Review Status

The requested Sol Max review was started against
`fbt-final-integration-senior-brief-20260714.md`, but the reviewer exhausted
its service quota before returning findings. A subsequent read-only Spark
review also exhausted its separate quota before returning findings. Neither
attempt produced a review verdict, and neither is represented as successful.

The final pre-build disposition below is therefore based on the main-agent
source review plus the two completed earlier focused Spark reviews. No build or
test was run during this review phase.

## Disposition

| Finding | Disposition |
|---|---|
| VRIK-only pose updates were dropped | Fixed. `HasAnyVRPosePayload` includes VRIK state and is used by server and client ingress. |
| Client cached server pose without validation | Fixed. Client ingress now calls the shared `IsVRPoseUpdateSafe` validator. |
| Tick endpoint initialization could leave sticky mapped state | Fixed. Environment-publication failure unmaps the view, closes the handle, and clears both globals. |
| HIGGS endpoint publication had a first-use race | Fixed. The mapped endpoint is published with an atomic compare/exchange before callbacks consume it. |
| Callback teardown could wait forever | Fixed. Teardown marks the endpoint and capture mailbox inactive without waiting on a callback. Process-lifetime mapping and interned strings prevent shutdown UAF. |
| Runtime capability could be confused with compile-time availability | Addressed diagnostically. Compatibility reports build policy while HIGGS reports endpoint faults, attempts, successes, and callback result separately. |
| Full body packet exceeded the old 1000-byte test fixture | Fixed. Only affected body-pose round-trip fixtures now allocate 4096 bytes. Runtime client/server buffers are 64 KiB and 1 MiB respectively. |
| Body packet may exceed a single network MTU | Accepted. TiltedConnect sends the reliable message through Steam Networking Sockets, which provides message fragmentation, and packet compression is attempted before send. The expected rate is bounded by the existing 45 ms relay interval. |
| Fixed-order schema is incompatible with mixed revisions | Accepted deployment constraint. Client and server must be built from the same revision; nested body versioning fails closed but is not wire negotiation. |
| Sequence comparison uses modular signed subtraction | Accepted. This is the conventional half-range ordering rule; sequence jumps of 2^31 or more are outside the producer contract. |
| Remote body data is not written to actor bones | Accepted first-stage safety boundary. Data is captured, sent, validated, cached, and observable. Remote rendering remains disabled until an actor-specific post-animation ownership point is proven. |

## Local Review Checks

- Endpoint ABI remains 0x50 bytes at ABI version 3 and uses a launcher-relative
  callback RVA.
- HIGGS maps the endpoint outside the real-time callback and performs no file
  I/O, network send, remote actor lookup, or explicit allocation in the body
  callback.
- Body mailbox operations use nonblocking SRW acquisition on capture and read;
  stale samples become explicit invalid format-1 samples after 250 ms.
- Server and client both validate finite values, bounded positions/scales,
  near-orthogonal unit bases, and right-handed body transforms.
- `CharacterService` records remote body validity and sequence but performs no
  pelvis or leg scene-node writes.
- Companion summaries expose FBT installed/loaded state, body capture policy,
  endpoint fault state, callback counts/results, body validity, and sequences.

## Remaining Runtime Proof

The final build must pass before deployment. In game, evidence must show an
increasing `bodyCapture.attemptCount`, increasing success count, a non-faulted
endpoint, and `local.body.valid=1`. Two matched clients must then prove that the
remote body sequence increases. This stage does not claim remote leg rendering.
