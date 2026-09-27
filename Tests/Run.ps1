$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $repoRoot 'x64\dma-tests'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw 'Visual Studio C++ x64 tools not found.' }
& (Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
Push-Location $testOutput
try {
    $common = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/W3', "/I$repoRoot", "/I$repoRoot\Code")
    & cl @common /LD "$PSScriptRoot\MockVmm.cpp" /link /OUT:vmm.dll "/DEF:$PSScriptRoot\MockVmm.def"
    if ($LASTEXITCODE) { throw 'Mock VMM build failed.' }
    & cl @common "$PSScriptRoot\MemoryTests.cpp" "$repoRoot\Memory\Memory.cpp" "$repoRoot\Memory\DmaBackend.cpp" "$repoRoot\Memory\RpmBackend.cpp" "$repoRoot\Code\Core\Process.cpp" /Fe:MemoryTests.exe
    if ($LASTEXITCODE) { throw 'Memory regression build failed.' }
    Copy-Item -LiteralPath "$repoRoot\ThirdParty\MemProcFS\bin\leechcore.dll","$repoRoot\ThirdParty\MemProcFS\bin\FTD3XX.dll" -Destination $testOutput
    & .\MemoryTests.exe
    if ($LASTEXITCODE) { throw 'Memory regression tests failed.' }

    & cl @common "$PSScriptRoot\NamePoolTests.cpp" "$repoRoot\Memory\Memory.cpp" "$repoRoot\Memory\DmaBackend.cpp" "$repoRoot\Memory\RpmBackend.cpp" /Fe:NamePoolTests.exe
    if ($LASTEXITCODE) { throw 'Name-pool regression build failed.' }
    $namePoolArgs = @()
    if (Test-Path -LiteralPath 'live-namepool.bin') { $namePoolArgs += 'live-namepool.bin' }
    & .\NamePoolTests.exe @namePoolArgs
    if ($LASTEXITCODE) { throw 'Name-pool regression tests failed.' }

    # Exercise the real SafeMode CLI with synthetic target memory via MockVmm.
    Copy-Item -LiteralPath "$repoRoot\x64\Release-DMA\UEDumper.exe" -Destination $testOutput
    $config = Get-Content -LiteralPath "$repoRoot\config.ini" -Raw
    $config = $config -replace '(?m)^GNames\s*=\s*0x[0-9A-Fa-f]+', 'GNames = 0x2000'
    $config = $config -replace '(?m)^GObjects\s*=\s*0x[0-9A-Fa-f]+', 'GObjects = 0x1000'
    [System.IO.File]::WriteAllText((Join-Path $testOutput 'config.ini'), $config, [System.Text.UTF8Encoding]::new($false))
    $safeModeOutput = & .\UEDumper.exe --no-pause
    $safeModeOutput | Set-Content -LiteralPath 'safe-mode.log'
    if ($LASTEXITCODE) { throw 'Synthetic SafeMode run failed.' }
    $names = @($safeModeOutput | Where-Object { $_ -match '^\s+\d+\s+\|\s+0x[0-9A-F]+\s+\|\s+0x[0-9A-F]+\s+\|\s+\S+' })
    if ($names.Count -ne 10 -or !($safeModeOutput -match '/Script/CoreUObject') -or !($safeModeOutput -match '\| Actor$')) {
        throw 'SafeMode did not resolve all 10 synthetic UObject names.'
    }
    $names
    Write-Output 'PASS: real SafeMode CLI resolved all 10 synthetic UObject names.'
} finally {
    Pop-Location
}
