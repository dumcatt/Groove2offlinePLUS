# Builds the full modded game APK: GC2Plus\out\groovecoaster-gc2plus.apk
#   - patched libtune.so + libgcp.so (from build.ps1)
#   - GC2PlusPad (gamepad play) and GC2PlusSearch (song search box) as classes2.dex,
#     hooked into MainActivity.dispatchKeyEvent / dispatchGenericMotionEvent / onCreate
#   - signed with GC2Plus\gc2plus.keystore (created on first run)
param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$BuildTools = '36.0.0',
    [string]$Platform = 'android-37.0'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'out'
$work = Join-Path $out 'apk'
$bt = Join-Path $Sdk "build-tools\$BuildTools"
$androidJar = Join-Path $Sdk "platforms\$Platform\android.jar"

& "$root\build.ps1"

# 1. Decode the unmodified game APK (never the setup app's asset, which is the modded output).
$apk = Resolve-Path (Join-Path $root 'original\groovecoaster.apk')
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
java -jar "$root\tools\apktool.jar" d -f -o $work $apk | Out-Null
if ($LASTEXITCODE) { throw 'apktool decode failed' }

# 2. Route gamepad key and stick events through GC2PlusPad before the game's own handling.
$smali = Join-Path $work 'smali\jp\co\taito\groovecoasterzero\MainActivity.smali'
$text = [IO.File]::ReadAllText($smali) -replace "`r`n", "`n"
$anchor = ".method public dispatchKeyEvent(Landroid/view/KeyEvent;)Z`n    .locals 4`n"
if (-not $text.Contains($anchor)) { throw 'dispatchKeyEvent anchor not found' }
$keyHook = $anchor + @'

    invoke-static {p0, p1}, Ljp/co/taito/groovecoasterzero/GC2PlusPad;->handleKey(Landroid/app/Activity;Landroid/view/KeyEvent;)Z

    move-result v0

    if-eqz v0, :gc2plus_not_handled

    const/4 v0, 0x1

    return v0

    :gc2plus_not_handled

'@
$motionMethod = @'

.method public dispatchGenericMotionEvent(Landroid/view/MotionEvent;)Z
    .locals 1

    invoke-static {p0, p1}, Ljp/co/taito/groovecoasterzero/GC2PlusPad;->handleMotion(Landroid/app/Activity;Landroid/view/MotionEvent;)Z

    move-result v0

    if-eqz v0, :gc2plus_not_handled

    const/4 v0, 0x1

    return v0

    :gc2plus_not_handled
    invoke-super {p0, p1}, Ljp/co/taito/groovecoasterzero/JNIActivity;->dispatchGenericMotionEvent(Landroid/view/MotionEvent;)Z

    move-result v0

    return v0
.end method
'@ -replace "`r`n", "`n"
$text = $text.Replace($anchor, ($keyHook -replace "`r`n", "`n")) + $motionMethod

# Song search overlay, attached right after the game's own onCreate.
$createAnchor = "    invoke-super {p0, p1}, Ljp/co/taito/groovecoasterzero/JNIActivity;->onCreate(Landroid/os/Bundle;)V`n"
if (-not $text.Contains($createAnchor)) { throw 'onCreate anchor not found' }
$text = $text.Replace($createAnchor, $createAnchor +
    "`n    invoke-static {p0}, Ljp/co/taito/groovecoasterzero/GC2PlusSearch;->attach(Landroid/app/Activity;)V`n")
[IO.File]::WriteAllText($smali, $text)

# 3. Native libraries.
Copy-Item "$out\libtune.so", "$out\libgcp.so" "$work\lib\arm64-v8a\" -Force

# 4. Rebuild.
java -jar "$root\tools\apktool.jar" b -o "$out\unsigned.apk" $work | Out-Null
if ($LASTEXITCODE) { throw 'apktool build failed' }

# 5. Compile the Java classes into classes2.dex and add it to the APK.
$classes = Join-Path $out 'classes'
$stubs = Join-Path $out 'stub-classes'
$dex = Join-Path $out 'dex'
foreach ($d in $classes, $stubs, $dex) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory $d | Out-Null
}
javac -nowarn --release 8 -d $stubs (Get-ChildItem -Recurse "$root\java-stubs\*.java" | ForEach-Object FullName)
javac -nowarn --release 8 -cp "$androidJar;$stubs" -d $classes (Get-ChildItem -Recurse "$root\java\*.java" | ForEach-Object FullName)
if ($LASTEXITCODE) { throw 'javac failed' }
& "$bt\d8.bat" --min-api 21 --lib $androidJar --classpath $stubs --output $dex (Get-ChildItem -Recurse "$classes\*.class" | ForEach-Object FullName)
if ($LASTEXITCODE) { throw 'd8 failed' }
python -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'a',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'classes2.dex'); z.close()" "$out\unsigned.apk" "$dex\classes.dex"
if ($LASTEXITCODE) { throw 'adding classes2.dex failed' }

# 6. Align and sign.
$ks = Join-Path $root 'gc2plus.keystore'
if (-not (Test-Path $ks)) {
    keytool -genkeypair -keystore $ks -storepass gc2plus -keypass gc2plus -alias gc2plus `
        -keyalg RSA -keysize 2048 -validity 36500 -dname 'CN=GC2Plus' | Out-Null
}
$final = Join-Path $out 'groovecoaster-gc2plus.apk'
& "$bt\zipalign.exe" -P 16 -f 4 "$out\unsigned.apk" "$out\aligned.apk"
if ($LASTEXITCODE) { throw 'zipalign failed' }
& "$bt\apksigner.bat" sign --ks $ks --ks-pass pass:gc2plus --ks-key-alias gc2plus --out $final "$out\aligned.apk"
if ($LASTEXITCODE) { throw 'apksigner failed' }
Remove-Item "$out\unsigned.apk", "$out\aligned.apk"
Write-Host "built $final"
