# Runtime Version Senior Review Disposition

| Recommendation | Disposition | Result |
| --- | --- | --- |
| Read translated ProductVersion/FileVersion rather than fixed root fields. | Adopted | `QueryFileVersion` enumerates `VarFileInfo/Translation`, validates returned spans, and never uses `VS_FIXEDFILEINFO`. |
| Use a narrow 0409/04B0 fallback only when Translation is absent. | Adapted | The one supported executable has a verified Translation block. The final implementation requires it because WinAPI does not distinguish an absent query from every malformed-resource failure. |
| Parse exactly four bounded decimal fields into POD. | Adopted | The shared parser rejects signs, whitespace, junk, embedded NULs, missing/extra fields, zero version, and uint16 overflow. |
| Reject non-1.4.15.0 before manual mapping. | Adopted | `LoadProgram` gates before reading the executable, `LC.SetLoaded`, and `ExeLoader::Load`; `VersionDb` retains its second check. |
| Require values across translations and Product/File keys to agree. | Adopted | Conflicting present values fail rather than selecting an arbitrary resource. |
| Add a real VERSIONINFO regression fixture. | Adopted | The Windows test executable has fixed 1.0.0.0 and translated 1.4.15.0 strings; the test requires 1.4.15.0. |
| Add strict parser edge tests. | Adopted | Unit coverage includes field count, punctuation, signs, whitespace, suffixes, embedded NUL, 65535, 65536, and zero. |
| Add a call-order test around the mapper. | Adapted | A source audit enforces query, exact gate, executable read, mapped-state transition, then mapper call. Dependency-injecting the established mapper solely for a unit test would add disproportionate churn. |
| Ensure the regression fixture is compiled by the release workflow. | Adopted from final review | The Windows build wrapper now builds and runs `TPTests` before production targets and packaging. |
| Reuse or remove the legacy client parser. | Rejected for this correction | The launcher does not reuse it. Removing an unused client API is unrelated cleanup and is deferred. |
| Return immediately after address-load failure. | Adopted from main-agent spot check | `RunTiltedInit` now stops after the actionable dialog instead of continuing with an empty database. |
