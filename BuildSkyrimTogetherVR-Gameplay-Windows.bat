@echo off
setlocal EnableExtensions

rem Builds the full gameplay SkyrimTogetherVR package:
rem - SkyrimTogetherVRGameplay.exe
rem - Data\SKSE\Plugins\SkyrimTogetherVRVrikBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRHiggsBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRPlanckBridge.dll
rem - Data\SKSE\Plugins\SkyrimTogetherVRTickBridge.dll
rem - EarlyLoad.dll
rem - TPProcess.exe
rem This target leaves unvalidated inline/gameplay hooks disabled, but enables
rem the normal Skyrim Together client services plus VR pose/equipment/gameplay
rem relay observation services and remote-avatar actor targets.

call "%~dp0SetupSkyrimTogetherVRBuildEnv-Windows.bat"
set "STVR_ENV_RESULT=%ERRORLEVEL%"
if not "%STVR_ENV_RESULT%"=="0" exit /b %STVR_ENV_RESULT%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildSkyrimTogetherVR-Windows.ps1" -Targets SkyrimTogetherVRGameplayClient,SkyrimVRImmersiveLauncherGameplay,SkyrimTogetherVRVrikBridge,SkyrimTogetherVRHiggsBridge,SkyrimTogetherVRPlanckBridge,SkyrimTogetherVRTickBridge,ImmersiveElf,TPProcess %*
exit /b %ERRORLEVEL%
