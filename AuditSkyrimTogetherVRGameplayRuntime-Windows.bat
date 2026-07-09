@echo off
setlocal

rem Runs the no-launch full gameplay runtime audit after a user-run VR test.
rem Extra arguments are forwarded to AuditSkyrimTogetherVRRuntime-Windows.bat.

set "ROOT=%~dp0"
set "RUNTIME_AUDIT=%ROOT%AuditSkyrimTogetherVRRuntime-Windows.bat"

if not exist "%RUNTIME_AUDIT%" (
    echo Could not find "%RUNTIME_AUDIT%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

call "%RUNTIME_AUDIT%" --gameplay %*
exit /b %ERRORLEVEL%
