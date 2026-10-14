@echo off
REM ===========================================================================
REM  ONE BRAHAM - run the packaged client: main menu, an address, or a map URL to host
REM ---------------------------------------------------------------------------
REM  A wrapper: the logic is in RunClient.ps1, which takes the arguments raw. cmd.exe
REM  splits arguments on = and treats ? as a wildcard, which breaks map URLs
REM  like L_Persistent_OneBraham?listen?Bots=11 - so it does no parsing here.
REM  Examples are in RunClient.ps1 and README_RUN.md.
REM ===========================================================================
setlocal
set OB_PS=powershell
where pwsh >nul 2>&1 && set OB_PS=pwsh
%OB_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0RunClient.ps1" %*
exit /b %errorlevel%
