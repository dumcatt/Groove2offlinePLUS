# Builds libgcp.so and the patched libtune.so into GC2Plus\out\.
#   -Deploy   push both libraries into the installed game on a rooted device (adb + su)
param(
    [string]$Ndk = "$env:LOCALAPPDATA\Android\Sdk\ndk\27.2.12479018",
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'out'
New-Item -ItemType Directory -Force $out | Out-Null

# Original libtune.so straight from the unmodified game APK (the setup app's assets now hold the modded one).
$apk = Join-Path $root 'original\groovecoaster.apk'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path $apk))
try {
    [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry('lib/arm64-v8a/libtune.so'), "$out\libtune.orig.so", $true)
} finally { $zip.Dispose() }

python "$root\tools\patch_libtune.py" "$out\libtune.orig.so" "$out\libtune.so"
if ($LASTEXITCODE) { throw 'patch failed' }

$clang = "$Ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
$src = Get-ChildItem "$root\native\*.cpp" | ForEach-Object FullName
& $clang --target=aarch64-linux-android21 -shared -fPIC -O2 -std=c++17 -Wall `
    -fno-exceptions -fno-rtti -static-libstdc++ '-Wl,--no-undefined' '-Wl,-soname,libgcp.so' `
    -o "$out\libgcp.so" @src -llog -lGLESv1_CM -lEGL
if ($LASTEXITCODE) { throw 'compile failed' }
Write-Host "built $out\libgcp.so"

if ($Deploy) {
    $pkg = 'jp.co.taito.groovecoasterzero'
    $libDir = (adb shell pm path $pkg | Select-Object -First 1).Trim() -replace '^package:', '' -replace '/base\.apk$', '/lib/arm64'
    adb shell am force-stop $pkg
    adb push "$out\libtune.so" "$out\libgcp.so" /data/local/tmp/ | Out-Null
    adb shell su -c "cp /data/local/tmp/libtune.so /data/local/tmp/libgcp.so '$libDir/' && cd '$libDir' && chmod 755 *.so && chown system:system *.so && chcon u:object_r:apk_data_file:s0 *.so && ls -laZ"
}
