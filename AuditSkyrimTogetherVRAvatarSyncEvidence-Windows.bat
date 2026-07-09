@echo off
setlocal

rem Audits a collected two-client VRIK/HIGGS avatar-sync runtime evidence zip.
rem Pass the evidence zip path first. Extra arguments are forwarded to the evidence audit.

set "ROOT=%~dp0"
set "EVIDENCE_AUDIT=%ROOT%AuditSkyrimTogetherVREvidence-Windows.bat"

if not exist "%EVIDENCE_AUDIT%" (
    echo Could not find "%EVIDENCE_AUDIT%".
    echo Run this from the packaged SkyrimTogetherVR folder or rebuild the Windows package.
    exit /b 1
)

call "%EVIDENCE_AUDIT%" %* --require-avatar-sync
exit /b %ERRORLEVEL%
