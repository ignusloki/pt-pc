# iPhone, iPad and Android

The same renderer and game as on PC, through Vulkan: MoltenVK on iOS and iPadOS, the system driver on Android. As on PC, the
game files come from your own PS4 copy (`CUSA01127`: `chunk1.psarc`, `texture.qar`, `pathid_list_ps4.bin`).

## Requirements

- iOS / iPadOS 17 or later on an Apple GPU with Metal argument buffers tier 2 (A13 or later).
- Android 11 (API 30) or later, arm64, with Vulkan 1.3 and descriptor indexing (recent Adreno, Mali and Xclipse GPUs).
  GPUs without BC texture compression get the textures decoded when they load (more memory, same picture).

## Building

iOS (needs the dynamic `MoltenVK.framework` from the official MoltenVK release, `MoltenVK-ios.tar`):

    cmake -B build/ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DPT_MOLTENVK_IOS=<MoltenVK>/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework
    cmake --build build/ios --target pt

`ios/deploy.sh` signs `build/ios/pt.app` with a development profile, installs it on a connected device, copies the game files
into the app's Documents folder (`PT_GAME=<path to CUSA01127>`) and starts it. The game files can also be copied with Finder:
the device, Files, P.T., a `CUSA01127` folder.

Android (NDK 27 or later):

    cmake -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
          -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build/android --target pt
    android/build_apk.sh    # build/android/PT.apk, signed with the debug keystore; no Gradle

Copy the game files to the app's folder, then start it:

    adb install -r build/android/PT.apk
    adb push CUSA01127 /sdcard/Android/data/com.blounty.pt/files/
    adb shell chmod -R 777 /sdcard/Android/data/com.blounty.pt/files/CUSA01127

(files `adb push` makes belong to the shell user; some phones, a RedMagic 11 Pro among them, do not let the app read them
without the `chmod`)

The log goes to logcat (`adb logcat -s pt`). Some phones silence every app's log (`log.tag` is `S`):
`adb shell setprop log.tag.pt V` (and `DEBUG` for crash reports) brings it back until the phone restarts.

## Playing

On-screen controls (a virtual DualSense the game reads like any controller):

- left half of the screen: a stick where your thumb lands, to walk
- right half: drag to look around (the view turns with the thumb and stops with it)
- **X** interact, **O** back, **ZOOM** (R1), **II** pause, **PC** the port's settings page
- **JACK**: stands in for saying the word the game listens for at the end (for a quiet room)

The end listens to the phone's microphone as the PC does: whisper.cpp is built into the game, with the base.en model and the
voice activity model in the app (the PC's larger second opinion model is left out). The phone asks for the microphone the
first time the game listens.

A Bluetooth controller works too; using it hides the on-screen controls until the screen is touched again.

Settings for phones in `pt.ini` (in the app's data folder): `display.render_scale` (0.25 to 1, 0.5 on phones) draws the 3D
view at fewer pixels than the screen has; `display.fps_limit` is 30 on phones (60 ran an iPhone hot); shadows default to
1024 px tiles.

## Notes for developers

- Phones have no command line: `env.txt` in the user data folder sets environment variables (KEY=VALUE lines), and its
  `PT_ARGS` gives command line options (relative paths are in the user data folder), for example
  `PT_ARGS=--log voice.log --voice-listen 60` to try the microphone, or `--voice-test jack.wav`. `PT_DEBUG_PASSES=1` adds the
  passes' GPU times to the status line, `PT_DEBUG_SHOT=shot.png` saves the screen with it, `PT_DEBUG_TOUCH=1` logs touches.

- Android's shaders use the gather shadow tap like Apple's (`PT_SHADOW_GATHER`), so the emulator, which runs MoltenVK on a
  Mac host, shows what a phone does. The emulator turns MoltenVK's Metal argument buffers off, which the bindless image
  array needs: start it with an interposer that keeps `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1`.
- When the app returns from the background on Android, the window has a new surface: the renderer makes a new `VkSurfaceKHR`
  when SDL's `ANativeWindow` changes. Nothing is drawn while the app is in the background.
- Lua 5.1 is built without `_FORTIFY_SOURCE` on Android: bionic otherwise aborts in the collector.
