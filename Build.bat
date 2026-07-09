@echo off
setlocal EnableExtensions

rem Legacy entry point kept for Windows users who expect Build.bat.
rem This repository is Skyrim VR-only; do not build the old Skyrim SE target graph here.
rem For DLL-only builds, use BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat directly.

if /I "%~1"=="--help" goto :usage
if /I "%~1"=="-h" goto :usage
if "%~1"=="/?" goto :usage

call "%~dp0BuildAndAuditSkyrimTogetherVR-Windows.bat" %*
exit /b %ERRORLEVEL%

:usage
echo Usage:
echo   %~nx0 [BuildAndAuditSkyrimTogetherVR-Windows.bat options]
echo.
echo This is a Skyrim VR-only compatibility wrapper.
echo It delegates to BuildAndAuditSkyrimTogetherVR-Windows.bat and never builds
echo Skyrim SE targets.
echo.
echo Examples:
echo   %~nx0
echo   %~nx0 --avatar-sync
echo   %~nx0 --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
echo   %~nx0 -- -Mode release
exit /b 0
