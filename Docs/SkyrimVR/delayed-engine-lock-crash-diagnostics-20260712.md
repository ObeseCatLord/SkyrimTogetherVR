# Delayed Engine-Lock Crash Diagnostics - July 12, 2026

## Scope

This note records the remaining runtime failure after the SKSEVR Steam-shim,
HIGGS startup, and pre-entry hook fixes. It is not a claim that the underlying
engine-lock fault has been fixed.

## Observed Failure

The July 12 minidump recorded an access violation about one minute after a
successful startup. The faulting instruction was `SkyrimVR.exe+0xC42410`, which
the VR Address Library maps to ID `66982`,
`RE::BSReadWriteLock::UnlockForRead`. The prior minidump did not retain mapped
engine code, so it cannot identify the caller that supplied the invalid lock.

## Changes Included In The Next Windows Build

- Retain the five newest `crash_*.dmp` files instead of deleting generic paths
  containing `crash`.
- Request module, thread, memory-map, and mapped-code information from
  `MiniDumpWriteDump`, while avoiding a full-memory dump.
- Report dump-file creation and dump-write failures in `logs/tp_client.log`.
- Keep these targeted VR dumps enabled even when the source branch makes
  `IS_MASTER` true.
- Fail before game entry when a delayed MinHook create/enable operation fails;
  failed hook objects cannot remove hooks they did not install.
- Record the immutable Git revision, a full source-tree fingerprint, and a
  SHA-256 inventory of every staged package file. The audit rejects unknown or
  unapproved dirty source states, missing files, and byte mismatches.

## Review Disposition

An independent release review found two package-integrity gaps: a dirty source
tree had only a generic label, and only copied binaries were hashed. Both are
resolved by build-manifest schema v2. Clean builds require a committed source
tree; `-AllowDirtySource` is explicit and records the content fingerprint.

## Required Next Reproduction

1. Build a fresh Windows package from this source. The prior package does not
   contain these diagnostics and must not be used as evidence for this change.
2. Install only the audited package and reproduce the delayed crash once. Do
   not delete prior logs or dumps first; the handler now retains five dumps.
3. Preserve the two newest `crash_*.dmp` files, `logs/tp_client.log`, and the
   package's `SkyrimTogetherVR_BuildManifest.json`.
4. Analyze the new dump before changing hook coverage or disabling unrelated
   VR features. Its mapped-code and thread data are intended to identify the
   caller of `BSReadWriteLock::UnlockForRead`.

The runtime remains unverified until that build is installed and exercised in
Skyrim VR. A successful package audit only proves staging integrity and
prerequisites; it cannot prove that the engine-lock caller is correct.
