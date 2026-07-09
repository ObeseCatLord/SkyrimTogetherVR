@echo off
setlocal

rem Collects strict no-launch two-client VRIK/HIGGS avatar-sync runtime evidence.
rem Extra arguments are forwarded to CollectSkyrimTogetherVREvidence-Windows.bat.

set "ROOT=%~dp0"
set "EVIDENCE_COLLECTOR=%ROOT%CollectSkyrimTogetherVREvidence-Windows.bat"

if not exist "%EVIDENCE_COLLECTOR%" (
    echo Could not find "%EVIDENCE_COLLECTOR%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

call "%EVIDENCE_COLLECTOR%" --avatar-sync --require-connected --require-remote-player --require-vrik --require-higgs %*
exit /b %ERRORLEVEL%
