# Android 1.0.11 — build and install your own APK

This is the first official **Android source release**. No new public APK is
provided for 1.0.11. The last prebuilt download remains
[1.0.10 Android Beta 5](https://github.com/ignusloki/pt-pc/releases/tag/v1.0.10),
with its [existing installation guide](android-beta-installation.md).

## Preserve an existing beta installation

Do not uninstall or clear the beta's storage. It holds imported game files,
settings and saves. Android requires the same app ID **and signing key** to
install an update over it; a higher version code alone is insufficient.

Use a personal application ID for a self-built APK, such as
`com.yourname.pt.selfbuilt`, through the builder's `--application-id` option.
It coexists with the beta in separate storage. Saves are not implicitly migrated.
The source retains the legacy Release default ID `org.ignusloki.pt.beta` for
matching-key builds. Do not request or distribute the maintainer's private key.

## Build on a Mac

Install .NET 10 with the Android workload, Android SDK platform/build-tools 36,
NDK **28.2.13676358**, JDK 21, CMake, Ninja and a host `glslc`. The SDK minimum
for the resulting game is Android 10/API 29, arm64-v8a. Clone the source and the
pinned LibOrbisPkg reader:

```sh
git clone https://github.com/ignusloki/pt-pc.git
cd pt-pc
git checkout v1.0.11
git clone https://github.com/maxton/LibOrbisPkg.git /your/work/LibOrbisPkg
git -C /your/work/LibOrbisPkg checkout 643477263b2644e0803e0f58b8726ea4e3f3b7d4
```

A first development build uses a local debug key and needs no release key:

```sh
python3 tools/android/build.py \
  --sdk /absolute/path/to/android-sdk \
  --jdk /absolute/path/to/jdk-21 \
  --liborbis /your/work/LibOrbisPkg \
  --application-id com.yourname.pt.selfbuilt \
  --version 1.0.11 --version-code 16
```

The development APK is named `PT-Android-Prototype-arm64.apk` under
`dist/android/prototype`. This identifies a development build, not an official
prebuilt binary. Install it manually or with `adb install -r /path/to/the.apk`.
No installation is performed automatically by the builder.

For your own Release build, create and securely retain your own keystore/key
alias using JDK `keytool`, and keep the key password in a private file outside
the source checkout. Add the following options to the command above:

```sh
  --configuration Release --out dist/my-android-release \
  --keystore /private/path/to/your-keystore.p12 \
  --key-alias your-key-alias \
  --password-file /private/path/to/your-key-password.txt
```

Commit tracked source changes first: the Release builder requires a clean
tracked source tree. Retain the same personal application ID and key for your
future updates. Do not put passwords in command arguments, commits or reports.
The builder passes the private password file through an environment reference,
verifies identity/version/signature and 16 KiB native/ZIP alignment, and writes
checksums, build provenance and corresponding LibOrbisPkg source/notices.

## Game import and testing

Supply your own compatible decrypted/fake P.T. PKG through the launcher's
**Select P.T. .pkg** button. No original game archives are included. Keep the app
open through import. The .NET launcher and streaming extractor remain in place;
the PR's separate native APK builder is not used.

Requirements remain Android 10+, ARM64, Vulkan 1.3 with the required features,
descriptor limits and usable BC formats. 8 GB RAM or more is recommended.
The verified hardware baseline is **Galaxy S22 SM-S901B/DS**, Exynos 2200 /
Xclipse 920, Android 16. Official source status does not establish additional
device compatibility, and the absence of new tickets is not extra testing.

Start with Low graphics, 30 fps and original textures. The existing 960-pixel
surface, 1024-pixel mip cap, touch/menu navigation, subtitle sizing, AAudio phone
microphone, diagnostics and foreground/surface recovery remain. HDR, ray queries,
desktop upscalers, frame generation and VR remain unavailable on Android.
The narrow Lua FORTIFY compatibility fix is retained.

Check import, both sticks, menu tabs/columns, microphone On/Off and “Jack” test,
background/resume and save persistence. Use the launcher's diagnostic export for
reports. See the historical beta guide for the full control table and known
limitations; these continue to apply until separately revalidated.
