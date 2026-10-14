<#
.SYNOPSIS
  ONE BRAHAM - validate the project, the map, the cook and the references.

.DESCRIPTION
  Four checks, each writing a report to Saved/Validation/ and exiting non-zero
  on a failure, so CI can gate on any one of them:

    Project     the engine-free ladder (run_all.py --text-only) and
                validate_project.py: settings, documented numbers, the input
                pipeline, tags, the audio bank, the interaction surface.
    Maps        opens the persistent map in the editor, runs MAP CHECK, and
                fails on any "MapCheck: Error" line.
    References  the DataValidation commandlet over /Game/OneBraham: missing
                references, broken soft paths, assets failing their own
                IsDataValid.
    Cook        a cook-only pass for Windows, then fails on any cook error and
                on every "Failed to load" / "Unable to find" line, which is how
                a missing reference shows up at cook time.

.EXAMPLE
  Scripts\Validate.ps1                     all four
  Scripts\Validate.ps1 -Check Project      the engine-free checks only
  Scripts\Validate.ps1 -Check Cook,Maps
#>
param(
    # Project, Maps, References, Cook or All; a comma list works from cmd too
    # (powershell -File hands "Cook,Maps" over as one string).
    [string[]]$Check = @('All'),

    [switch]$DryRun
)

Import-Module (Join-Path $PSScriptRoot 'OneBraham.psm1') -Force

$Check = @($Check | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$known = @('Project', 'Maps', 'References', 'Cook', 'All')
foreach ($name in $Check) {
    if ($known -notcontains $name) { throw "Unknown check '$name'. Use: $($known -join ', ')." }
}

if ($Check -contains 'All') { $Check = @('Project', 'Maps', 'References', 'Cook') }

$reportDir = Join-Path (Get-OBRoot) 'Saved/Validation'
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$failures = New-Object System.Collections.Generic.List[string]

function Test-OBLog {
    <# Counts the lines of a tool log that match a failure pattern, and reports them. #>
    param([string]$Name, [string]$Log, [string[]]$Patterns)

    if (-not $Log -or -not (Test-Path $Log)) { return }

    $hits = Select-String -Path $Log -Pattern $Patterns
    $report = Join-Path $reportDir ("{0}-{1}.txt" -f $Name, (Get-Date -Format 'yyyyMMdd-HHmmss'))
    $hits | ForEach-Object { $_.Line } | Set-Content -Path $report

    if ($hits.Count -gt 0) {
        Write-OBFail "$Name : $($hits.Count) problem line(s). First: $($hits[0].Line.Trim())"
        Write-Host "         all of them: $report"
        $failures.Add($Name)
    }
    else {
        Write-OBOk "$Name : clean"
    }
}

if ($Check -contains 'Project') {
    Write-OBStep 'Project: the engine-free checks'
    $python = Get-Command python3 -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }

    if (-not $python) {
        Write-OBFail 'Project: Python 3 is not on PATH.'
        $failures.Add('Project')
    }
    else {
        $env:PYTHONPATH = Join-Path (Get-OBRoot) 'Tools/Python'
        $env:PYTHONUTF8 = '1'
        foreach ($script in @('run_all.py', 'validate_project.py')) {
            $arguments = @((Join-Path (Get-OBRoot) "Tools/Python/onebraham/$script"))
            if ($script -eq 'run_all.py') { $arguments += '--text-only' }

            $result = Invoke-OBTool -Name ($script -replace '\.py$', '') -Exe $python.Source -Arguments $arguments -DryRun:$DryRun -AllowFailure
            if ($result.ExitCode -ne 0) { $failures.Add("Project/$script") }
        }
    }
}

$needsEngine = @('Maps', 'References', 'Cook') | Where-Object { $Check -contains $_ }
if ($needsEngine) {
    $engine = Get-OBEngine -DryRun:$DryRun
    $common = @('-unattended', '-nopause', '-nosplash', '-stdout', '-FullStdOutLogOutput', '-utf8output')

    if ($Check -contains 'Maps') {
        Write-OBStep 'Maps: MAP CHECK on the persistent map'
        $result = Invoke-OBTool -Name 'MapCheck' -Exe $engine.EditorCmd -DryRun:$DryRun -AllowFailure -Arguments (@(
            (Get-OBProject), '/Game/OneBraham/Maps/L_Persistent_OneBraham',
            '-ExecCmds=MAP CHECK;QUIT_EDITOR', '-NullRHI'
        ) + $common)
        Test-OBLog -Name 'MapCheck' -Log $result.Log -Patterns @('MapCheck: Error', 'MapCheck:.*Critical')
    }

    if ($Check -contains 'References') {
        Write-OBStep 'References: DataValidation over /Game/OneBraham'
        $result = Invoke-OBTool -Name 'DataValidation' -Exe $engine.EditorCmd -DryRun:$DryRun -AllowFailure -Arguments (@(
            (Get-OBProject), '-run=DataValidation', '-Paths=/Game/OneBraham', '-NullRHI'
        ) + $common)
        Test-OBLog -Name 'DataValidation' -Log $result.Log -Patterns @(
            'Error:', 'is invalid', 'Failed to load', 'Unable to find', 'missing'
        )
        if ($result.ExitCode -ne 0 -and -not $failures.Contains('DataValidation')) { $failures.Add('DataValidation') }
    }

    if ($Check -contains 'Cook') {
        Write-OBStep 'Cook: a cook-only pass for Windows'
        $result = Invoke-OBTool -Name 'Cook' -Exe $engine.EditorCmd -DryRun:$DryRun -AllowFailure -Arguments (@(
            (Get-OBProject), '-run=cook', '-targetplatform=Windows', '-unversioned', '-NullRHI'
        ) + $common)
        Test-OBLog -Name 'Cook' -Log $result.Log -Patterns @(
            'LogCook: Error', 'Error: ', 'Failed to load', 'Unable to find package', "Can't find file"
        )
        if ($result.ExitCode -ne 0 -and -not $failures.Contains('Cook')) { $failures.Add('Cook') }
    }
}

Write-OBStep 'Validation summary'
if ($failures.Count -gt 0) {
    Write-OBFail ("failed: " + ($failures -join ', '))
    exit 1
}

Write-OBOk ('passed: ' + ($Check -join ', '))
exit 0
