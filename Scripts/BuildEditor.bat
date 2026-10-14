@echo off
REM ===========================================================================
REM  ONE BRAHAM - compile the editor, game or server target
REM ---------------------------------------------------------------------------
REM  A wrapper: the logic is in BuildEditor.ps1, so the command lines exist once.
REM  Every argument is passed through, e.g.  BuildEditor.bat -DryRun
REM  PowerShell 7 (pwsh) is used when installed, Windows PowerShell otherwise.
REM ===========================================================================
setlocal
set OB_PS=powershell
where pwsh >nul 2>&1 && set OB_PS=pwsh
%OB_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildEditor.ps1" %*
exit /b %errorlevel%
