# Android Vulkan diagnostic build

Version: `1.0.7-android-beta.2-vkdiag.1`, version code **7**, application ID
`org.ignusloki.pt.beta`, signed with the existing beta certificate. This is a
test APK, not a new published release. It still uses the current Vulkan 1.3 game
renderer; Vulkan 1.1/1.2 gameplay support has not been added.

## Purpose

A beta 2 diagnostic export from a Huawei ADY-LX9, Android 12 / API 31, shows
successful package import and archive loading followed by
`vulkan: no Vulkan 1.3 device with a graphics queue`. That places the failure
before logical device creation and rendering. The export contains no GPU name,
driver, actual Vulkan API version, extension list or feature/limit report.
The reported Vulkan 1.2 version remains unverified by that export. This is a
separate device report from the earlier Snapdragon 870 / Vulkan 1.1 report.

The existing selector rejects physical device API versions below 1.3 before
testing their other capabilities. The same final message can also follow empty
GPU enumeration or lack of a usable graphics/presentation queue. More detailed
evidence is needed to distinguish those cases and assess compatibility work.
See the [compatibility assessment](android-vulkan-1.1-assessment.md).

## Instructions for testers

1. Install this APK as an update over the existing public beta. Keep both the
   beta and prototype installed; do not clear app storage. Previously imported
   files and profiles stay in place, so a repeat PKG import is unnecessary.
2. Tap **Play P.T.** once. On an unsupported device the startup error is still
   expected. Dismiss it and return to the launcher. If the game runs, quit through
   its menu to return to the launcher.
3. Tap **Save diagnostic log**. Save under a new filename identifying this test,
   for example `PT-Android-vkdiag-Huawei.txt`, and send that text file back.
4. Include the phone model and Android/OS build if the system's visible version
   differs from the Android version reported in the export.

The report is collected when the native game reaches Vulkan initialization;
exporting immediately after installing, without tapping Play, will not produce
a fresh GPU report. Save the export before another game launch replaces it.
The APK contains no original game archives.

## Information collected

The game writes `profile/vulkan-capabilities.txt` and the existing **Save
diagnostic log** action includes its last 128 KiB alongside launcher/startup
logs. The report includes its build version, capture timestamp and schema.

- Vulkan loader API and physical device API versions separately; GPU name,
  vendor/device IDs, raw driver version, and extended driver identification
  when exposed by the driver.
- Every enumerated device extension with its revision and enumeration status.
- Individual descriptor indexing, timeline semaphore, scalar layout, dynamic
  rendering, synchronization2 and shader demotion features, plus relevant core
  features and texture-compression family flags.
- Descriptor limits compared with the current 8,192 textures / 64 cube textures
  allocation model; other resource, compute, subgroup and memory properties.
- Graphics/compute queue families, presentation support, surface queries,
  formats and presentation modes when a diagnostic surface can be created.
- Compressed/uncompressed texture and render-target format capabilities, exact
  usage flags, and image-format query results. Format identifiers and flags are
  numeric Vulkan enum values. Unsupported usage combinations are expected data,
  not a conclusion that the complete game is incompatible.
- Exact numeric Vulkan results from fallible queries. Unavailable feature
  queries are labelled `not_queried`; failed/incomplete enumerations are marked
  explicitly instead of turning missing information into a false feature bit.

## Implementation boundaries

The collector runs before the renderer's existing API gate, creates its own
Vulkan instance requesting 1.1 (or 1.0 for a 1.0 loader), and owns its function
pointers. It queries individual feature/property structures only when exposed
by the physical device's core API or enumerated extensions. It never chains the
aggregate Vulkan 1.2/1.3 feature structures into the diagnostic query. The game
continues through its ordinary 1.3 initialization after collection; its volk
dispatch is not replaced by diagnostic dispatch.

The temporary surface/instance are destroyed before normal rendering begins.
If instance creation with window extensions fails, collection retries without
those extensions so GPU capabilities can still be reported. No logical device,
images, shader modules or game resources are created by the collector.
Consequently, a positive capability report is preliminary evidence, not proof
of successful allocation, rendering, correct gameplay or sustained performance.

Collection and the extra native startup messages are Android-only. The startup
error now directs Android players to the launcher's export action. Desktop
startup advice and rendering behavior retain their existing paths. JNI settings
remain `AndroidEnableMarshalMethods=false` and `AndroidIgnoreAllJniPreload=true`.

## Build and checks

Use `tools/android/build.py` with the retained SDK/JDK, pinned `--liborbis`,
`--configuration Release`, `--version 1.0.7-android-beta.2-vkdiag.1`,
`--version-code 7` and the private beta signing files. Keep outputs inside the
Android worktree. Use the committed source required by the Release builder;
do not publish another release as part of this diagnostic build.

`tests/android_vulkan_diagnostics_test.cpp` exercises nine simulated driver
scenarios: Vulkan 1.1 without extensions, 1.1 with feature extensions, 1.2 with
an incomplete enumeration retry, 1.3 with failed presentation queries, instance
retry without surface extensions, failed extension enumeration, a 1.0 loader,
failed GPU enumeration and a missing loader. The tests reject inappropriate
feature chains and logical-device creation, and check temporary object cleanup.

Host test command (substitute the host Vulkan header include path):

```sh
clang++ -std=c++20 -fsanitize=address,undefined \
  -DVK_NO_PROTOTYPES -DPT_VULKAN_DIAGNOSTICS_TEST=1 \
  -I src -I /path/to/vulkan/include \
  tests/android_vulkan_diagnostics_test.cpp \
  src/engine/render/android_vulkan_diagnostics.cpp \
  -o /tmp/pt-android-vulkan-diagnostics-test
/tmp/pt-android-vulkan-diagnostics-test
```

The Android CMake target is `pt_android_vulkan_diagnostics_test` (excluded from
normal builds). Actual older-device results must come from testers. Verify the
signed APK's startup and exported report on the reference S22 before sharing it.
