@echo off
rem Shared Windows build environment bootstrap for SkyrimTogetherVR.
rem Intentionally does not use setlocal: VsDevCmd.bat environment changes must
rem remain visible to the caller that invokes xmake/PowerShell afterward.

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo powershell.exe was not found in PATH.
    echo Install Windows PowerShell or run from a Visual Studio Developer shell with PowerShell available.
    exit /b 1
)

where cl.exe >nul 2>nul
if not errorlevel 1 exit /b 0

set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" set "VSWHERE_DIR=%ProgramFiles%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" (
    echo MSVC cl.exe was not found in PATH and vswhere.exe was not found.
    echo If xmake cannot find MSVC automatically, rerun this from the x64 Native Tools Command Prompt for Visual Studio.
    exit /b 0
)

set "VSROOT="
set "VSWHERE_RESULT=%TEMP%\stvr-vswhere-%RANDOM%-%RANDOM%.txt"
"%VSWHERE_DIR%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSWHERE_RESULT%"
if errorlevel 1 (
    del /q "%VSWHERE_RESULT%" >nul 2>nul
    echo vswhere.exe failed while locating the Visual Studio C++ toolset.
    exit /b 1
)
set /p VSROOT=<"%VSWHERE_RESULT%"
del /q "%VSWHERE_RESULT%" >nul 2>nul
if not defined VSROOT (
    echo MSVC cl.exe was not found in PATH and vswhere did not find a Visual C++ toolset.
    echo Install Visual Studio Build Tools with the Desktop development with C++ workload.
    exit /b 1
)

set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
    echo Expected Visual Studio developer command script was not found:
    echo   %VSDEVCMD%
    exit /b 1
)

echo Loading Visual Studio x64 build environment:
echo   %VSDEVCMD%
call "%VSDEVCMD%" -arch=x64 -host_arch=x64
if errorlevel 1 (
    echo VsDevCmd.bat failed.
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo VsDevCmd.bat completed, but cl.exe is still not visible in PATH.
    exit /b 1
)

exit /b 0
