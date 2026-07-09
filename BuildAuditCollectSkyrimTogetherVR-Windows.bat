@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Builds/audits a SkyrimTogetherVR Windows package, collects build evidence, then audits that evidence zip.
rem This script does not install files and does not launch Skyrim.
rem Usage:
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat --avatar-sync
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat --gameplay
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat --dll-only
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
rem   BuildAuditCollectSkyrimTogetherVR-Windows.bat --avatar-sync --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites -- -Mode release

set "ROOT=%~dp0"
set "AVATAR_SYNC=0"
set "GAMEPLAY=0"
set "DLL_ONLY=0"
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
if /I "%~1"=="--dll-only" (
    set "DLL_ONLY=1"
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
if "%AVATAR_SYNC%"=="1" if "%DLL_ONLY%"=="1" (
    echo --avatar-sync and --dll-only cannot be combined.
    exit /b 1
)
if "%AVATAR_SYNC%"=="1" if "%GAMEPLAY%"=="1" (
    echo --avatar-sync and --gameplay cannot be combined.
    exit /b 1
)
if "%GAMEPLAY%"=="1" if "%DLL_ONLY%"=="1" (
    echo --gameplay and --dll-only cannot be combined.
    exit /b 1
)

if not defined SKYRIM_VR if defined SKYRIMVR_PATH set "SKYRIM_VR=%SKYRIMVR_PATH%"
if not defined SKYRIM_VR if defined STVR_SKYRIM_VR set "SKYRIM_VR=%STVR_SKYRIM_VR%"
if "%REQUIRE_PREREQUISITES%"=="1" if not defined SKYRIM_VR (
    echo --require-prerequisites requires --skyrim-vr or SKYRIMVR_PATH / STVR_SKYRIM_VR.
    echo Example: %~nx0 --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
    exit /b 1
)

set "MODE=default"
set "BUILD_WRAPPER=BuildAndAuditSkyrimTogetherVR-Windows.bat"
set "BUILD_MODE_ARG="
set "EVIDENCE_MODE_ARG="
set "EVIDENCE_REQUIRE_MODE_ARG=--require-default"
if "%AVATAR_SYNC%"=="1" (
    set "MODE=avatar-sync"
    set "BUILD_MODE_ARG=--avatar-sync"
    set "EVIDENCE_MODE_ARG=--avatar-sync"
    set "EVIDENCE_REQUIRE_MODE_ARG=--require-avatar-sync"
)
if "%GAMEPLAY%"=="1" (
    set "MODE=gameplay"
    set "BUILD_MODE_ARG=--gameplay"
    set "EVIDENCE_MODE_ARG=--gameplay"
    set "EVIDENCE_REQUIRE_MODE_ARG=--require-gameplay"
)
if "%DLL_ONLY%"=="1" (
    set "MODE=dll-only"
    set "BUILD_WRAPPER=BuildAndAuditSkyrimTogetherVR-DLL-Windows.bat"
    set "EVIDENCE_MODE_ARG=--dll-only"
    set "EVIDENCE_REQUIRE_MODE_ARG=--require-dll-only"
)

set "BUILD_SKYRIM_ARG="
set "EVIDENCE_SKYRIM_ARG="
if defined SKYRIM_VR (
    set "BUILD_SKYRIM_ARG=--skyrim-vr ^"%SKYRIM_VR%^""
    set "EVIDENCE_SKYRIM_ARG=--skyrim-vr ^"%SKYRIM_VR%^""
)

set "BUILD_PREREQ_ARG="
set "EVIDENCE_PREREQ_ARGS="
if "%REQUIRE_PREREQUISITES%"=="1" (
    set "BUILD_PREREQ_ARG=--require-prerequisites"
    set "EVIDENCE_PREREQ_ARGS=--require-installed-prerequisites --require-vrik --require-higgs --require-planck"
)
if "%COMPILE_PAPYRUS%"=="1" (
    set "BUILD_PREREQ_ARG=%BUILD_PREREQ_ARG% --compile-papyrus"
)
set "PACKAGE_ROOT=artifacts\SkyrimTogetherVR\%PACKAGE_MODE%"

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

echo Running Windows build and package audit for %MODE% package:
echo call "%ROOT%%BUILD_WRAPPER%" %BUILD_MODE_ARG% %BUILD_SKYRIM_ARG% %BUILD_PREREQ_ARG% %BUILD_ARGS%
call "%ROOT%%BUILD_WRAPPER%" %BUILD_MODE_ARG% %BUILD_SKYRIM_ARG% %BUILD_PREREQ_ARG% %BUILD_ARGS%
set "BUILD_RESULT=%ERRORLEVEL%"

if not "%BUILD_RESULT%"=="0" (
    echo.
    echo Build/package audit failed with exit code %BUILD_RESULT%.
    echo Collecting build evidence anyway.
)

echo.
echo Collecting build evidence for %MODE% package:
echo call "%ROOT%CollectSkyrimTogetherVRBuildEvidence-Windows.bat" --package "%PACKAGE_ROOT%" %EVIDENCE_MODE_ARG% %EVIDENCE_SKYRIM_ARG% %EVIDENCE_PREREQ_ARGS%
call "%ROOT%CollectSkyrimTogetherVRBuildEvidence-Windows.bat" --package "%PACKAGE_ROOT%" %EVIDENCE_MODE_ARG% %EVIDENCE_SKYRIM_ARG% %EVIDENCE_PREREQ_ARGS%
set "EVIDENCE_RESULT=%ERRORLEVEL%"

set "EVIDENCE_ZIP="
for /f "delims=" %%Z in ('dir /b /o-d "%ROOT%artifacts\SkyrimTogetherVR\build-evidence\SkyrimTogetherVR-build-evidence-%MODE%-*.zip" 2^>nul') do (
    if not defined EVIDENCE_ZIP set "EVIDENCE_ZIP=%ROOT%artifacts\SkyrimTogetherVR\build-evidence\%%Z"
)

if defined EVIDENCE_ZIP (
    set "EVIDENCE_AUDIT_ARGS=!EVIDENCE_REQUIRE_MODE_ARG!"
    if "!BUILD_RESULT!"=="0" if "!EVIDENCE_RESULT!"=="0" set "EVIDENCE_AUDIT_ARGS=--require-package !EVIDENCE_AUDIT_ARGS!"
    if not "!BUILD_RESULT!"=="0" set "EVIDENCE_AUDIT_ARGS=!EVIDENCE_AUDIT_ARGS! --allow-command-failures"
    if not "!EVIDENCE_RESULT!"=="0" set "EVIDENCE_AUDIT_ARGS=!EVIDENCE_AUDIT_ARGS! --allow-command-failures"

    echo.
    echo Auditing build evidence archive:
    echo call "%ROOT%AuditSkyrimTogetherVRBuildEvidence-Windows.bat" "%EVIDENCE_ZIP%" !EVIDENCE_AUDIT_ARGS!
    call "%ROOT%AuditSkyrimTogetherVRBuildEvidence-Windows.bat" "%EVIDENCE_ZIP%" !EVIDENCE_AUDIT_ARGS!
    set "EVIDENCE_AUDIT_RESULT=%ERRORLEVEL%"
) else (
    echo.
    echo No build evidence zip was found for mode %MODE%.
    set "EVIDENCE_AUDIT_RESULT=1"
)

popd >nul

if not "%BUILD_RESULT%"=="0" exit /b %BUILD_RESULT%
if not "%EVIDENCE_RESULT%"=="0" exit /b %EVIDENCE_RESULT%
exit /b %EVIDENCE_AUDIT_RESULT%
