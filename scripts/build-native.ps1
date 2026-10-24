param(
    [string]$CMake = "$PSScriptRoot\..\.venv311\Scripts\cmake.exe",
    [string]$Dotnet = "$PSScriptRoot\..\.tools\dotnet\dotnet.exe",
    [string]$OrtDll = "$PSScriptRoot\..\.venv311\Lib\site-packages\onnxruntime\capi\onnxruntime.dll"
)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
if (!(Test-Path -LiteralPath $CMake)) { $CMake = 'cmake' }
if (!(Test-Path -LiteralPath $Dotnet)) { $Dotnet = 'dotnet' }
New-Item -ItemType Directory -Force native/deps/ort,native/deps/json | Out-Null
foreach ($file in @('onnxruntime_c_api.h','onnxruntime_cxx_api.h','onnxruntime_cxx_inline.h','onnxruntime_float16.h','onnxruntime_ep_c_api.h')) {
    if (!(Test-Path -LiteralPath "native/deps/ort/$file")) {
        Invoke-WebRequest "https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/include/onnxruntime/core/session/$file" -OutFile "native/deps/ort/$file"
    }
}
if (!(Test-Path -LiteralPath native/deps/json/json.hpp)) {
    Invoke-WebRequest 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp' -OutFile native/deps/json/json.hpp
}
if (!(Test-Path -LiteralPath native/deps/soxr/CMakeLists.txt)) {
    git clone https://github.com/chirlu/soxr.git native/deps/soxr
    if ($LASTEXITCODE -ne 0) { throw 'SoXR source download failed' }
    git -C native/deps/soxr checkout 945b592b70470e29f917f4de89b4281fbbd540c0
    if ($LASTEXITCODE -ne 0) { throw 'SoXR revision checkout failed' }
}
# Upstream 0.1.3 CMake needs these two fixes for add_subdirectory and spaces.
$source = Get-Content -LiteralPath native/deps/soxr/CMakeLists.txt -Raw
$source = $source.Replace('UNIX;NOT CMAKE_CROSSCOMPILING;EXISTS ${PROJECT_SOURCE_DIR}/lsr-tests;WITH_LSR_BINDINGS','UNIX;NOT CMAKE_CROSSCOMPILING;WITH_LSR_BINDINGS').Replace('${CMAKE_SOURCE_DIR}/cmake/Modules','${PROJECT_SOURCE_DIR}/cmake/Modules')
Set-Content -LiteralPath native/deps/soxr/CMakeLists.txt -Value $source
if (!(Test-Path -LiteralPath $OrtDll)) { throw 'Pass -OrtDll with an official ONNX Runtime CPU x64 DLL (tested: 1.31.0).' }
& $CMake -S native -B native/build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Native configuration failed; install Visual Studio Desktop development with C++ and Windows SDK.' }
& $CMake --build native/build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
Copy-Item $OrtDll native/build/Release/onnxruntime.dll -Force
Copy-Item native/build/deps/soxr/bin/Release/soxr.dll native/build/Release/soxr.dll -Force
& native/build/Release/VoiceLock.AudioEngine.exe selftest
if ($LASTEXITCODE -ne 0) { throw 'Native self-test failed' }
& $Dotnet publish VoiceLock.Desktop -c Release -r win-x64 --self-contained true -o dist/VoiceLock-Native
if ($LASTEXITCODE -ne 0) { throw 'Desktop publish failed' }
New-Item -ItemType Directory -Force dist/VoiceLock-Native/worker,dist/VoiceLock-Native/licenses | Out-Null
Copy-Item native/build/Release/VoiceLock.AudioEngine.exe,native/build/Release/onnxruntime.dll,native/build/Release/soxr.dll dist/VoiceLock-Native/worker -Force
# Ship the app-local MSVC runtime used by these official/native DLLs.
$redist = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC' -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'x64\Microsoft.VC143.CRT') } | Sort-Object Name -Descending | Select-Object -First 1
if ($redist) { Copy-Item "$($redist.FullName)\x64\Microsoft.VC143.CRT\*.dll" dist/VoiceLock-Native/worker -Force }
Copy-Item docs/NATIVE.md dist/VoiceLock-Native/README.md -Force
Copy-Item docs/NATIVE-VALIDATION.md dist/VoiceLock-Native/VALIDATION.md -Force
Copy-Item THIRD-PARTY-NOTICES.md dist/VoiceLock-Native -Force
Copy-Item native/deps/soxr/LICENCE dist/VoiceLock-Native/licenses/SoXR-LICENCE.txt -Force
Copy-Item native/deps/json/json.hpp dist/VoiceLock-Native/licenses/json-header-with-MIT-license.hpp -Force
Copy-Item vendor/voxlock/LICENSE dist/VoiceLock-Native/licenses/VoxLock-LICENSE.txt -Force
Copy-Item vendor/voxlock/NOTICE dist/VoiceLock-Native/licenses/VoxLock-NOTICE.txt -Force
Copy-Item .tools/dotnet/LICENSE.txt dist/VoiceLock-Native/licenses/Dotnet-LICENSE.txt -Force
Copy-Item .tools/dotnet/ThirdPartyNotices.txt dist/VoiceLock-Native/licenses/Dotnet-ThirdPartyNotices.txt -Force
Copy-Item dist/VoiceLock/licenses/onnxruntime dist/VoiceLock-Native/licenses -Recurse -Force -ErrorAction SilentlyContinue
git -C native/deps/soxr archive --format=zip --output=../../../dist/VoiceLock-Native/licenses/soxr-upstream-source.zip HEAD
Copy-Item scripts/build-native.ps1 dist/VoiceLock-Native/licenses/soxr-build-patches.ps1 -Force
Write-Output 'Native release: dist/VoiceLock-Native/VoiceLock.Desktop.exe'
