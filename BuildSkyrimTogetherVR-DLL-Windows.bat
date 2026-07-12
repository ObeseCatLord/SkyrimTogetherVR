@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Builds the DLL-producing SkyrimTogetherVR Windows targets:
rem - Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll
rem - EarlyLoad.dll
rem The main VR client is launcher-linked as SkyrimTogetherVR.exe, not SkyrimTogetherVR.dll.
rem
rem This script can be run from an already configured Visual Studio Developer
rem Command Prompt. The shared environment helper also tries to locate Visual
rem Studio with vswhere and load VsDevCmd.bat for x64 builds when needed.

if /I "%~1"=="--help" goto :usage
if /I "%~1"=="-h" goto :usage
if "%~1"=="/?" goto :usage

set "STVR_TARGETS=SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,ImmersiveElf"
set "ROOT=%~dp0"

call "%ROOT%SetupSkyrimTogetherVRBuildEnv-Windows.bat"
set "STVR_ENV_RESULT=%ERRORLEVEL%"
if not "%STVR_ENV_RESULT%"=="0" exit /b %STVR_ENV_RESULT%

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

echo Building SkyrimTogetherVR DLL targets: %STVR_TARGETS%
echo Default output package: artifacts\SkyrimTogetherVR\releasedbg
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildSkyrimTogetherVR-Windows.ps1" -Targets "%STVR_TARGETS%" %*
set "STVR_RESULT=%ERRORLEVEL%"

popd >nul
exit /b %STVR_RESULT%

:usage
echo Usage:
echo   %~nx0 [BuildSkyrimTogetherVR-Windows.ps1 options]
echo.
echo Builds the DLL-producing SkyrimTogetherVR targets on Windows:
echo   SkyrimTogetherVRVrikBridge.dll
echo   SkyrimTogetherVRHiggsBridge.dll
echo   SkyrimTogetherVRPlanckBridge.dll
echo   SkyrimTogetherVRTickBridge.dll
echo   EarlyLoad.dll
echo.
echo Examples:
echo   %~nx0
echo   %~nx0 -PreflightOnly
echo   %~nx0 -Mode debug
echo   %~nx0 -Mode releasedbg -Xmake C:\Tools\xmake\xmake.exe
echo   %~nx0 -NoPackage
echo.
echo Run from the x64 Native Tools Command Prompt for Visual Studio, or let this
echo wrapper load VsDevCmd.bat through vswhere when Visual Studio is installed.
exit /b 0
