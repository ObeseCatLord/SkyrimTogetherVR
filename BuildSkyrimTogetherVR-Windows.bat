@echo off
setlocal EnableExtensions

call "%~dp0SetupSkyrimTogetherVRBuildEnv-Windows.bat"
set "STVR_ENV_RESULT=%ERRORLEVEL%"
if not "%STVR_ENV_RESULT%"=="0" exit /b %STVR_ENV_RESULT%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildSkyrimTogetherVR-Windows.ps1" %*
exit /b %ERRORLEVEL%
