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

The log goes to logcat (`adb logcat -s pt`).

## Playing

On-screen controls (a virtual DualSense the game reads like any controller):

- left half of the screen: a stick where your thumb lands, to walk
- right half: a stick to look around
- **X** interact, **O** back, **ZOOM** (R1), **II** pause, **PC** the port's settings page
- **JACK**: the word the game listens for at the end; phones have no speech recognizer here, so this stands in for it

A Bluetooth controller works too; using it hides the on-screen controls until the screen is touched again.

Settings for phones in `pt.ini` (in the app's data folder): `display.render_scale` (0.25 to 1) draws the 3D view at fewer
pixels than the screen has, the first thing to lower if the frame rate drops; shadows default to 1024 px tiles.

## Notes for developers

- Android's shaders use the gather shadow tap like Apple's (`PT_SHADOW_GATHER`), so the emulator, which runs MoltenVK on a
  Mac host, shows what a phone does. The emulator turns MoltenVK's Metal argument buffers off, which the bindless image
  array needs: start it with an interposer that keeps `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1`.
- When the app returns from the background on Android, the window has a new surface: the renderer makes a new `VkSurfaceKHR`
  when SDL's `ANativeWindow` changes. Nothing is drawn while the app is in the background.
- Lua 5.1 is built without `_FORTIFY_SOURCE` on Android: bionic otherwise aborts in the collector.
