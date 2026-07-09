@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Builds a SkyrimTogetherVR Windows package, then audits the produced package.
rem This script does not install files and does not launch Skyrim.
rem Usage:
rem   BuildAndAuditSkyrimTogetherVR-Windows.bat
rem   BuildAndAuditSkyrimTogetherVR-Windows.bat --avatar-sync
rem   BuildAndAuditSkyrimTogetherVR-Windows.bat --gameplay
rem   BuildAndAuditSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
rem   BuildAndAuditSkyrimTogetherVR-Windows.bat --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
rem   BuildAndAuditSkyrimTogetherVR-Windows.bat --avatar-sync -- -Mode release

set "ROOT=%~dp0"
set "AVATAR_SYNC=0"
set "GAMEPLAY=0"
set "REQUIRE_PREREQUISITES=0"
set "COMPILE_PAPYRUS=0"
set "PACKAGE_MODE=releasedbg"
set "SKYRIM_VR="
set "BUILD_ARGS="

:parse_args
if "%~1"=="" goto parsed_args
if /I "%~1"=="--avatar-sync" (
    set "AVATAR_SYNC=1"
    shift
    goto parse_args
)
if /I "%~1"=="--gameplay" (
    set "GAMEPLAY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--require-prerequisites" (
    set "REQUIRE_PREREQUISITES=1"
    shift
    goto parse_args
)
if /I "%~1"=="--compile-papyrus" (
    set "COMPILE_PAPYRUS=1"
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
if "%~1"=="--" (
    shift
    goto collect_build_args
)
goto collect_build_args

:collect_build_args
if "%~1"=="" goto parsed_args
if /I "%~1"=="-Mode" if not "%~2"=="" set "PACKAGE_MODE=%~2"
set "BUILD_ARGS=!BUILD_ARGS! ^"%~1^""
shift
goto collect_build_args

:parsed_args
if "%AVATAR_SYNC%"=="1" if "%GAMEPLAY%"=="1" (
    echo --avatar-sync and --gameplay cannot be combined.
    exit /b 1
)
if not defined SKYRIM_VR if defined SKYRIMVR_PATH set "SKYRIM_VR=%SKYRIMVR_PATH%"
if not defined SKYRIM_VR if defined STVR_SKYRIM_VR set "SKYRIM_VR=%STVR_SKYRIM_VR%"
if "%REQUIRE_PREREQUISITES%"=="1" if not defined SKYRIM_VR (
    echo --require-prerequisites requires --skyrim-vr or SKYRIMVR_PATH / STVR_SKYRIM_VR.
    echo Example: %~nx0 --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
    exit /b 1
)
if not "%REQUIRE_PREREQUISITES%"=="1" (
    echo Warning: target Skyrim VR prerequisite checks are disabled for this build audit.
    echo Pass --skyrim-vr "C:\Path\To\SkyrimVR" --require-prerequisites before treating the package as install-ready.
)
if "%COMPILE_PAPYRUS%"=="1" (
    set "BUILD_ARGS=!BUILD_ARGS! -CompilePapyrus"
)
set "PACKAGE_ROOT=artifacts\SkyrimTogetherVR\%PACKAGE_MODE%"

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

if "%GAMEPLAY%"=="1" (
    call "%ROOT%BuildSkyrimTogetherVR-Gameplay-Windows.bat" %BUILD_ARGS%
) else if "%AVATAR_SYNC%"=="1" (
    call "%ROOT%BuildSkyrimTogetherVR-AvatarSync-Windows.bat" %BUILD_ARGS%
) else (
    call "%ROOT%BuildSkyrimTogetherVR-Windows.bat" %BUILD_ARGS%
)
if errorlevel 1 (
    set "STVR_RESULT=%ERRORLEVEL%"
    popd >nul
    exit /b %STVR_RESULT%
)

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
    echo Python was not found. Install Python 3 or run audit_built_package.py manually after the build.
    popd >nul
    exit /b 1
)

set "AVATAR_SYNC_AUDIT_ARG="
if "%AVATAR_SYNC%"=="1" set "AVATAR_SYNC_AUDIT_ARG=--avatar-sync"
set "GAMEPLAY_AUDIT_ARG="
if "%GAMEPLAY%"=="1" set "GAMEPLAY_AUDIT_ARG=--gameplay"
set "PREREQUISITE_AUDIT_ARGS="
if "%REQUIRE_PREREQUISITES%"=="1" set "PREREQUISITE_AUDIT_ARGS=--require-installed-prerequisites --require-vrik --require-higgs --require-planck"

echo.
echo Running package audit:
if defined SKYRIM_VR (
    echo %PYTHON_CMD% Tools\SkyrimVR\audit_built_package.py --package "%PACKAGE_ROOT%" %AVATAR_SYNC_AUDIT_ARG% %GAMEPLAY_AUDIT_ARG% --skyrim-vr "%SKYRIM_VR%" %PREREQUISITE_AUDIT_ARGS%
    %PYTHON_CMD% Tools\SkyrimVR\audit_built_package.py --package "%PACKAGE_ROOT%" %AVATAR_SYNC_AUDIT_ARG% %GAMEPLAY_AUDIT_ARG% --skyrim-vr "%SKYRIM_VR%" %PREREQUISITE_AUDIT_ARGS%
) else (
    echo %PYTHON_CMD% Tools\SkyrimVR\audit_built_package.py --package "%PACKAGE_ROOT%" %AVATAR_SYNC_AUDIT_ARG% %GAMEPLAY_AUDIT_ARG% %PREREQUISITE_AUDIT_ARGS%
    %PYTHON_CMD% Tools\SkyrimVR\audit_built_package.py --package "%PACKAGE_ROOT%" %AVATAR_SYNC_AUDIT_ARG% %GAMEPLAY_AUDIT_ARG% %PREREQUISITE_AUDIT_ARGS%
)
set "STVR_RESULT=%ERRORLEVEL%"

popd >nul
exit /b %STVR_RESULT%
