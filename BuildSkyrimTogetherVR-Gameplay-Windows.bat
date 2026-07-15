@echo off
setlocal EnableExtensions

rem Builds the staged gameplay SkyrimTogetherVR package:
rem - SkyrimTogetherVRGameplay.exe
rem - Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRGameplayBridge.dll
rem - EarlyLoad.dll
rem - TPProcess.exe
rem This target leaves unvalidated inline/gameplay hooks and legacy desktop
rem mutation services disabled. It enables observation relays plus the
rem CommonLib-owned same-cell remote-avatar root synchronization slice.

call "%~dp0SetupSkyrimTogetherVRBuildEnv-Windows.bat"
set "STVR_ENV_RESULT=%ERRORLEVEL%"
if not "%STVR_ENV_RESULT%"=="0" exit /b %STVR_ENV_RESULT%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildSkyrimTogetherVR-Windows.ps1" -Targets SkyrimTogetherVRGameplayClient,SkyrimVRImmersiveLauncherGameplay,SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,SkyrimTogetherVRGameplayBridge,ImmersiveElf,TPProcess %*
exit /b %ERRORLEVEL%
