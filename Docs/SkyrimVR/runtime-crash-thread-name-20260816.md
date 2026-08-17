# Proton startup crash: MSVC thread-name notification

## Incident summary

Two captured Skyrim VR launches reached the first verified `Main::Draw` callback
and the Skyrim Together VR client startup hook, then terminated before network
bootstrap. Both runs recorded the same top-level exception code:

```text
0x406D1388
```

This is the legacy MSVC thread-name notification raised through Windows
structured exception handling. It is a continuable control-flow notification,
not an access violation or application crash.

## Root cause

The mapped executable launcher suppresses this notification through an import
hook, but that hook covers only the mapped main image's `Kernel32!RaiseException`
IAT entry. A DLL, a dynamically resolved call, a `KERNELBASE` path, or a Proton
implementation path can bypass it.

The client top-level crash filter assumed that any exception reaching it was
terminal. Under Proton, the thread-name notification escaped the IAT hook and
reached that filter. The filter armed its one-shot crash latch, wrote fatal
diagnostics, and returned `EXCEPTION_CONTINUE_SEARCH`; Wine then terminated the
process. SKSEVR and the required VR plugins had loaded successfully, so this was
not a server, authentication, address-library, or mod-load failure.

## Why prior validation missed it

The crash-handler tests covered direct synthetic access violations, prior-filter
chaining, recursive entry, dump behavior, and exceptions handled by an inner SEH
frame. They did not send a nonfatal control-flow exception through the real
Windows exception dispatcher. Review also treated the launcher's IAT suppression
as if it were process-wide, despite the hook applying only to one import table.

This was therefore a boundary-coverage failure: unit tests validated terminal
exception handling, while the untested Windows/Proton delivery path violated the
assumption used to classify exceptions.

## Corrective controls

- The top-level filter resumes only the continuable `0x406D1388` notification
  before arming crash capture. A noncontinuable record with the same code follows
  the normal fatal/delegated path.
- A Windows subprocess probe raises a protocol-accurate thread-name notification
  through the OS dispatcher and proves that the STVR filter handled it without
  arming crash capture.
- Unit coverage verifies the ignored-event counter, prior-filter isolation,
  noncontinuable handling, and subsequent real-exception capture.
- Crash-filter installation is process-global and preserves the original filter
  chain across duplicate `CrashHandler` instances.
- The crash-diagnostics source audit checks these controls and runs as part of the
  complete readiness/build path.
- Other exception codes remain delegated. C++/CLR exceptions, breakpoints,
  single-step and guard-page events, control-C/break, and debug-print exceptions
  are not broadly ignored without runtime evidence and an explicit protocol.

## Validation status

The focused native Windows test suite must pass before packaging. The remaining
runtime proof is one Proton/GE-Proton launch with the corrected package, followed
by normal connection bootstrap. No additional Skyrim launch is required to
collect existing evidence; use the packaged crash-evidence collector after a
failed run.
