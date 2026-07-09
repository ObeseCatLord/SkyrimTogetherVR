@echo off
setlocal EnableExtensions

rem Compatibility wrapper for Windows builds where the requested artifact is
rem described as the "client DLL".
rem
rem Current SkyrimTogetherVR does not emit a standalone SkyrimTogetherVR.dll.
rem The client is linked into SkyrimTogetherVR.exe. The DLL-producing targets are:
rem - Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll
rem - EarlyLoad.dll

if /I "%~1"=="--help" goto :usage
if /I "%~1"=="-h" goto :usage
if "%~1"=="/?" goto :usage

echo Building SkyrimTogetherVR DLL-producing Windows targets.
echo Note: the VR client is launcher-linked as SkyrimTogetherVR.exe; this build does not emit a standalone SkyrimTogetherVR.dll.
echo.

call "%~dp0BuildSkyrimTogetherVR-DLL-Windows.bat" %*
exit /b %ERRORLEVEL%

:usage
echo Usage:
echo   %~nx0 [BuildSkyrimTogetherVR-Windows.ps1 options]
echo.
echo Compatibility wrapper for "client DLL" handoffs.
echo SkyrimTogetherVR does not currently emit a standalone SkyrimTogetherVR.dll.
echo This builds the real DLL-producing Windows targets:
echo   SkyrimTogetherVRVrikBridge.dll
echo   SkyrimTogetherVRHiggsBridge.dll
echo   SkyrimTogetherVRPlanckBridge.dll
echo   EarlyLoad.dll
echo.
echo Examples:
echo   %~nx0
echo   %~nx0 -PreflightOnly
echo   %~nx0 -Mode release
echo   %~nx0 -Xmake C:\Tools\xmake\xmake.exe
echo.
echo For the full launcher-linked client package, run:
echo   BuildSkyrimTogetherVR-Windows.bat
exit /b 0
