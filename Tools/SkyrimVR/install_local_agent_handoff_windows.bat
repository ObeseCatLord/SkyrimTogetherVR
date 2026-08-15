@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0INSTALL-SECOND-CLIENT-WINDOWS.ps1" %*
exit /b %ERRORLEVEL%
