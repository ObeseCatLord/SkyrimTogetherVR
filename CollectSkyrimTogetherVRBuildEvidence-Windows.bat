@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Collects no-build SkyrimTogetherVR Windows build/package evidence.
rem This script does not build targets, install files, or launch Skyrim.
rem Usage:
rem   CollectSkyrimTogetherVRBuildEvidence-Windows.bat
rem   CollectSkyrimTogetherVRBuildEvidence-Windows.bat --dll-only
rem   CollectSkyrimTogetherVRBuildEvidence-Windows.bat --avatar-sync --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
rem   CollectSkyrimTogetherVRBuildEvidence-Windows.bat --gameplay --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\collect_build_evidence.py"
set "PYTHON_CMD="

if not exist "%HELPER%" (
    echo Could not find "%HELPER%".
    echo Run this from the SkyrimTogetherVR repository root.
    exit /b 1
)

where py >nul 2>nul
if not errorlevel 1 set "PYTHON_CMD=py -3"
if not defined PYTHON_CMD (
    where python >nul 2>nul
    if not errorlevel 1 set "PYTHON_CMD=python"
)
if not defined PYTHON_CMD (
    where python3 >nul 2>nul
    if not errorlevel 1 set "PYTHON_CMD=python3"
)
if not defined PYTHON_CMD (
    echo Python 3 was not found. Install Python 3 or run:
    echo   python "%HELPER%"
    exit /b 1
)

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

echo %PYTHON_CMD% "%HELPER%" %*
%PYTHON_CMD% "%HELPER%" %*
set "STVR_RESULT=%ERRORLEVEL%"

popd >nul
exit /b %STVR_RESULT%
