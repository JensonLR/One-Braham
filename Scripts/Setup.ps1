<#
.SYNOPSIS
  ONE BRAHAM - first-run setup.

.DESCRIPTION
  Gets a fresh clone to the point where it can be built:
    1. finds Unreal Engine 5.5+ and checks its version;
    2. checks every plugin OneBraham.uproject enables is present in the engine
       (the project has no plugins of its own to restore: every one ships with
       the engine, so "restore" means "verify, and say what to install");
    3. checks Python 3, for the engine-free checks;
    4. checks the Steam client, for the overlay and invites in development;
    5. generates the IDE project files;
    6. runs the engine-free checks, so the first thing a new machine proves is
       that the tree it was handed is consistent.

.EXAMPLE
  Scripts\Setup.ps1
  Scripts\Setup.ps1 -SkipProjectFiles
  Scripts\Setup.ps1 -DryRun
#>
param(
    [switch]$SkipProjectFiles,
    [switch]$SkipChecks,
    [switch]$DryRun
)

Import-Module (Join-Path $PSScriptRoot 'OneBraham.psm1') -Force

$problems = 0

Write-OBStep 'ONE BRAHAM - environment check'

# --- 1. Engine ---------------------------------------------------------------
$engine = $null
try {
    $engine = Get-OBEngine -DryRun:$DryRun
    if (Test-OBEngineVersion $engine) {
        Write-OBOk "Unreal Engine $($engine.Version) at $($engine.Root)"
    }
    else {
        Write-OBFail "Unreal Engine $($engine.Version) found; ONE BRAHAM needs 5.5 or newer."
        $problems++
    }
}
catch {
    Write-OBFail $_.Exception.Message
    $problems++
}

# --- 2. Plugins ----------------------------------------------------------------
if ($engine -and (Test-Path (Join-Path $engine.Root 'Engine/Plugins'))) {
    $project = Get-Content (Get-OBProject) -Raw | ConvertFrom-Json
    $available = @{}
    Get-ChildItem -Path (Join-Path $engine.Root 'Engine/Plugins') -Recurse -Filter '*.uplugin' -ErrorAction SilentlyContinue |
        ForEach-Object { $available[$_.BaseName] = $_.FullName }

    foreach ($plugin in $project.Plugins | Where-Object { $_.Enabled }) {
        if ($available.ContainsKey($plugin.Name)) {
            Write-OBOk "plugin $($plugin.Name)"
        }
        else {
            Write-OBFail "plugin $($plugin.Name) is enabled in OneBraham.uproject and is not in the engine. Install it from the Epic Games Launcher (Options > installed engine components) or Fab."
            $problems++
        }
    }

    foreach ($optional in $project.OneBrahamProjectSettings.OptionalPluginsToEnableByHand) {
        if ($available.ContainsKey($optional)) {
            Write-OBOk "optional plugin $optional available (enable it by hand when its content arrives)"
        }
        else {
            Write-OBWarn "optional plugin $optional not installed. Only needed for the content that uses it (Docs/16)."
        }
    }
}
elseif ($engine) {
    Write-OBWarn 'engine plugin folder not readable: plugin check skipped (dry run without an engine).'
}

# --- 3. Python -------------------------------------------------------------------
$python = Get-Command python3 -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
if ($python) {
    Write-OBOk ("Python: " + (& $python.Source --version 2>&1))
}
else {
    Write-OBWarn 'Python 3 is not on PATH. The engine-free checks (Scripts\RunAll.bat) need it; the editor brings its own for the content build.'
}

# --- 4. Steam ----------------------------------------------------------------------
$steam = if (Test-OBIsWindows) {
    Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'
} else { Join-Path $HOME '.steam/steam/steam.sh' }
if (Test-Path $steam) {
    Write-OBOk "Steam client at $steam"
}
else {
    Write-OBWarn 'Steam client not found. Listen-server invites and the overlay need it in development; a dedicated server does not.'
}

if ($problems -gt 0) {
    Write-OBFail "$problems blocking problem(s). Fix them and run Setup again."
    exit 1
}

# --- 5. Project files ------------------------------------------------------------
if (-not $SkipProjectFiles) {
    Write-OBStep 'Generating project files'
    Invoke-OBTool -Name 'GenerateProjectFiles' -Exe $engine.Build -DryRun:$DryRun -Arguments @(
        '-projectfiles', "-project=$(Get-OBProject)", '-game', '-rocket', '-progress'
    ) | Out-Null
}

# --- 6. Engine-free checks ------------------------------------------------------------
if (-not $SkipChecks -and $python) {
    Write-OBStep 'Engine-free checks (the same ladder CI runs)'
    $env:PYTHONPATH = Join-Path (Get-OBRoot) 'Tools/Python'
    $env:PYTHONUTF8 = '1'
    Invoke-OBTool -Name 'RunAll' -Exe $python.Source -DryRun:$DryRun -Arguments @(
        (Join-Path (Get-OBRoot) 'Tools/Python/onebraham/run_all.py'), '--text-only'
    ) | Out-Null
}

Write-OBStep 'Setup complete. Next:'
Write-Host '    Scripts\BuildEditor.bat         compile the editor'
Write-Host '    Scripts\BuildContent.bat        build the content inside it'
Write-Host '    Scripts\PackageClient.bat       package the Windows client'
Write-Host '    Scripts\PackageServer.bat       package the dedicated server'
Write-Host '    README_BUILD.md                 the whole sequence, with what each step proves'
exit 0
