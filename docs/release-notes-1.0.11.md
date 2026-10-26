# P.T. PC Port 1.0.11 — Android official release and iOS Beta 1

This release adds the reconciled iPhone/iPad port from Alex Blount (**ilsonb**)'s
[PR #5](https://github.com/ignusloki/pt-pc/pull/5) to the shared Mac/Android source.
Android moves from Beta 5 to its first official **source release**. No new public APK is supplied. iOS remains **Beta 1**:
it works in short tests on our iPhone and iPad, but performance needs optimization.

## Downloads

- Apple silicon Mac: `P.T.PC.Port.Setup-macOS-arm64.zip` or the portable
  `P.T.PC.Port-macOS-arm64.zip`.
- Intel Mac: `P.T.PC.Port.Setup-macOS-x64.zip` or the portable
  `P.T.PC.Port-macOS-x64.zip`.
- Android and iOS: use the tagged source plus the build/signing guides below.
  **No new APK or IPA is included.** The previous Android Beta 5 download stays
  on the 1.0.10 release for existing users.

Mac packages require macOS 14+ and a compatible Metal GPU. They remain ad-hoc
signed, without Apple notarization; follow the [Mac opening instructions](macos.md).
Use Setup over an existing installation to preserve game files and profiles.
No original P.T. game archives are included on any platform.

## iOS Beta 1 — build and sign it yourself

- iPhone/iPad app bundle with embedded MoltenVK, on-screen movement/look and menu
  controls, safe-area placement, interrupted-touch release and lifecycle handling.
- CPU-only Whisper linked statically for iOS, startup microphone permission and
  the saved microphone toggle. Live recognition still needs broader device tests.
- BC texture decoding for GPUs without BC support, a 1024-pixel texture mip cap,
  half-resolution rendering, smaller bloom targets and a default 30 fps cap.
- The same forest app icon used on Mac, compiled for iPhone and iPad.
- Finder/devicectl transfer of a user's own extracted game archives. Native
  Files/PKG selection and in-app extraction are not implemented.

**Distribution is source/build instructions, not a universally installable IPA.**
Use the [iOS beta guide](mobile.md) with Xcode and your own signing account. A free
Personal Team can install on registered personal devices, with seven-day profile
renewal. We do not distribute our development profile or signing credentials.
No original P.T. archives are included in the source or runtime dependencies.

Tested by the maintainer/user: **iPhone 15 Pro Max** and **12.9-inch iPad Pro,
5th generation (M1)**, on **iOS/iPadOS 26.6.2**. Both were reported working,
with unsatisfactory performance. These were Debug development builds and short
interactive tests, not full-playthrough or minimum-device certification.
Optimization is deferred; this release does not claim a performance fix. The
contributor's other device reports remain separate evidence.

## Android — first official source release

The source's Release launcher is now named **P.T. Android**, with version code
**16**. Users build and sign their own APK. The last public prebuilt Android
package remains **1.0.10 Android Beta 5**; it is not renamed or republished as
1.0.11. Official status describes the Android source release, not a newly
verified official APK.

Existing beta users: keep the beta installed to preserve game files and progress.
A build signed with a different key cannot update it, even with a higher version
code. The build helper accepts `--application-id` so a self-built app can coexist
under a personal ID. It has separate storage and does not migrate saves
implicitly. The legacy default ID `org.ignusloki.pt.beta` and signature-dependent
update compatibility remain available to builds made with the matching key.
Never share private signing keys or uninstall the beta merely to switch keys.

Keep direct PKG import, private game/profile storage, touch/menu navigation,
subtitle sizing, phone-microphone capture through AAudio, Vulkan diagnostics,
texture-memory limits and foreground/surface recovery. The narrow Lua FORTIFY
compatibility fix stays in place; the PR's alternative native APK builder is not
used. Enhanced Textures remains removed.

The previous **Galaxy S22 SM-S901B/DS, Exynos 2200/Xclipse 920, Android 16** test
record remains the hardware baseline. Requirements remain Android 10+, ARM64,
Vulkan 1.3 and the required features/limits; 8 GB RAM or more is recommended.
Official status does not imply support for every Vulkan phone. The lack of new
Android issue reports is not additional device or gameplay verification.
See the [Android installation and testing guide](android-installation.md).

## macOS and shared behavior

Preserve Apple silicon/Intel packaging, Mac HDR/MetalFX corrections, fullscreen
pointer alignment, microphone consent/toggle, optional Fast Walk, metadata JSON
namespace isolation, extras.pak packaging and the Museum X-photo preview fix.
iOS-specific build and voice changes do not replace the tested Mac/Android
runtime-library/model-selection paths.

## Validation and release assets

The integration passed native Apple silicon compilation, focused touch/lifecycle,
settings, graphics-preset, mouse, Fast Walk and decoder regressions, plus Mac
packaging, Android device-report and payload-index checks. The iOS bundle audit
checks device architecture, frameworks, resource contents and both icon families.
Simulator execution and a full iOS playthrough have not been verified. Native
Intel gameplay and wider Android hardware coverage have not been rerun.

Final package checksums and build provenance accompany the downloadable artifacts.
Only artifacts actually rebuilt for this source tag are labelled 1.0.11; earlier
release binaries must not be renamed or presented as fresh builds. Original game
files, installed apps, saves and private signing material are not release assets.
