param([string]$Python = "$PSScriptRoot\..\.venv311\Scripts\python.exe", [string]$Dotnet = "$PSScriptRoot\..\.tools\dotnet\dotnet.exe")
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
if (!(Test-Path -LiteralPath $Python)) { throw 'Create a Python 3.11 venv in .venv311 and install requirements.txt plus pyinstaller first.' }
if (!(Test-Path -LiteralPath $Dotnet)) { $Dotnet = 'dotnet' }
& $Python -m PyInstaller --noconfirm --clean --name VoiceLock.AudioEngine --onedir --collect-all voxlock --collect-all soxr --collect-all sounddevice --collect-all soundfile --collect-all onnxruntime --collect-all huggingface_hub --hidden-import win32timezone --distpath dist/worker-build worker.py
if ($LASTEXITCODE -ne 0) { throw 'Worker packaging failed' }
& $Dotnet publish VoiceLock.Desktop -c Release -r win-x64 --self-contained true -o dist/VoiceLock
if ($LASTEXITCODE -ne 0) { throw 'Desktop publish failed' }
New-Item -ItemType Directory -Force dist/VoiceLock/worker | Out-Null
Copy-Item -Path dist/worker-build/VoiceLock.AudioEngine/* -Destination dist/VoiceLock/worker -Recurse -Force
Copy-Item README.md,THIRD-PARTY-NOTICES.md -Destination dist/VoiceLock
Copy-Item -Path docs -Destination dist/VoiceLock -Recurse -Force
& $Python -m scripts.licenses
if ($LASTEXITCODE -ne 0) { throw 'License collection failed' }
Copy-Item -Path vendor/voxlock/LICENSE -Destination dist/VoiceLock/licenses/VoxLock-LICENSE.txt -ErrorAction SilentlyContinue
Copy-Item -Path vendor/voxlock/NOTICE -Destination dist/VoiceLock/licenses/VoxLock-NOTICE.txt -ErrorAction SilentlyContinue
Copy-Item -Path .tools/dotnet/LICENSE.txt -Destination dist/VoiceLock/licenses/Dotnet-LICENSE.txt -ErrorAction SilentlyContinue
Copy-Item -Path .tools/dotnet/ThirdPartyNotices.txt -Destination dist/VoiceLock/licenses/Dotnet-ThirdPartyNotices.txt -ErrorAction SilentlyContinue
Write-Output 'Release: dist/VoiceLock/VoiceLock.Desktop.exe'
