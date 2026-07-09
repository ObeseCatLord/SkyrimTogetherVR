@echo off
setlocal

rem Audits a SkyrimTogetherVR Windows build evidence zip.
rem This script does not build targets, install files, or launch Skyrim.
rem Usage:
rem   AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-default-YYYYMMDD-HHMMSSZ.zip" --require-package --require-default
rem   AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-avatar-sync-YYYYMMDD-HHMMSSZ.zip" --require-package --require-avatar-sync
rem   AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-gameplay-YYYYMMDD-HHMMSSZ.zip" --require-package --require-gameplay
rem   AuditSkyrimTogetherVRBuildEvidence-Windows.bat "artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-dll-only-YYYYMMDD-HHMMSSZ.zip" --require-package --require-dll-only

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\audit_build_evidence_zip.py"

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
echo   python "%HELPER%" SkyrimTogetherVR-build-evidence.zip
exit /b 1
