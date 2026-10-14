<#
.SYNOPSIS
  ONE BRAHAM - run the packaged client.

.DESCRIPTION
  To the main menu, to a server, or as a listen host. The first argument, if
  there is one, is where to go: an address (127.0.0.1, host:7810) or a map URL
  (L_Persistent_OneBraham?listen). Everything after it is passed through.
  Arguments are taken raw, so ? options survive.

.EXAMPLE
  Scripts\RunClient.ps1
  Scripts\RunClient.ps1 127.0.0.1
  Scripts\RunClient.ps1 127.0.0.1:7810 -windowed -ResX=1600 -ResY=900
  Scripts\RunClient.ps1 "L_Persistent_OneBraham?listen?Bots=11"
#>
Import-Module (Join-Path $PSScriptRoot 'OneBraham.psm1') -Force

$items = @($args | ForEach-Object { [string]$_ })
if ($items -contains '-DryRun') {
    $env:OB_DRYRUN = '1'
    $items = @($items | Where-Object { $_ -ne '-DryRun' })
}

$exe = if (Test-OBIsWindows) {
    Join-Path (Get-OBArchiveDirectory 'WindowsClient') 'Windows/OneBraham.exe'
} else {
    Join-Path (Get-OBArchiveDirectory 'LinuxClient') 'Linux/OneBraham.sh'
}

if (-not (Test-OBDryRun) -and -not (Test-Path $exe)) {
    Write-OBFail "The packaged client was not found at $exe"
    Write-Host '         Package it first: Scripts\PackageClient.bat'
    exit 1
}

$arguments = @()
if ($items.Count -gt 0) {
    $arguments = @($items[0], '-log') + @($items | Select-Object -Skip 1)
    Write-OBStep "Client: $($items[0])"
}
else {
    Write-OBStep 'Client: main menu'
}

if (Test-OBDryRun) {
    Write-Host "  [dry-run] `"$exe`" $($arguments -join ' ')"
    exit 0
}

& $exe @arguments
exit $LASTEXITCODE
