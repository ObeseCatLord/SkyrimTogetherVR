@echo off
setlocal

rem Compatibility alias for the singular DLL-producing Windows wrapper.
call "%~dp0BuildSkyrimTogetherVR-DLL-Windows.bat" %*
exit /b %ERRORLEVEL%
