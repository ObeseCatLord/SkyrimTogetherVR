@echo off
setlocal

rem Audits final SkyrimTogetherVR build/runtime evidence bundles.
rem This script does not build targets, install files, or launch Skyrim.
rem Usage:
rem   AuditSkyrimTogetherVRFinalHandoff-Windows.bat
rem   AuditSkyrimTogetherVRFinalHandoff-Windows.bat --build-default "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-default-YYYYMMDD-HHMMSSZ.zip" --build-avatar-sync "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-avatar-sync-YYYYMMDD-HHMMSSZ.zip" --build-gameplay "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-YYYYMMDD-HHMMSSZ.zip" --build-dll-only "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-dll-only-YYYYMMDD-HHMMSSZ.zip" --runtime-default "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-default.zip" --runtime-avatar-sync "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-avatar-sync.zip"
rem   AuditSkyrimTogetherVRFinalHandoff-Windows.bat --build-gameplay "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-YYYYMMDD-HHMMSSZ.zip" --runtime-gameplay "%USERPROFILE%\Documents\SkyrimTogetherVRRuntimeEvidence\SkyrimTogetherVR-evidence-gameplay.zip" --require-gameplay-runtime
rem With no explicit zip paths, the helper auto-discovers the newest matching build/runtime evidence archives.

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\audit_final_handoff.py"

if not exist "%HELPER%" (
    echo Could not find "%HELPER%".
    echo Run this from the SkyrimTogetherVR repository root.
    exit /b 1
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%HELPER%" %*
    exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if not errorlevel 1 (
    python "%HELPER%" %*
    exit /b %ERRORLEVEL%
)

where python3 >nul 2>nul
if not errorlevel 1 (
    python3 "%HELPER%" %*
    exit /b %ERRORLEVEL%
)

echo Python 3 was not found. Install Python 3 or run:
echo   python "%HELPER%" --help
exit /b 1
