@echo off
setlocal EnableExtensions

rem Builds the VRIK/HIGGS remote-avatar validation package:
rem - SkyrimTogetherVRAvatarSync.exe
rem - Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll
rem - EarlyLoad.dll
rem - TPProcess.exe
rem This is the explicit two-client avatar-sync build for VRIK/HIGGS validation; the default SkyrimTogetherVR.exe stays connection-only.

call "%~dp0SetupSkyrimTogetherVRBuildEnv-Windows.bat"
set "STVR_ENV_RESULT=%ERRORLEVEL%"
if not "%STVR_ENV_RESULT%"=="0" exit /b %STVR_ENV_RESULT%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildSkyrimTogetherVR-Windows.ps1" -Targets SkyrimTogetherVRClientAvatarSync,SkyrimVRImmersiveLauncherAvatarSync,SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,ImmersiveElf,TPProcess %*
exit /b %ERRORLEVEL%
