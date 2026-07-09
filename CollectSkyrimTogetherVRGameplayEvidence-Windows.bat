@echo off
setlocal

rem Collects strict no-launch full gameplay runtime evidence.
rem Extra arguments are forwarded to CollectSkyrimTogetherVREvidence-Windows.bat.

set "ROOT=%~dp0"
set "EVIDENCE_COLLECTOR=%ROOT%CollectSkyrimTogetherVREvidence-Windows.bat"

if not exist "%EVIDENCE_COLLECTOR%" (
    echo Could not find "%EVIDENCE_COLLECTOR%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

call "%EVIDENCE_COLLECTOR%" --gameplay --require-connected --require-remote-player --require-vrik --require-higgs --require-vr-pose-context --require-gameplay-relays %*
exit /b %ERRORLEVEL%
