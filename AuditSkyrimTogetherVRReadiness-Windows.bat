@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Runs the no-launch SkyrimTogetherVR source/package readiness gate on Windows.
rem This script does not build, install files, or launch Skyrim.
rem Usage:
rem   AuditSkyrimTogetherVRReadiness-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
rem   AuditSkyrimTogetherVRReadiness-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
rem   AuditSkyrimTogetherVRReadiness-Windows.bat --avatar-sync --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
rem   AuditSkyrimTogetherVRReadiness-Windows.bat --gameplay --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-built-package
rem   AuditSkyrimTogetherVRReadiness-Windows.bat --include-fus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
rem   AuditSkyrimTogetherVRReadiness-Windows.bat --planck-archive "C:\Downloads\PLANCK.zip" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"

set "ROOT=%~dp0"
set "HELPER=%ROOT%Tools\SkyrimVR\audit_vr_readiness.py"
set "HAS_SKYRIM_VR_ARG=0"
set "SKIP_FUS_ARG=--skip-fus"
set "EXTRA_ARGS="

:parse_args
if "%~1"=="" goto parsed_args
if /I "%~1"=="--include-fus" (
    set "SKIP_FUS_ARG="
    shift
    goto parse_args
)
if /I "%~1"=="--skyrim-vr" (
    if "%~2"=="" (
        echo --skyrim-vr requires a path argument.
        exit /b 1
    )
    set "HAS_SKYRIM_VR_ARG=1"
    set "EXTRA_ARGS=!EXTRA_ARGS! ^"%~1^" ^"%~2^""
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--planck-archive" (
    if "%~2"=="" (
        echo --planck-archive requires a path argument.
        exit /b 1
    )
    set "EXTRA_ARGS=!EXTRA_ARGS! ^"%~1^" ^"%~2^""
    shift
    shift
    goto parse_args
)
set "EXTRA_ARGS=!EXTRA_ARGS! ^"%~1^""
shift
goto parse_args

:parsed_args
if not exist "%HELPER%" (
    echo Could not find "%HELPER%".
    echo Run this from the SkyrimTogetherVR repository root.
    exit /b 1
)

set "SKYRIM_VR_FROM_ENV="
if defined SKYRIMVR_PATH set "SKYRIM_VR_FROM_ENV=%SKYRIMVR_PATH%"
if not defined SKYRIM_VR_FROM_ENV if defined STVR_SKYRIM_VR set "SKYRIM_VR_FROM_ENV=%STVR_SKYRIM_VR%"

set "PYTHON_CMD="
where py >nul 2>nul
if not errorlevel 1 set "PYTHON_CMD=py -3"
if not defined PYTHON_CMD (
    where python >nul 2>nul
    if not errorlevel 1 set "PYTHON_CMD=python"
)
if not defined PYTHON_CMD (
    where python3 >nul 2>nul
    if not errorlevel 1 set "PYTHON_CMD=python3"
)
if not defined PYTHON_CMD (
    echo Python 3 was not found. Install Python 3 or run:
    echo   python "%HELPER%" --skyrim-vr "C:\Path\To\SkyrimVR" --skip-fus
    exit /b 1
)

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

if "%HAS_SKYRIM_VR_ARG%"=="1" (
    echo %PYTHON_CMD% "%HELPER%"%EXTRA_ARGS% %SKIP_FUS_ARG%
    %PYTHON_CMD% "%HELPER%"%EXTRA_ARGS% %SKIP_FUS_ARG%
) else if defined SKYRIM_VR_FROM_ENV (
    echo %PYTHON_CMD% "%HELPER%" --skyrim-vr "%SKYRIM_VR_FROM_ENV%"%EXTRA_ARGS% %SKIP_FUS_ARG%
    %PYTHON_CMD% "%HELPER%" --skyrim-vr "%SKYRIM_VR_FROM_ENV%"%EXTRA_ARGS% %SKIP_FUS_ARG%
) else (
    echo No Skyrim VR path was provided.
    echo Pass --skyrim-vr "C:\Path\To\SkyrimVR" or set SKYRIMVR_PATH / STVR_SKYRIM_VR.
    popd >nul
    exit /b 1
)
set "STVR_RESULT=%ERRORLEVEL%"

popd >nul
exit /b %STVR_RESULT%
