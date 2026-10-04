param([string]$StageDirectory = 'work/runtime-package')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage = [IO.Path]::GetFullPath((Join-Path $root $StageDirectory))
if (!$stage.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'StageDirectory must be inside the project.' }
if (Test-Path -LiteralPath $stage) { throw 'Use a new, empty StageDirectory.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$client = [Net.WebClient]::new()
$archive = Join-Path $stage 'python.zip'
$client.DownloadFile('https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip', $archive)
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3') { throw 'Python archive checksum mismatch.' }
$pythonRoot = Join-Path $stage 'python'
Expand-Archive -LiteralPath $archive -DestinationPath $pythonRoot
[IO.File]::WriteAllText((Join-Path $pythonRoot 'python312._pth'), "python312.zip`n.`nLib/site-packages`nimport site`n", [Text.UTF8Encoding]::new($false))
$bootstrap = Join-Path $stage 'get-pip.py'
$client.DownloadFile('https://bootstrap.pypa.io/get-pip.py', $bootstrap)
$python = Join-Path $pythonRoot 'python.exe'
& $python $bootstrap 'pip==26.2.1' 'setuptools==70.3.0'
if ($LASTEXITCODE) { throw 'pip installation failed.' }
& $python -m pip install -r (Join-Path $root 'translation/requirements.txt')
if ($LASTEXITCODE) { throw 'Runtime dependencies installation failed.' }
[IO.File]::WriteAllText((Join-Path $pythonRoot 'ready.txt'), 'PinyinShift offline runtime', [Text.UTF8Encoding]::new($false))
Compress-Archive -Path (Join-Path $pythonRoot '*') -DestinationPath (Join-Path $root 'runtime/windows-runtime.zip') -CompressionLevel Optimal -Force
Write-Host 'Runtime packaged. Update runtime/README.md with its SHA-256.'
