@echo off
setlocal

rem Collects no-launch SkyrimTogetherVR runtime evidence after a user-run VR test.
rem The packaged folder is used as the default --game-path; pass --game-path to override it.

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\collect_runtime_evidence.py"

if not exist "%HELPER%" (
    echo Could not find "%HELPER%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%HELPER%" --game-path "%ROOT%" %*
    exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if not errorlevel 1 (
    python "%HELPER%" --game-path "%ROOT%" %*
    exit /b %ERRORLEVEL%
)

where python3 >nul 2>nul
if not errorlevel 1 (
    python3 "%HELPER%" --game-path "%ROOT%" %*
    exit /b %ERRORLEVEL%
)

echo Python 3 was not found. Install Python 3 or run:
echo   python "%HELPER%" --game-path "%ROOT%" %*
exit /b 1
