@echo off
setlocal

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\vr_handoff.py"
set "GAME_PATH=%ROOT%"

if /I "%~1"=="--game-path" (
    set "GAME_PATH=%~2"
    shift
    shift
)

if not exist "%HELPER%" (
    echo Could not find "%HELPER%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%HELPER%" --game-path "%GAME_PATH%" serve --open-browser %*
    exit /b
)

where python >nul 2>nul
if not errorlevel 1 (
    python "%HELPER%" --game-path "%GAME_PATH%" serve --open-browser %*
    exit /b
)

echo Python 3 was not found. Install Python 3 or run:
echo   python "%HELPER%" --game-path "%GAME_PATH%" serve --open-browser
exit /b 1
