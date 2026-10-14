# Android beta 2 changes

Candidate version: `1.0.7-android-beta.2`, version code 6, beta application ID
`org.ignusloki.pt.beta`. Based on beta 1 and the local settings/subtitle fixes on
`codex/android-beta-fixes`. The original beta signing certificate is retained.

## Returning from another app

Reproduced on the signed fix.2 APK on the Galaxy S22 SM-S901B / Android 16:
launch the installed game, open Android Settings, then return using Back.
The game process survives but only Android's touch overlay remains visible.
SDL records surface destruction and creation; the Android driver reports that
the old BufferQueue has been abandoned. The original renderer retained its
initial Vulkan surface and did not recover that presentation destination.

The existing Android foreground callback now records pause generations. Rendering
waits while the app is backgrounded or SDL has no native window. On return, the
renderer waits for GPU work, destroys the old swapchain and Vulkan surface, and
creates a surface for SDL's current native window. Presentation semaphores and
frame fences are recreated, then the normal swapchain/target resize path runs.
Game state, textures, audio engine and profile remain loaded. Acquisition and
presentation surface-loss errors also request recovery. Surface capability,
format and present-mode queries now check their Vulkan results.

The lifecycle and synchronization recovery paths are guarded for Android;
desktop presentation keeps its existing resize behavior. Android process
termination by the OS still requires normal startup from the saved profile.

## Phone rumble

The original audio engine already produces the game's authored two-motor motion
levels. Android's virtual touch gamepad previously supplied no rumble output,
and the APK did not declare `android.permission.VIBRATE`.

The touch gamepad advertises a rumble callback when the phone has a vibrator and
the normal vibration permission. JNI forwards those existing motor levels to
Android's vibrator, adapting the stronger motor to one phone amplitude. Phones
with amplitude control use that level; other phones use the system default
amplitude. Effects are bounded to one second and refreshed/stopped by the game's
existing rumble path. The existing Vibration setting applies, physical gamepad
routing remains intact, and phone feedback is cancelled on pause or destruction.
This is Android-only; it does not add Mac-specific feedback.

Physical rumble and the final baby-laugh/puzzle timing are intentionally left to
testers at the user's request. Phone actuator output is an adaptation of the
original controller motors, not identical hardware behavior.

## Candidate checks

Build the committed source with `tools/android/build.py`, the retained SDK/JDK
and `--liborbis` checkout, `--configuration Release`, `--version
1.0.7-android-beta.2`, `--version-code 6` and the existing private signing files.
Keep `AndroidEnableMarshalMethods=false` and `AndroidIgnoreAllJniPreload=true`.
Build outputs belong to this worktree. No original game archives belong in the APK.

Verify the actual signed APK on the S22: install over the existing beta without
clearing data, compare signing/package hashes, and confirm launch, rendering,
repeated other-app/Back transitions, Home/Recents return and a longer background
interval. Confirm the same game process survives, gameplay and menus draw again,
and resolution remains 960×443 with the 30 fps cap. Export a fresh diagnostic log
and retain the local test evidence in `build/android-validation/`.

Vulkan 1.1 compatibility is deferred; the
[compatibility assessment](android-vulkan-1.1-assessment.md) records the current
evidence and possible implementation paths. Beta 2 still requires the current
Vulkan 1.3 features and resource limits. Other phones, OS memory eviction, physical
controllers, fresh import/first boot and sustained performance need wider testing.
