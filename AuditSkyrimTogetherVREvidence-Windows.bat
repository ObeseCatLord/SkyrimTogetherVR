@echo off
setlocal

rem Audits a SkyrimTogetherVR runtime evidence zip without needing the game folder.

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\audit_runtime_evidence_zip.py"

if not exist "%HELPER%" (
    echo Could not find "%HELPER%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%HELPER%" %*
    exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if not errorlevel 1 (
    python "%HELPER%" %*
    exit /b %ERRORLEVEL%
)

where python3 >nul 2>nul
if not errorlevel 1 (
    python3 "%HELPER%" %*
    exit /b %ERRORLEVEL%
)

echo Python 3 was not found. Install Python 3 or run:
echo   python "%HELPER%" evidence.zip
exit /b 1
