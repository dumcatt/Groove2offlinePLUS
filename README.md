# Groove Coaster 2 : Original Style offline mode

Use my mod to play after the shutdown on Android! Play all the 4max expansion songs!

## Includes :

✅ All songs unlocked from the start (Including arcade only songs, extra difficulty songs!)

✅ All difficulties unlocked (no need to play normal to unlock hard)

✅ 99999 of each item

✅ Can play all songs offline

✅ Fixed delayed ad-lib sound

✅ Don't need root!

✅ Can chose to play on a custom server!

## How to use

- Have ~10gb of storage (to download the song data) on an Android device
- Uninstall Groove Coaster, if you have it installed.
- Download the xapk using [Mediafire](https://www.mediafire.com/file/1g4rub1oi3tnw6x/gc2offlinev4.xapk/file) or [Mega](https://mega.nz/file/GLAxBQKQ#_YNXkyNxYsbPsfkWnUUNZVDqqmjBLkKV8Lo6HsyDDdE) (Can't put it on GitHub, it's too heavy)
- Sometimes, the file will download as an `.apk.zip` file. If that's the case, rename the file to end with `.xapk` only.
- Install the xapk using the zarchiver app
> [!Caution]
> **DON'T** open the app yet. After the install is finished, wait for 2 minutes because a file transfer is still in progress (but not shown on the zarchiver UI). You can check if the transfer is done by going to `Internal storage/Android/obb/jp.co.taito.groovecoasterzero` and refreshing the page by sliding down every few seconds : If the size of files are increasing, then the file transfer is not yet done.

<details>
  <summary><b>If the zarchiver install failed, the OBB data is not found or does not transfer, you get a "Failed to extract OBB" error, or the setup app gives you a "Cannot find obb file" error, click here</b></summary>
  <ul>
    <li>Rename the <code>.xapk</code> extension of the file into <code>.zip</code></li>
    <li>Extract the zip file in its own folder. There should be an <code>.apk</code> file and an <code>Android</code> folder.</li>
  </ul>
      <details>
        <summary><b>If you are on Android 13 or above, click here</b></summary>
    <ul>
      <li>Plug in your phone to a PC so you can see the internal storage of your phone on your PC's file browser</li>
            <li>From your PC, copy and paste the Android folder from the xapk you extracted earlier to the root of your internal storage. When asked to merge folders or replace files, click Yes.</li>
      <li>Follow the next steps based on your Android version :</li>
          </ul>
    </details>
       <details>
        <summary><b>If you are on Android 12 or below, click here</b></summary>
          <ul>
            <li>Copy and paste the Android folder to the root of your internal storage. When asked to merge folders or replace files, click Yes.</li>
          </ul>
    </details>
</details>

- Delete the .xapk file and any archive you might have extracted earlier, to free up space
- Start the new `GROOVE2 Setup` app
- Wait for the load to finish (It should take a minute or two)
- Click the button that sets up the app as you want :
  - If you only want to play songs with complete offline support, and don't care about missions, ranking, and social features, click on `Set up for offline play`
  - If you want to use a third party server (or your own) that has support for missions, cloud saves, ranking, etc, click on `Set up with a third party server`. Then enter the URL of a third party server.

> [!TIP]
> As of the time I'm writing this, one public server is `https://api.gc2mrp.top/`
  
- Click on the 1st button to save the APK file for the game. Allow unknown sources, and if needed click the button again to install the game.
 - Open the file in your phone's file browser, install the game and enjoy!

## GC2Plus edition

This build of the setup app (shown as `Version 4 + GC2Plus` on its screen) installs a game APK with the GC2Plus mod built in:

- **FAST / LATE popups**: after each judged note, shows whether you hit early or late.
- **AUTO button**: the mode screen's former REPLAY button now plays the chart automatically on every difficulty. Autoplay results are **not saved** (no score, clear or rank is recorded).
- **High framerate render interpolation**: the game still runs its logic at 60 Hz, but on 90/120/144 Hz displays the frames in between are extrapolated, so notes and the avatar move smoothly.
- **Song search box**: a search bar on the song list screen (in both the 3D route view and 2D LIST view) filters songs live by title or artist as you type.
- **All avatars unlocked & avatar crash fix**: fixes the original game's crash when selecting avatars #256 and above (enlarges `TuneAppMain`'s 256-slot avatar table to 1024 entries so it no longer overflows into the item/skin tables) and makes all 368 avatars selectable without modifying the save file.
- **Gamepad controls**, modelled on Groove Coaster Wai Wai Party's standard scheme. Each half of the pad is one "booster":
  - Left booster: D-pad, left stick, L1/L2
  - Right booster: face buttons (top/right/bottom/left = up/right/down/left), right stick, R1/R2
  - Any button on a side is a HIT (hold for HOLD, mash for BEAT). A directional or face button also pushes toward its direction, which performs a SLIDE. Tilting a stick drags (SLIDE / SLIDE HOLD, waggle for SCRATCH). Use both sides together for CRITICAL / DUAL HOLD / DUAL SLIDE.
  - The pad only drives gameplay while a stage is on screen. Menus work as usual, and you can play without touching the screen.
- **Options file** at `/sdcard/Android/data/jp.co.taito.groovecoasterzero/files/gc2plus.ini`. Create it if you want to change the defaults, one `key=value` per line:

  ```ini
  ; Live-reloaded options (picked up within ~1 second while running):
  autoplay=0     ; 1 = autoplay every stage (not saved), default 0
  fastlate=1     ; 0 = hide FAST/LATE popups, default 1
  interpolate=1  ; 0 = disable high framerate render interpolation, default 1

  ; Startup options (read once when the game launches):
  gc2plus=1      ; 0 = disable GC2Plus completely and run unmodified, default 1
  autobutton=1   ; 0 = keep original REPLAY button instead of AUTO on all difficulties, default 1
  allavatars=1   ; 0 = don't unlock all avatars, default 1
  ```

### Installing GC2Plus if you already have the mod

> [!WARNING]
> The GC2Plus setup app and game are signed with a different key than the original release, so Android refuses to install them over it. **You must uninstall the existing `GROOVE2 Setup` / Groove Coaster app first.** Uninstalling deletes `Android/data/jp.co.taito.groovecoasterzero` and `Android/obb/jp.co.taito.groovecoasterzero`, which hold your extracted song data and local save. **Back those folders up first (for example, copy them to a PC) or they are gone.**

1. Back up `Internal storage/Android/data/jp.co.taito.groovecoasterzero` and `Internal storage/Android/obb/jp.co.taito.groovecoasterzero` if you want to keep your save.
2. Uninstall the existing app.
3. Put the OBB files back in `Internal storage/Android/obb/jp.co.taito.groovecoasterzero`, either from your backup or from the `.xapk` (rename it to `.zip`, extract it and copy its `Android` folder, see the manual steps above). Don't install the `.xapk` with zarchiver: the setup APK inside it is the original, non-GC2Plus one.
4. Install `groove2-setup-gc2plus.apk`, then follow "How to use" from `Start the new GROOVE2 Setup app` onward. The game APK it saves is the GC2Plus one.

### Building the GC2Plus edition

Requirements:
- Android SDK (build-tools `36.0.0`, platform `android-37.0`, NDK `27.2.12479018`) under `%LOCALAPPDATA%\Android\Sdk`
- JDK and Python 3 (`pip install pyelftools`) on `PATH`
- [Apktool](https://apktool.org/) JAR placed at `GC2Plus\tools\apktool.jar`
- Unmodified Groove Coaster 2 APK placed at `GC2Plus\original\groovecoaster.apk`

Build steps (from the repository root):

1. Ensure `GC2Plus\original\groovecoaster.apk` contains the **unmodified** game APK and `GC2Plus\tools\apktool.jar` is present. The build scripts read only `GC2Plus\original\groovecoaster.apk`, never `app\src\main\assets\groovecoaster.apk`, so rebuilding never re-patches an already-modded APK.
2. Build the modded game APK:
   ```powershell
   powershell -File GC2Plus\build_apk.ps1
   ```
   This produces `GC2Plus\out\groovecoaster-gc2plus.apk` (signed with `GC2Plus\gc2plus.keystore`, generated automatically on first run).
3. Copy the modded game APK into the setup app's assets:
   ```powershell
   Copy-Item GC2Plus\out\groovecoaster-gc2plus.apk app\src\main\assets\groovecoaster.apk -Force
   ```
4. Build the setup app:
   ```powershell
   .\gradlew.bat assembleRelease
   ```
   This produces `app\build\outputs\apk\release\app-release.apk`. Release builds are signed with `GC2Plus/gc2plus.keystore` (alias `gc2plus`, password `gc2plus`) by default. To use another key, create an uncommitted `keystore.properties` in the repo root with `storeFile`, `storePassword`, `keyAlias` and `keyPassword`. **The setup app and the game APK must use the same key**, because the game installs as an update over the setup app (same package `jp.co.taito.groovecoasterzero`).
5. Copy `app\build\outputs\apk\release\app-release.apk` to `GC2Plus\out\groove2-setup-gc2plus.apk` for distribution, or repackage the full `.xapk` with:
   ```powershell
   python GC2Plus\tools\make_xapk.py gc2offlinev4.xapk GC2Plus\out\groove2-setup-gc2plus.apk GC2Plus\out\gc2plus.xapk
   ```

> [!NOTE]
> This source code is for the apk only. All the data is stored in .obb files that are too big to put on GitHub. The xapk includes :
> - The game's OBB file. It gets edited depending on which server you set up the app with
> - The data OBB files. It should contain the `resource` and `savedata` folders at the root with the appropriate files in them to play offline. To get these files, download the .xapk from the link above, rename it as .zip, extract it and check the Android folder
