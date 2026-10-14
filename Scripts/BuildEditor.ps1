<#
.SYNOPSIS
  ONE BRAHAM - compile.

.DESCRIPTION
  Builds the C++ module through Unreal Build Tool. The editor target is what the
  content pipeline needs (the reflected classes have to exist before
  BuildContent runs); the game and server targets are what packaging builds,
  and compiling them here first finds a server-only compile error before a
  forty-minute cook does.

  After every successful compile it runs the two checkers that guard what the
  compiler cannot see - declarations without definitions, and the reflection
  contracts - so a green build is also a consistent one.

.EXAMPLE
  Scripts\BuildEditor.ps1                          editor, Development
  Scripts\BuildEditor.ps1 -Target All              editor, game and server
  Scripts\BuildEditor.ps1 -Target Server -Configuration Shipping
  Scripts\BuildEditor.ps1 -Clean
#>
param(
    [ValidateSet('Editor', 'Game', 'Server', 'All')]
    [string]$Target = 'Editor',

    [ValidateSet('Debug', 'DebugGame', 'Development', 'Shipping')]
    [string]$Configuration = 'Development',

    [switch]$Clean,
    [switch]$DryRun
)

Import-Module (Join-Path $PSScriptRoot 'OneBraham.psm1') -Force

$engine = Get-OBEngine -DryRun:$DryRun
$targets = switch ($Target) {
    'Editor' { @('OneBrahamEditor') }
    'Game'   { @('OneBraham') }
    'Server' { @('OneBrahamServer') }
    'All'    { @('OneBrahamEditor', 'OneBraham', 'OneBrahamServer') }
}

foreach ($name in $targets) {
    # The editor only exists as Development or DebugGame.
    $config = if ($name -eq 'OneBrahamEditor' -and $Configuration -eq 'Shipping') { 'Development' } else { $Configuration }

    $arguments = @($name, $engine.HostPlatform, $config, "-Project=$(Get-OBProject)", '-WaitMutex', '-NoHotReloadFromIDE')
    if ($Clean) { $arguments += '-Clean' }

    Write-OBStep "Compiling $name ($($engine.HostPlatform) $config)"
    Invoke-OBTool -Name "Build-$name" -Exe $engine.Build -Arguments $arguments -DryRun:$DryRun | Out-Null
}

if (-not $Clean) {
    $python = Get-Command python3 -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }

    if ($python) {
        Write-OBStep 'Post-compile consistency checks'
        $env:PYTHONPATH = Join-Path (Get-OBRoot) 'Tools/Python'
        $env:PYTHONUTF8 = '1'
        foreach ($checker in @('check_cpp_symbols.py', 'check_uht_contracts.py')) {
            Invoke-OBTool -Name ($checker -replace '\.py$', '') -Exe $python.Source -DryRun:$DryRun -Arguments @(
                (Join-Path (Get-OBRoot) "Tools/Python/onebraham/$checker")
            ) | Out-Null
        }
    }
}

Write-OBStep 'Compile succeeded.'
exit 0
