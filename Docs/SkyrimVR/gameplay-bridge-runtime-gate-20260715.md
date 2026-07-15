# CommonLib Gameplay Bridge Runtime Gate

## Failure

The first automated gameplay run reached a stable Realm of Lorkhan state but
the mapped client disabled itself before networking. Its bootstrap returned
`CommandPumpResult::Inactive`. SKSEVR's current log proved the adapter had
returned false from `SKSEPlugin_Load` and had then been unloaded:

```text
SkyrimTogetherVRGameplayBridge.dll ... reported as incompatible during load
```

The adapter log identified the rejected runtime. Official SKSEVR 2.0.12
publishes packed runtime `0x010400F1`, while CommonLib's
`SKSE::RUNTIME_VR_1_4_15` represents SkyrimVR.exe file version `1.4.15.0` and
packs as `0x010400F0`.

## Contract

These values describe different interfaces and must remain distinct:

| Identity | Value | Use |
| --- | --- | --- |
| SkyrimVR.exe / VR Address Library | `1.4.15.0`, `0x010400F0` | executable ABI, CommonLib module detection, `version-1-4-15-0.csv`, shared mapping identity |
| SKSEVR plugin interface runtime | `1.4.15.1`, `0x010400F1` | `SKSEInterface::runtimeVersion` gate |
| Minimum SKSEVR | `2.0.12`, `0x020000C0`, release `60` | plugin loader/API gate |

The gameplay adapter validates the SKSE interface runtime, packed SKSE version,
and release index before calling `SKSE::Init`. It retains `.0` in the shared
mapping because that field describes the mapped executable and address-library
ABI, not the script-extender interface.

## Senior Review Disposition

| Recommendation | Disposition |
| --- | --- |
| Keep the correction local instead of changing CommonLib's global constant. | Adopted. |
| Accept only the `.1` SKSE interface identity, not `.0`/`.1`. | Adopted. |
| Enforce SKSEVR 2.0.12 before CommonLib initialization. | Adopted with packed version and release-index gates. |
| Add retry/deferred attachment. | Rejected; official SKSEVR unloads a DLL after `SKSEPlugin_Load` fails, so retry cannot recover this failure. |
| Retain `.0` in the mapping header. | Adopted. |

## Runtime Acceptance

The next Windows build is not accepted merely because it compiles. A fresh run
must prove all of the following from the same process:

1. `sksevr.log` reports the gameplay bridge loaded correctly.
2. `SkyrimTogetherVRGameplayBridge.log` records validated runtime
   `0x010400F1`, SKSE at least `0x020000C0`, and release at least `60`.
3. `tp_client.log` has no owner-thread bootstrap failure and reports recurring
   client updates.
4. lifecycle and player-cell handoffs reach Ready.
5. the server admits the client and assigns a nonzero player ID.
