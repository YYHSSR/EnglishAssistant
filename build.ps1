param([string]$ToolchainBin = '', [string]$BuildDirectory = 'work\build')
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if ($ToolchainBin) {
    if (!(Test-Path -LiteralPath (Join-Path $ToolchainBin 'g++.exe'))) { throw 'ToolchainBin must contain g++.exe' }
    $env:PATH = "$ToolchainBin;$env:PATH"
} elseif (!(Get-Command g++.exe -ErrorAction SilentlyContinue)) {
    foreach ($candidate in @('E:\msys2\ucrt64\bin', 'C:\msys64\ucrt64\bin', 'C:\msys64\mingw64\bin')) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'g++.exe')) { $env:PATH = "$candidate;$env:PATH"; break }
    }
}
if (!(Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'CMake is required.' }
if (!(Get-Command g++.exe -ErrorAction SilentlyContinue)) { throw 'A MinGW C++ compiler is required.' }
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$rootPrefix = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
if (!$buildPath.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'BuildDirectory must be inside the project directory.' }
# CMake caches absolute source paths. Archive a cache left by moving the project.
$cachePath = Join-Path $buildPath 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $cachedSource = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($cachedSource -and ![String]::Equals([IO.Path]::GetFullPath($cachedSource.Matches[0].Groups[1].Value),[IO.Path]::GetFullPath($projectRoot),[StringComparison]::OrdinalIgnoreCase)) {
        $backupPath = [IO.Path]::GetFullPath((Join-Path $projectRoot ('work\backup\moved-build-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))))
        if (!$backupPath.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup path.' }
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($backupPath)) -Force | Out-Null
        Move-Item -LiteralPath $buildPath -Destination $backupPath
    }
}
& cmake -S $projectRoot -B $buildPath -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildPath
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
& ctest --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
foreach ($name in @('PinyinShift.exe')) {
    Copy-Item -LiteralPath (Join-Path $buildPath $name) -Destination (Join-Path $projectRoot $name) -Force
}
Write-Host "Ready: $(Join-Path $projectRoot 'PinyinShift.exe')"
