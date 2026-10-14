# iPhone, iPad and Android

The same renderer and game as on PC, through Vulkan: MoltenVK on iOS and iPadOS, the system driver on Android. As on PC, the
game files come from your own PS4 copy: the `CUSA01127` folder with `chunk1.psarc`, `texture.qar` and `pathid_list_ps4.bin`
(1.3 GB). Nothing of the game is in this repository or in the apps.

Contents: [requirements](#requirements), [tools](#tools), [the game files](#the-game-files), [iPhone and iPad](#iphone-and-ipad),
[Android](#android), [playing](#playing), [troubleshooting](#troubleshooting), [notes for developers](#notes-for-developers).

## Requirements

- iOS / iPadOS 17 or later on an Apple GPU with Metal argument buffers tier 2 (A13 or later). Tested: iPhone 17 Pro Max.
- Android 11 (API 30) or later, arm64, with Vulkan 1.3 and descriptor indexing (recent Adreno, Mali and Xclipse GPUs).
  GPUs without BC texture compression get the textures decoded when they load (more memory, same picture).
  Tested: RedMagic 11 Pro (Adreno 840), the Android emulator on a Mac.
- About 2 GB free on the device: the game files and the app (the Android APK is about 90 MB, most of it the voice models).

## Tools

The build runs on a Mac (the iOS build needs Xcode; the Android build may work elsewhere, but only macOS is tested).

- Xcode (tested with 27), with its command line tools: `xcode-select --install`
- CMake 3.28 or later, Ninja, and `glslc` for the shaders (Homebrew: `brew install cmake ninja shaderc`, or the Vulkan SDK)
- for Android: Android Studio's SDK manager or `sdkmanager` with the packages `platform-tools`, `platforms;android-36`,
  `build-tools;36.0.0` (or later) and `ndk;27.2.12479018` (NDK 27 or later), and a JDK 17 or later for `javac` (Android
  Studio's bundled JDK will do). `build_apk.sh` takes the SDK from `ANDROID_HOME` (default `~/Library/Android/sdk`) and the
  newest build tools and platform there.
- for the game files from a PKG: the .NET 10 SDK (`brew install dotnet`)

The first CMake configure downloads the libraries (SDL, Lua, whisper.cpp, ...) and the voice models (about 250 MB).

## The game files

On Windows, the PC installer (docs/installer.md) extracts them from your P.T. fake PKG. On a Mac, build its extractor and run
it on the PKG; it writes the three files into an empty folder:

    git clone https://github.com/maxton/LibOrbisPkg && git -C LibOrbisPkg checkout 6434772
    python3 tools/patch_liborbis_readers.py LibOrbisPkg
    sed -i '' 's/netcoreapp3.0/net10.0/' LibOrbisPkg/LibOrbisPkg.Core/LibOrbisPkg.Core.csproj
    dotnet build installer/Extractor -c Release -p:LibOrbisSource=$PWD/LibOrbisPkg -o build/extractor
    dotnet build/extractor/PT.PkgExtract.dll <your P.T.>.pkg CUSA01127

## iPhone and iPad

1. Download `MoltenVK-ios.tar` from the [MoltenVK releases](https://github.com/KhronosGroup/MoltenVK/releases) (tested with
   1.4.2) and unpack it. The build embeds its dynamic framework in the app.
2. Configure and build:

       cmake -B build/ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
             -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPT_IOS_BUNDLE_ID=<your bundle id> \
             -DPT_MOLTENVK_IOS=<MoltenVK>/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework
       cmake --build build/ios --target pt

   The bundle id defaults to `org.ignusloki.pt.mobile`; pick your own (for example `com.<you>.pt`): it must be one your
   Apple account can sign.
3. Signing, once: sign in to Xcode (Settings, Accounts) with your Apple ID; a free account works, its profiles last 7 days.
   Let Xcode make a development profile for your bundle id: create an iOS App project in Xcode with that bundle id, pick your
   team under Signing & Capabilities, connect the device and run it once. The profile is then in
   `~/Library/Developer/Xcode/UserData/Provisioning Profiles/` (find it by its id:
   `grep -l <bundle id> ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/*`), and the signing identity is listed by
   `security find-identity -v -p codesigning` (its 40 character SHA-1).
4. On the device: Settings, Privacy & Security, Developer Mode on (it restarts). With a free account, the first launch also
   asks you to trust the developer: Settings, General, VPN & Device Management.
5. Sign, install, copy the game files (the first time) and start, with the device connected and unlocked:

       PT_PROFILE=<profile.mobileprovision> PT_IDENTITY=<SHA-1> PT_GAME=<path to CUSA01127> ios/deploy.sh

   Later installs can leave out `PT_GAME`: the files stay in the app's Documents folder. They can also be copied with Finder
   (the device, Files, P.T.: a `CUSA01127` folder with the three files).

When the 7 day profile of a free account runs out, the app no longer opens: run the Xcode project from step 3 again, then
`deploy.sh` without `PT_GAME`.

## Android

1. Configure, build and package:

       cmake -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_HOME/ndk/<version>/build/cmake/android.toolchain.cmake \
             -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DCMAKE_BUILD_TYPE=RelWithDebInfo
       cmake --build build/android --target pt
       android/build_apk.sh

   `build_apk.sh` writes `build/android/PT.apk`, signed with the debug keystore (`~/.android/debug.keystore`, made if it is
   missing), without Gradle. `PT_ANDROID_PACKAGE=<id> android/build_apk.sh` gives the app another id than the default
   `org.ignusloki.pt.mobile`.
2. On the phone: Settings, About phone, tap Build number seven times; then Developer options, USB debugging on. Connect it
   and allow the computer.
3. Install, copy the game files, and start:

       adb install -r build/android/PT.apk
       adb shell mkdir -p /sdcard/Android/data/org.ignusloki.pt.mobile/files
       adb push CUSA01127 /sdcard/Android/data/org.ignusloki.pt.mobile/files/
       adb shell chmod -R 777 /sdcard/Android/data/org.ignusloki.pt.mobile/files/CUSA01127
       adb shell am start -n org.ignusloki.pt.mobile/org.ignusloki.pt.mobile.PTActivity

   With another `PT_ANDROID_PACKAGE`, use that id in the paths and before the `/` of `am start`. Files `adb push` makes
   belong to the shell user, and some phones (a RedMagic 11 Pro among them) do not let the app read them without the `chmod`.
   Updates only need `adb install -r`; the game files stay.

## Playing

On-screen controls (a virtual DualSense the game reads like any controller):

- left half of the screen: a stick where your thumb lands, to walk
- right half: drag to look around (the view turns with the thumb and stops with it)
- **X** interact, **O** back, **ZOOM** (R1), **II** pause, **PC** the port's settings page

The end listens to the phone's microphone as the PC does: whisper.cpp is built into the game, with the base.en model and the
voice activity model in the app (the PC's larger second opinion model is left out). The phone asks for the microphone the
first time the game listens.

A Bluetooth controller works too; using it hides the on-screen controls until the screen is touched again.

Settings for phones are in `pt.ini` in the app's data folder (iOS: `Library/Application Support/pt-port/pt` in the app's
container; Android: `files/data` in the app's folder): `display.render_scale` (0.25 to 1, 0.5 on phones) draws the 3D view
at fewer pixels than the screen has; `display.fps_limit` is 30 on phones (60 ran an iPhone hot); shadows default to
1024 px tiles.

## Troubleshooting

- **The game is not found**: the app looks for `CUSA01127` in its Documents folder (iOS) or its files folder (Android), and
  says so in its log. On Android check the `chmod` above.
- **iPhone: black screen, or "Unable to launch ... Locked"**: the phone locked; iOS suspends a locked app, and `devicectl`
  cannot start one. Unlock it (while testing: Settings, Display & Brightness, Auto-Lock, Never).
- **iPhone: the app does not open after a week**: the free account's profile ran out (iPhone and iPad, step 3).
- **Logs**: Android: `adb logcat -s pt`. Some phones silence every app's log (`log.tag` is `S`): `adb shell setprop
  log.tag.pt V` (and `DEBUG` for crash reports) brings it back until the phone restarts. iOS: the log is `pt.log` in the
  data folder (`xcrun devicectl list devices` lists the ids):

      xcrun devicectl device copy from --device <id> --domain-type appDataContainer --domain-identifier <bundle id> \
            --source "Library/Application Support/pt-port/pt/pt.log" --destination pt.log

- **Hot or slow**: lower `display.render_scale` in `pt.ini` (0.4, 0.35); `fps_limit` stays 30.

## Notes for developers

- Phones have no command line: `env.txt` in the user data folder sets environment variables (KEY=VALUE lines), and its
  `PT_ARGS` gives command line options (relative paths are in the user data folder), for example
  `PT_ARGS=--log voice.log --voice-listen 60` to try the microphone, or `--voice-test jack.wav`. `PT_DEBUG_PASSES=1` adds the
  passes' GPU times to the status line, `PT_DEBUG_SHOT=shot.png` saves the screen with it, `PT_DEBUG_TOUCH=1` logs touches.
  `ios/deploy.sh` also takes `PT_LAUNCH_ENV='{"KEY":"value"}'` for one launch. `PT_TOUCH=1` shows the touch controls on a
  desktop (the mouse acts as a finger).
- Android's shaders use the gather shadow tap like Apple's (`PT_SHADOW_GATHER`), so the emulator, which runs MoltenVK on a
  Mac host, shows what a phone does. The emulator turns MoltenVK's Metal argument buffers off, which the bindless image
  array needs: start it with an interposer of `setenv` that keeps `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1`
  (`DYLD_INSERT_LIBRARIES` on the `emulator` command itself; `nohup` and other system binaries drop it).
- When the app returns from the background on Android, the window has a new surface: the renderer makes a new `VkSurfaceKHR`
  when SDL's `ANativeWindow` changes, and at start it waits for the first one. Nothing is drawn while the app is in the
  background.
- Lua 5.1 is built without `_FORTIFY_SOURCE` on Android: bionic otherwise aborts in the collector.
