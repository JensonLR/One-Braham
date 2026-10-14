@echo off
REM ===========================================================================
REM  ONE BRAHAM - validate project, maps, references and cook
REM ---------------------------------------------------------------------------
REM  A wrapper: the logic is in Validate.ps1, so the command lines exist once.
REM  Every argument is passed through, e.g.  Validate.bat -DryRun
REM  PowerShell 7 (pwsh) is used when installed, Windows PowerShell otherwise.
REM ===========================================================================
setlocal
set OB_PS=powershell
where pwsh >nul 2>&1 && set OB_PS=pwsh
%OB_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0Validate.ps1" %*
exit /b %errorlevel%
