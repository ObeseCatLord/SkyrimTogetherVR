# SKSEVR Version-Resource Guard

The default Linux/Proton launch reached the rebuilt startup guard, then reported
that SKSEVR 2.0.12 was unsupported even though
`sksevr_1_4_15.dll` was the official 2.0.12 release.

The DLL's PE resource encodes its public `2.0.12` version as `0.2.0.12`.
The client previously mixed that four-part resource into an incompatible integer
format and rejected it. `ScriptExtender.cpp` now compares the four resource
fields lexicographically against `0.2.0.12`.

This change does not alter the launch route: the default VR script must start
`SkyrimTogetherVR.exe` and pass `SkyrimVR.exe` with `--exePath`. It requires a
new Windows default package. After deployment, the client log must either say
that the SKSEVR module is loaded or report a genuine module-load failure; it
must not report `result 2` for the installed 2.0.12 DLL.
