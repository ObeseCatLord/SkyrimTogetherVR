# Senior Review Disposition: Skyrim VR Lifecycle Menu Predicate

| Recommendation | Disposition | Result |
| --- | --- | --- |
| Correct the brief's `IMenu::menuFlags` offset from `0x20` to `0x1C`. | Adopted | Source and audits assert flags `0x1C`, input context `0x20`, and `kOnStack` `0x40`. |
| Replace the boolean predicate with an explicit tri-state. | Adopted | `MenuOpenState` distinguishes `Unavailable`, `Closed`, and `Open`; lifecycle maps every unavailable probe to `ui_unavailable`. |
| Do not instantiate the unsupported local hash lookup for `BSFixedString`. | Adopted | The VR probe validates metadata/backing storage and scans occupied entries linearly by interned key. |
| Validate map metadata, entry storage, and menu flag storage. | Adopted | Invalid capacity/free metadata, unreadable entry allocation, or unreadable menu flags fail closed. |
| Keep ID `82074` desktop-only. | Adopted | The relocated call remains under the non-VR preprocessor branch. |
| Add predicate tests and static gates. | Adopted | Focused tests cover absent/null/off-stack/on-stack/unreadable/invalid states; audits require the local predicate and repair the overwritten lifecycle requirement. |
| Preserve owner-thread-only use. | Adopted | The lifecycle caller remains exclusively downstream of the established `Main::Draw` owner dispatch. |
