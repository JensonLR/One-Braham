<#
.SYNOPSIS
  ONE BRAHAM - package the client.

.DESCRIPTION
  Builds, cooks, stages, paks and archives the game client with BuildCookRun.
  The cook list is the one in Config/DefaultGame.ini (MapsToCook): no -allmaps,
  so a test map left in Content/ does not ship, and a map that is not on the
  list does not ship either - add it there first.

  Output: Build\WindowsClient\Windows\OneBraham.exe (Build/LinuxClient/Linux/
  OneBraham.sh for the Linux development client). The script checks the
  executable exists before it says it succeeded.

.EXAMPLE
  Scripts\PackageClient.ps1                         Windows, Shipping
  Scripts\PackageClient.ps1 -Configuration Development
  Scripts\PackageClient.ps1 -Platform Linux -Configuration Development
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
$kind = if ($Platform -eq 'Win64') { 'WindowsClient' } else { 'LinuxClient' }
$archive = Get-OBArchiveDirectory $kind

if ($Platform -eq 'Linux' -and $engine.HostPlatform -eq 'Win64' -and -not $env:LINUX_MULTIARCH_ROOT -and -not (Test-OBDryRun -DryRun:$DryRun)) {
    throw 'A Linux build from Windows needs the cross-compile toolchain: install the one Epic lists for this engine version and set LINUX_MULTIARCH_ROOT.'
}

$arguments = @(
    'BuildCookRun',
    "-project=$(Get-OBProject)",
    '-noP4', '-utf8output', '-unattended',
    "-platform=$Platform",
    "-clientconfig=$Configuration",
    '-cook', '-stage', '-pak', '-iostore', '-compressed',
    '-archive', "-archivedirectory=$archive"
)
if (-not $SkipBuild) { $arguments += '-build' }
if ($Platform -eq 'Win64') { $arguments += '-prereqs' }
if ($Configuration -eq 'Shipping') { $arguments += '-nodebuginfo' }

Write-OBStep "Packaging the client ($Platform $Configuration) to $archive"
Invoke-OBTool -Name 'PackageClient' -Exe $engine.UAT -Arguments $arguments -DryRun:$DryRun | Out-Null

$exe = if ($Platform -eq 'Win64') { Join-Path $archive 'Windows/OneBraham.exe' } else { Join-Path $archive 'Linux/OneBraham.sh' }
if (-not (Test-OBDryRun -DryRun:$DryRun) -and -not (Test-Path $exe)) {
    throw "BuildCookRun reported success but $exe does not exist. Check the archive step in the log."
}

Write-OBStep "Packaged: $exe"
Write-Host '  Run it: Scripts\RunClient.bat [address]'
exit 0
