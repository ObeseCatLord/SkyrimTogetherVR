@echo off
setlocal EnableExtensions

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\SkyrimVR\build_planck_windows.ps1" %*
set "PLANCK_EXIT=%ERRORLEVEL%"
endlocal & exit /b %PLANCK_EXIT%
