@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Verifies already-built SkyrimTogetherVR Windows package snapshots.
rem This script does not build targets, install files, or launch Skyrim.
rem Run it after PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all.
rem Usage:
rem   VerifySkyrimTogetherVRWindowsPackages-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"
rem   VerifySkyrimTogetherVRWindowsPackages-Windows.bat --include-fus --planck-archive "C:\Downloads\PLANCK.zip" --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR"

set "ROOT=%~dp0"
set "SKYRIM_VR="
set "INCLUDE_FUS_ARG="
set "PLANCK_ARCHIVE_ARGS="
set "VERIFY_DLL_ONLY=1"
set "RUN_INSTALL_DRY_RUNS=1"

:parse_args
if "%~1"=="" goto parsed_args
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if "%~1"=="/?" goto usage
if /I "%~1"=="--include-fus" (
    set "INCLUDE_FUS_ARG=--include-fus"
    shift
    goto parse_args
)
if /I "%~1"=="--planck-archive" (
    if "%~2"=="" (
        echo --planck-archive requires a path argument.
        exit /b 1
    )
    set "PLANCK_ARCHIVE_ARGS=--planck-archive ^"%~2^""
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--skyrim-vr" (
    if "%~2"=="" (
        echo --skyrim-vr requires a path argument.
        exit /b 1
    )
    set "SKYRIM_VR=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--skip-dll-only" (
    set "VERIFY_DLL_ONLY=0"
    shift
    goto parse_args
)
if /I "%~1"=="--skip-install-dry-runs" (
    set "RUN_INSTALL_DRY_RUNS=0"
    shift
    goto parse_args
)
echo Unknown argument: %~1
goto usage_error

:parsed_args
if not defined SKYRIM_VR if defined SKYRIMVR_PATH set "SKYRIM_VR=%SKYRIMVR_PATH%"
if not defined SKYRIM_VR if defined STVR_SKYRIM_VR set "SKYRIM_VR=%STVR_SKYRIM_VR%"
if not defined SKYRIM_VR (
    echo No Skyrim VR path was provided.
    echo Pass --skyrim-vr "C:\Path\To\SkyrimVR" or set SKYRIMVR_PATH / STVR_SKYRIM_VR.
    exit /b 1
)

set "DEFAULT_PACKAGE=artifacts\SkyrimTogetherVR\packages\default"
set "AVATAR_SYNC_PACKAGE=artifacts\SkyrimTogetherVR\packages\avatar-sync"
set "GAMEPLAY_PACKAGE=artifacts\SkyrimTogetherVR\packages\gameplay"
set "DLL_ONLY_PACKAGE=artifacts\SkyrimTogetherVR\packages\dll-only"
set "STVR_RESULT=0"

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
    echo Python 3 was not found. Install Python 3 or run audit_built_package.py manually.
    exit /b 1
)

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

echo Verifying SkyrimTogetherVR package snapshots without building, installing, or launching Skyrim.
echo Skyrim VR path: "%SKYRIM_VR%"
echo Default package snapshot: %DEFAULT_PACKAGE%
echo Avatar-sync package snapshot: %AVATAR_SYNC_PACKAGE%
echo Gameplay package snapshot: %GAMEPLAY_PACKAGE%
echo DLL-only package snapshot: %DLL_ONLY_PACKAGE%

echo.
echo [Default readiness]
call "%ROOT%AuditSkyrimTogetherVRReadiness-Windows.bat" --package "%DEFAULT_PACKAGE%" --skyrim-vr "%SKYRIM_VR%" --require-built-package %INCLUDE_FUS_ARG% %PLANCK_ARCHIVE_ARGS%
if errorlevel 1 (
    echo Default readiness failed with exit code !ERRORLEVEL!.
    if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
)

echo.
echo [Avatar-sync readiness]
call "%ROOT%AuditSkyrimTogetherVRReadiness-Windows.bat" --avatar-sync --package "%AVATAR_SYNC_PACKAGE%" --skyrim-vr "%SKYRIM_VR%" --require-built-package %INCLUDE_FUS_ARG% %PLANCK_ARCHIVE_ARGS%
if errorlevel 1 (
    echo Avatar-sync readiness failed with exit code !ERRORLEVEL!.
    if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
)

echo.
echo [Gameplay readiness]
call "%ROOT%AuditSkyrimTogetherVRReadiness-Windows.bat" --gameplay --package "%GAMEPLAY_PACKAGE%" --skyrim-vr "%SKYRIM_VR%" --require-built-package %INCLUDE_FUS_ARG% %PLANCK_ARCHIVE_ARGS%
if errorlevel 1 (
    echo Gameplay readiness failed with exit code !ERRORLEVEL!.
    if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
)

if "%VERIFY_DLL_ONLY%"=="1" (
    echo.
    echo [DLL-only package audit]
    echo %PYTHON_CMD% Tools\SkyrimVR\audit_built_package.py --package "%DLL_ONLY_PACKAGE%" --dll-only --skyrim-vr "%SKYRIM_VR%" --require-installed-prerequisites --require-vrik --require-higgs --require-planck
    %PYTHON_CMD% Tools\SkyrimVR\audit_built_package.py --package "%DLL_ONLY_PACKAGE%" --dll-only --skyrim-vr "%SKYRIM_VR%" --require-installed-prerequisites --require-vrik --require-higgs --require-planck
    if errorlevel 1 (
        echo DLL-only package audit failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )
)

if "%RUN_INSTALL_DRY_RUNS%"=="1" (
    echo.
    echo [Default package-only install preflight]
    call "%ROOT%InstallSkyrimTogetherVR-Windows.bat" --package "%DEFAULT_PACKAGE%" --package-only --skyrim-vr "%SKYRIM_VR%"
    if errorlevel 1 (
        echo Default package-only install preflight failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )

    echo.
    echo [Default strict install dry-run]
    call "%ROOT%InstallSkyrimTogetherVR-Windows.bat" --package "%DEFAULT_PACKAGE%" --skyrim-vr "%SKYRIM_VR%"
    if errorlevel 1 (
        echo Default strict install dry-run failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )

    echo.
    echo [Avatar-sync package-only install preflight]
    call "%ROOT%InstallSkyrimTogetherVR-Windows.bat" --avatar-sync --package "%AVATAR_SYNC_PACKAGE%" --package-only --skyrim-vr "%SKYRIM_VR%"
    if errorlevel 1 (
        echo Avatar-sync package-only install preflight failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )

    echo.
    echo [Avatar-sync strict install dry-run]
    call "%ROOT%InstallSkyrimTogetherVR-Windows.bat" --avatar-sync --package "%AVATAR_SYNC_PACKAGE%" --skyrim-vr "%SKYRIM_VR%"
    if errorlevel 1 (
        echo Avatar-sync strict install dry-run failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )

    echo.
    echo [Gameplay package-only install preflight]
    call "%ROOT%InstallSkyrimTogetherVR-Windows.bat" --gameplay --package "%GAMEPLAY_PACKAGE%" --package-only --skyrim-vr "%SKYRIM_VR%"
    if errorlevel 1 (
        echo Gameplay package-only install preflight failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )

    echo.
    echo [Gameplay strict install dry-run]
    call "%ROOT%InstallSkyrimTogetherVR-Windows.bat" --gameplay --package "%GAMEPLAY_PACKAGE%" --skyrim-vr "%SKYRIM_VR%"
    if errorlevel 1 (
        echo Gameplay strict install dry-run failed with exit code !ERRORLEVEL!.
        if "!STVR_RESULT!"=="0" set "STVR_RESULT=!ERRORLEVEL!"
    )
)

popd >nul

if "%STVR_RESULT%"=="0" (
    echo.
    echo SkyrimTogetherVR package snapshot verification passed.
) else (
    echo.
    echo SkyrimTogetherVR package snapshot verification failed with first failing exit code %STVR_RESULT%.
)
exit /b %STVR_RESULT%

:usage
echo Usage:
echo   %~nx0 --skyrim-vr "C:\Path\To\SkyrimVR" [--include-fus] [--planck-archive "C:\Downloads\PLANCK.zip"] [--skip-dll-only] [--skip-install-dry-runs]
echo.
echo Verifies package snapshots produced by the Windows packager:
echo   artifacts\SkyrimTogetherVR\packages\default
echo   artifacts\SkyrimTogetherVR\packages\avatar-sync
echo   artifacts\SkyrimTogetherVR\packages\gameplay
echo   artifacts\SkyrimTogetherVR\packages\dll-only
echo.
echo This script does not build targets, install files, or launch Skyrim.
exit /b 0

:usage_error
call :usage
exit /b 1
