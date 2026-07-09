@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Top-level Windows handoff driver for SkyrimTogetherVR.
rem This script does not install files and does not launch Skyrim.
rem Default behavior builds/audits/collects evidence for the default package and the avatar-sync validation package.
rem Usage:
rem   PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat
rem   PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
rem   PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --all --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
rem   PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --preflight-only
rem   PrepareSkyrimTogetherVRWindowsHandoff-Windows.bat --avatar-sync-only -- -Mode release

set "ROOT=%~dp0"
set "DO_DEFAULT=1"
set "DO_AVATAR_SYNC=1"
set "DO_GAMEPLAY=0"
set "DO_DLL_ONLY=0"
set "PREFLIGHT_ONLY=0"
set "REQUIRE_PREREQUISITES=0"
set "COMPILE_PAPYRUS=0"
set "SKYRIM_VR="
set "BUILD_ARGS="

:parse_args
if "%~1"=="" goto parsed_args
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if "%~1"=="/?" goto usage
if /I "%~1"=="--all" (
    set "DO_DEFAULT=1"
    set "DO_AVATAR_SYNC=1"
    set "DO_GAMEPLAY=1"
    set "DO_DLL_ONLY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--default-only" (
    set "DO_DEFAULT=1"
    set "DO_AVATAR_SYNC=0"
    set "DO_GAMEPLAY=0"
    set "DO_DLL_ONLY=0"
    shift
    goto parse_args
)
if /I "%~1"=="--avatar-sync-only" (
    set "DO_DEFAULT=0"
    set "DO_AVATAR_SYNC=1"
    set "DO_GAMEPLAY=0"
    set "DO_DLL_ONLY=0"
    shift
    goto parse_args
)
if /I "%~1"=="--gameplay-only" (
    set "DO_DEFAULT=0"
    set "DO_AVATAR_SYNC=0"
    set "DO_GAMEPLAY=1"
    set "DO_DLL_ONLY=0"
    shift
    goto parse_args
)
if /I "%~1"=="--dll-only" (
    set "DO_DEFAULT=0"
    set "DO_AVATAR_SYNC=0"
    set "DO_GAMEPLAY=0"
    set "DO_DLL_ONLY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--include-gameplay" (
    set "DO_GAMEPLAY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--include-dll-only" (
    set "DO_DLL_ONLY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--preflight-only" (
    set "PREFLIGHT_ONLY=1"
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
set "BUILD_ARGS=!BUILD_ARGS! ^"%~1^""
shift
goto collect_build_args

:parsed_args
if "%DO_DEFAULT%"=="0" if "%DO_AVATAR_SYNC%"=="0" if "%DO_GAMEPLAY%"=="0" if "%DO_DLL_ONLY%"=="0" (
    echo No package mode selected.
    exit /b 1
)

if not defined SKYRIM_VR if defined SKYRIMVR_PATH set "SKYRIM_VR=%SKYRIMVR_PATH%"
if not defined SKYRIM_VR if defined STVR_SKYRIM_VR set "SKYRIM_VR=%STVR_SKYRIM_VR%"
if "%REQUIRE_PREREQUISITES%"=="1" if not defined SKYRIM_VR (
    echo --require-prerequisites requires --skyrim-vr or SKYRIMVR_PATH / STVR_SKYRIM_VR.
    echo Example: %~nx0 --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
    exit /b 1
)

pushd "%ROOT%" >nul
if errorlevel 1 exit /b 1

set "COMMON_ARGS="
if defined SKYRIM_VR set "COMMON_ARGS=--skyrim-vr ^"%SKYRIM_VR%^""
if "%REQUIRE_PREREQUISITES%"=="1" set "COMMON_ARGS=%COMMON_ARGS% --require-prerequisites"
set "PREFLIGHT_BUILD_ARGS=%BUILD_ARGS%"
if "%COMPILE_PAPYRUS%"=="1" (
    set "COMMON_ARGS=%COMMON_ARGS% --compile-papyrus"
    set "PREFLIGHT_BUILD_ARGS=%PREFLIGHT_BUILD_ARGS% -CompilePapyrus"
)

set "FINAL_RESULT=0"

if "%PREFLIGHT_ONLY%"=="1" (
    echo Running SkyrimTogetherVR Windows handoff preflight only; no targets will be built.
    if "%DO_DEFAULT%"=="1" call :run_preflight default "%ROOT%BuildSkyrimTogetherVR-Windows.bat"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
    if "%DO_AVATAR_SYNC%"=="1" call :run_preflight avatar-sync "%ROOT%BuildSkyrimTogetherVR-AvatarSync-Windows.bat"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
    if "%DO_GAMEPLAY%"=="1" call :run_preflight gameplay "%ROOT%BuildSkyrimTogetherVR-Gameplay-Windows.bat"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
    if "%DO_DLL_ONLY%"=="1" call :run_preflight dll-only "%ROOT%BuildSkyrimTogetherVR-DLL-Windows.bat"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
) else (
    echo Running SkyrimTogetherVR Windows build/audit/evidence handoff.
    echo Selected modes: default=%DO_DEFAULT% avatar-sync=%DO_AVATAR_SYNC% gameplay=%DO_GAMEPLAY% dll-only=%DO_DLL_ONLY%
    echo.
    if "%DO_DEFAULT%"=="1" call :run_handoff default ""
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
    if "%DO_AVATAR_SYNC%"=="1" call :run_handoff avatar-sync "--avatar-sync"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
    if "%DO_GAMEPLAY%"=="1" call :run_handoff gameplay "--gameplay"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
    if "%DO_DLL_ONLY%"=="1" call :run_handoff dll-only "--dll-only"
    if "!ERRORLEVEL!" NEQ "0" if "!FINAL_RESULT!"=="0" set "FINAL_RESULT=!ERRORLEVEL!"
)

popd >nul

if "%FINAL_RESULT%"=="0" (
    echo.
    echo SkyrimTogetherVR Windows handoff completed.
) else (
    echo.
    echo SkyrimTogetherVR Windows handoff completed with failure code %FINAL_RESULT%.
)
exit /b %FINAL_RESULT%

:run_preflight
set "MODE_NAME=%~1"
set "WRAPPER=%~2"
echo.
echo Preflight: %MODE_NAME%
echo call "%WRAPPER%" -PreflightOnly %PREFLIGHT_BUILD_ARGS%
call "%WRAPPER%" -PreflightOnly %PREFLIGHT_BUILD_ARGS%
exit /b %ERRORLEVEL%

:run_handoff
set "MODE_NAME=%~1"
set "MODE_ARG=%~2"
echo.
echo Build/audit/evidence: %MODE_NAME%
echo call "%ROOT%BuildAuditCollectSkyrimTogetherVR-Windows.bat" %MODE_ARG% %COMMON_ARGS% %BUILD_ARGS%
call "%ROOT%BuildAuditCollectSkyrimTogetherVR-Windows.bat" %MODE_ARG% %COMMON_ARGS% %BUILD_ARGS%
exit /b %ERRORLEVEL%

:usage
echo Usage:
echo   %~nx0 [mode flags] [handoff flags] [-- BuildSkyrimTogetherVR-Windows.ps1 options]
echo.
echo Default mode selection:
echo   Builds/audits/collects evidence for default and avatar-sync packages.
echo.
echo Mode flags:
echo   --all                Run default, avatar-sync, gameplay, and DLL-only handoffs.
echo   --default-only       Run only the default package handoff.
echo   --avatar-sync-only   Run only the two-client VRIK/HIGGS avatar-sync handoff.
echo   --gameplay-only      Run only the full gameplay package handoff.
echo   --dll-only           Run only the DLL-producing partial package handoff.
echo   --include-gameplay   Add gameplay to the default+avatar-sync selection.
echo   --include-dll-only   Add DLL-only to the default+avatar-sync selection.
echo.
echo Handoff flags:
echo   --preflight-only          Configure/check wrappers without building targets.
echo   --skyrim-vr PATH          Target Skyrim VR root for prerequisite-aware audits.
echo   --require-prerequisites   Require SKSEVR, VR Address Library, VRIK, HIGGS, and PLANCK.
echo   --compile-papyrus         Compile VR Papyrus sources before packaging.
echo.
echo Examples:
echo   %~nx0
echo   %~nx0 --all --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
echo   %~nx0 --all --compile-papyrus --skyrim-vr "C:\SteamLibrary\steamapps\common\SkyrimVR" --require-prerequisites
echo   %~nx0 --preflight-only -- -Xmake C:\Tools\xmake\xmake.exe
echo   %~nx0 --avatar-sync-only -- -Mode release
exit /b 0
