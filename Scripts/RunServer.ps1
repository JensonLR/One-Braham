<#
.SYNOPSIS
  ONE BRAHAM - run the packaged dedicated server.

.DESCRIPTION
  No renderer, no local player: the whole match simulation - lockdown,
  extraction, purge. Arguments are taken raw, so map URLs with ? options pass
  through untouched (cmd.exe's own argument splitting breaks on both ? and =).

    -Map=<map[?options]>   default L_Persistent_OneBraham
    -Port=<port>           default 7777
    anything else          passed to the server as it is (-Mode=FireDrill, ...)

.EXAMPLE
  Scripts\RunServer.ps1
  Scripts\RunServer.ps1 -Mode=FireDrill
  Scripts\RunServer.ps1 "-Map=L_Persistent_OneBraham?Bots=11" -Port=7810
#>
Import-Module (Join-Path $PSScriptRoot 'OneBraham.psm1') -Force

$map = 'L_Persistent_OneBraham'
$port = '7777'
$extra = New-Object System.Collections.Generic.List[string]

foreach ($arg in $args) {
    $text = [string]$arg
    if ($text -match '^-Map=(.+)$') { $map = $Matches[1] }
    elseif ($text -match '^-Port=(\d+)$') { $port = $Matches[1] }
    elseif ($text -eq '-DryRun') { $env:OB_DRYRUN = '1' }
    else { $extra.Add($text) }
}

$exe = if (Test-OBIsWindows) {
    Join-Path (Get-OBArchiveDirectory 'WindowsServer') 'WindowsServer/OneBrahamServer.exe'
} else {
    Join-Path (Get-OBArchiveDirectory 'LinuxServer') 'LinuxServer/OneBrahamServer.sh'
}

if (-not (Test-OBDryRun) -and -not (Test-Path $exe)) {
    Write-OBFail "The packaged server was not found at $exe"
    Write-Host '         Package it first: Scripts\PackageServer.bat'
    exit 1
}

# The map is the first thing on the command line, as a bare map name: the old
# default here, "OneBrahamServer-L_Persistent_OneBraham", is not a map.
$arguments = @($map, '-server', '-log', "-Port=$port", '-SteamServerName=One-Braham-London', '-MaxPlayers=16') + $extra

Write-OBStep "Dedicated server: $map on port $port"
if (Test-OBDryRun) {
    Write-Host "  [dry-run] `"$exe`" $($arguments -join ' ')"
    exit 0
}

& $exe @arguments
exit $LASTEXITCODE
