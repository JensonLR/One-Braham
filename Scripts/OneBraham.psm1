# ===========================================================================
#  ONE BRAHAM - build script core
# ---------------------------------------------------------------------------
#  Every PowerShell script in this folder imports this module, so the engine is
#  found one way, every tool is invoked one way, and every log lands in one
#  place. The .bat files are one-line wrappers round the .ps1 files: the
#  command lines exist exactly once.
#
#  Engine discovery, in order:
#    1. $env:UE_ROOT          the folder that contains Engine\
#    2. $env:UE_EDITOR        ...\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
#    3. $env:UE_UAT           ...\Engine\Build\BatchFiles\RunUAT.bat
#    4. the registry entry the Epic Games Launcher writes, newest 5.x first
#    5. %ProgramFiles%\Epic Games\UE_5.x, newest first
#
#  Dry run: set OB_DRYRUN=1 (or pass -DryRun to any script) and every tool
#  invocation is printed instead of run. That is how these scripts are tested
#  on a machine with no engine, and how to see exactly what a build will do.
# ===========================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Script:OBRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Script:OBProject = Join-Path $Script:OBRoot 'OneBraham.uproject'
$Script:OBEngineVersions = @('5.7', '5.6', '5.5')
$Script:OBIsWindows = ($PSVersionTable.PSEdition -eq 'Desktop') -or $IsWindows

function Get-OBRoot { $Script:OBRoot }
function Test-OBIsWindows { $Script:OBIsWindows }
function Get-OBProject { $Script:OBProject }

function Write-OBStep([string]$Message) {
    Write-Host ''
    Write-Host "[OB] $Message" -ForegroundColor Cyan
}

function Write-OBOk([string]$Message)   { Write-Host "  [OK]   $Message" -ForegroundColor Green }
function Write-OBWarn([string]$Message) { Write-Host "  [WARN] $Message" -ForegroundColor Yellow }
function Write-OBFail([string]$Message) { Write-Host "  [FAIL] $Message" -ForegroundColor Red }

function Test-OBDryRun([switch]$DryRun) {
    return $DryRun.IsPresent -or ($env:OB_DRYRUN -eq '1')
}

function Resolve-OBEngineRoot {
    <# The folder that contains Engine\, or $null. #>
    $candidates = New-Object System.Collections.Generic.List[string]

    if ($env:UE_ROOT) { $candidates.Add($env:UE_ROOT) }

    # ...\Engine\Binaries\Win64\UnrealEditor-Cmd.exe -> four levels up.
    if ($env:UE_EDITOR) { $candidates.Add((Split-Path (Split-Path (Split-Path (Split-Path $env:UE_EDITOR))))) }

    # ...\Engine\Build\BatchFiles\RunUAT.bat -> four levels up.
    if ($env:UE_UAT) { $candidates.Add((Split-Path (Split-Path (Split-Path (Split-Path $env:UE_UAT))))) }

    if ($Script:OBIsWindows) {
        foreach ($version in $Script:OBEngineVersions) {
            $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$version"
            if (Test-Path $key) {
                $installed = (Get-ItemProperty -Path $key -Name 'InstalledDirectory' -ErrorAction SilentlyContinue).InstalledDirectory
                if ($installed) { $candidates.Add($installed) }
            }
        }

        foreach ($version in $Script:OBEngineVersions) {
            $candidates.Add((Join-Path $env:ProgramFiles "Epic Games\UE_$version"))
        }
    }
    else {
        foreach ($path in @("$HOME/UnrealEngine", '/opt/UnrealEngine')) { $candidates.Add($path) }
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'Engine/Build/Build.version'))) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Get-OBEngine {
    <#
      The engine's tools, as one object. Throws with the exact fix when the
      engine cannot be found - unless this is a dry run, where placeholder
      paths are returned so the command lines can still be printed.
    #>
    param([switch]$DryRun)

    $root = Resolve-OBEngineRoot

    if (-not $root) {
        if (Test-OBDryRun -DryRun:$DryRun) {
            $root = if ($Script:OBIsWindows) { 'C:\Program Files\Epic Games\UE_5.5' } else { '/opt/UnrealEngine' }
        }
        else {
            throw "Unreal Engine 5.5+ not found. Install it from the Epic Games Launcher, or set UE_ROOT to the folder that contains Engine\."
        }
    }

    $version = 'unknown'
    $versionFile = Join-Path $root 'Engine/Build/Build.version'
    if (Test-Path $versionFile) {
        $json = Get-Content $versionFile -Raw | ConvertFrom-Json
        $version = "$($json.MajorVersion).$($json.MinorVersion).$($json.PatchVersion)"
    }

    if ($Script:OBIsWindows) {
        [pscustomobject]@{
            Root      = $root
            Version   = $version
            Editor    = Join-Path $root 'Engine\Binaries\Win64\UnrealEditor.exe'
            EditorCmd = Join-Path $root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
            UAT       = Join-Path $root 'Engine\Build\BatchFiles\RunUAT.bat'
            Build     = Join-Path $root 'Engine\Build\BatchFiles\Build.bat'
            HostPlatform = 'Win64'
        }
    }
    else {
        [pscustomobject]@{
            Root      = $root
            Version   = $version
            Editor    = Join-Path $root 'Engine/Binaries/Linux/UnrealEditor'
            EditorCmd = Join-Path $root 'Engine/Binaries/Linux/UnrealEditor-Cmd'
            UAT       = Join-Path $root 'Engine/Build/BatchFiles/RunUAT.sh'
            Build     = Join-Path $root 'Engine/Build/BatchFiles/Linux/Build.sh'
            HostPlatform = 'Linux'
        }
    }
}

function Test-OBEngineVersion($Engine) {
    <# True when the engine is 5.5 or newer. #>
    if ($Engine.Version -eq 'unknown') { return $true }
    $parts = $Engine.Version.Split('.')
    return ([int]$parts[0] -gt 5) -or (([int]$parts[0] -eq 5) -and ([int]$parts[1] -ge 5))
}

function New-OBLogPath([string]$Name) {
    $dir = Join-Path $Script:OBRoot 'Saved/Logs/Scripts'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return (Join-Path $dir ("{0}-{1}.log" -f $Name, (Get-Date -Format 'yyyyMMdd-HHmmss')))
}

function Invoke-OBTool {
    <#
      Runs one tool, streams its output to the console and to a log, and throws
      on a non-zero exit code. A build step that fails and carries on is how a
      broken package ends up on Steam.
    #>
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string]$Exe,
        [string[]]$Arguments = @(),
        [switch]$DryRun,
        [switch]$AllowFailure
    )

    $printable = ($Arguments | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '

    if (Test-OBDryRun -DryRun:$DryRun) {
        Write-Host "  [dry-run] `"$Exe`" $printable"
        return [pscustomobject]@{ ExitCode = 0; Log = $null }
    }

    if (-not (Test-Path $Exe)) {
        throw "$Name : $Exe does not exist."
    }

    $log = New-OBLogPath $Name
    Write-Host "  > `"$Exe`" $printable"
    Write-Host "    log: $log"

    & $Exe @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
    $code = $LASTEXITCODE

    if ($code -ne 0 -and -not $AllowFailure) {
        throw "$Name failed with exit code $code. Read $log - the first 'Error' line is almost always the cause."
    }

    return [pscustomobject]@{ ExitCode = $code; Log = $log }
}

function Get-OBArchiveDirectory([string]$Kind) {
    <# Where packaged builds are archived, by kind: WindowsClient, WindowsServer, LinuxServer, LinuxClient. #>
    return (Join-Path $Script:OBRoot "Build/$Kind")
}

Export-ModuleMember -Function *-OB*
