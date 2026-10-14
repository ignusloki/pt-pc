# Android beta configuration fixes

Worktree branch: `codex/android-beta-fixes`, based on
`66ec5464d278a08ec5b7f4e4d80fbea8fafbca5e` / `1.0.7-android-beta.1`.
Local test APK version: `1.0.7-android-beta.1-fix.1`, version code 4.
This is an in-place, release-signed test update to `org.ignusloki.pt.beta`;
it is not a published release. The prototype app keeps its separate files/profile.

## Graphics presets

Reproduction: open PC Settings → Graphics, change a graphics value so the
preset reads Custom, then press Left on Preset. The desktop Ultra preset requests
enhanced textures, which Android immediately disables. Detection therefore reads
Custom again, and each Left repeats Ultra instead of reaching High/Original/Low.
The S22 advertises ray-query support, although the beta does not enable those
features on its Vulkan device, so Ultra also stored unavailable ray-tracing values.

Preset application and detection now share the texture capability. Android's
ray-tracing capability reports unavailable, and stale traced settings are disabled
at startup. All four presets use the available raster techniques and original
textures, and enhanced textures cannot be enabled from Android's menu.
Desktop capability defaults retain their existing behavior.

## Subtitle readability

The original subtitle metrics shrink with the 1280×720 UI canvas onto the S22's
960×443 surface. The original border technique also makes partially covered glyph
pixels opaque, which is conspicuous at this small size.

The launcher offers 100%, 125%, 150%, 175% and 200% subtitle sizes. Android starts
at 150%; desktop retains 100%. The choice is saved as `[display] subtitle_scale`
in the existing profile. Larger text reflows inside the original subtitle area,
scales spacing with glyphs, and keeps the existing letterbox/fade handling.
Android's border technique preserves fractional coverage; desktop shading is
unchanged. The render surface, texture limit and frame cap remain unchanged.

## Launcher orientation and preferences

Reproduction: uncheck Basic touch controls, close/reopen the beta, and observe it
checked again. The old launcher hard-coded `Checked = true` and requested portrait.

Touch controls are saved in the app's launcher preferences. Microphone and subtitle
choices are saved when changed and refreshed from the game profile on return.
The scrollable launcher follows the phone's orientation settings. Gameplay retains
its tested landscape orientation and controls.

## Build and verification

Use `tools/android/build.py` with the retained SDK, JDK and `--liborbis` checkout;
omit `--source-cache` for the fresh build. Keep all outputs in this worktree.
Build Release with the existing private beta signing files, version code 4 and
`--out dist/android/beta-fixes`. Do not publish or push this branch.

Keep `AndroidEnableMarshalMethods=false` and `AndroidIgnoreAllJniPreload=true`.
Compare the signed APK certificate with the installed beta, install over it with
storage preserved, and test the actual signed launcher/game. Verify presets in
both directions, subtitle sizes, launcher rotation/reopening, controls, microphone
capture and diagnostic export. Fresh import and first-boot checks must use a
separate test profile, never clear either existing app's storage.

Other phones, portrait gameplay and sustained performance remain unverified.
