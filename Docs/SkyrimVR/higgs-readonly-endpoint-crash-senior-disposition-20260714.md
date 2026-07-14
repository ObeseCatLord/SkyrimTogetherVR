# HIGGS Read-Only Endpoint Crash: Senior Review Disposition

Review: Sol Max, 2026-07-14. Source brief:
`higgs-readonly-endpoint-crash-senior-brief-20260714.md`.

| Recommendation | Disposition | Result |
| --- | --- | --- |
| Replace `InterlockedCompareExchange` with a volatile State load followed by `MemoryBarrier()` | Adopted | Preserves the read-only mapping and matches the standalone tick consumer. |
| Keep `FILE_MAP_READ`; do not grant bridge write access | Adopted | The HIGGS companion remains unable to mutate the endpoint through its view. |
| Do not use `std::atomic_ref` | Adopted | Avoids introducing C++ object-model assumptions into the plain Win32 mapping ABI. |
| Add a function-scoped regression audit | Adopted | The HIGGS audit now requires the load/barrier pair and rejects interlocked, `atomic_ref`, `const_cast`, and writable mapping access. |
| Do not add per-frame endpoint `VirtualQuery` | Adopted | It would not prevent a concurrent unmap and adds cost to every post-HIGGS update. |
| Do not refactor both endpoint consumers in this patch | Adopted | The runtime fix remains narrowly scoped before the next VR smoke test. |
| Treat capture-versus-retire overlap as a possible residual risk | Adapted | `Retire` disables new capture before clearing the mailbox, retains interned node names for process lifetime, and the endpoint mapping is not unmapped. No additional startup fix is warranted; runtime teardown remains observable in later testing. |

The load-bearing evidence is the write access violation at mapped endpoint address
`0x62030000 + 0x0c`, exactly matching `Endpoint::State`, and the deployed instruction
`lock cmpxchg` at bridge RVA `0x6916`. This distinguishes the failure from a stale
pointer, HIGGS vtable mismatch, VM hook, server, or character-creation fault.
