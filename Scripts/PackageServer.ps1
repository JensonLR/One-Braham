<#
.SYNOPSIS
  ONE BRAHAM - package the dedicated server.

.DESCRIPTION
  Builds the OneBrahamServer target (Source/OneBrahamServer.Target.cs) and cooks
  server-only content. -server -noclient is the part that matters: without it
  BuildCookRun packages a client, which is what this project's old
  Package.bat Server did.

  Output:
    Win64  Build\WindowsServer\WindowsServer\OneBrahamServer.exe
    Linux  Build/LinuxServer/LinuxServer/OneBrahamServer.sh

  A Linux server from a Windows host needs Epic's cross-compile toolchain and
  LINUX_MULTIARCH_ROOT; on a Linux host, Scripts/Package.sh Server does the
  same job natively.

.EXAMPLE
  Scripts\PackageServer.ps1                       Windows, Shipping
  Scripts\PackageServer.ps1 -Platform Linux       the rented boxes
  Scripts\PackageServer.ps1 -Configuration Development
#>
param(
    [ValidateSet('Win64', 'Linux')]
    [string]$Platform = 'Win64',

    [ValidateSet('Development', 'Shipping', 'Test')]
    [string]$Configuration = 'Shipping',

    [switch]$SkipBuild,
    [switch]$DryRun
)

Import-Module (Join-Path $PSScriptRoot 'OneBraham.psm1') -Force

$engine = Get-OBEngine -DryRun:$DryRun
$kind = if ($Platform -eq 'Win64') { 'WindowsServer' } else { 'LinuxServer' }
$archive = Get-OBArchiveDirectory $kind

if ($Platform -eq 'Linux' -and $engine.HostPlatform -eq 'Win64' -and -not $env:LINUX_MULTIARCH_ROOT -and -not (Test-OBDryRun -DryRun:$DryRun)) {
    throw 'A Linux server from Windows needs the cross-compile toolchain: install the one Epic lists for this engine version and set LINUX_MULTIARCH_ROOT, or run Scripts/Package.sh Server on a Linux host.'
}

$arguments = @(
    'BuildCookRun',
    "-project=$(Get-OBProject)",
    '-noP4', '-utf8output', '-unattended',
    '-server', '-noclient',
    "-platform=$Platform",
    "-serverplatform=$Platform",
    "-serverconfig=$Configuration",
    '-cook', '-stage', '-pak', '-iostore', '-compressed',
    '-archive', "-archivedirectory=$archive"
)
if (-not $SkipBuild) { $arguments += '-build' }

Write-OBStep "Packaging the dedicated server ($Platform $Configuration) to $archive"
Invoke-OBTool -Name 'PackageServer' -Exe $engine.UAT -Arguments $arguments -DryRun:$DryRun | Out-Null

$exe = if ($Platform -eq 'Win64') { Join-Path $archive 'WindowsServer/OneBrahamServer.exe' } else { Join-Path $archive 'LinuxServer/OneBrahamServer.sh' }
if (-not (Test-OBDryRun -DryRun:$DryRun) -and -not (Test-Path $exe)) {
    throw "BuildCookRun reported success but $exe does not exist. Check the archive step in the log."
}

Write-OBStep "Packaged: $exe"
Write-Host '  Run it: Scripts\RunServer.bat   (Scripts/RunServer.sh on Linux)'
exit 0
