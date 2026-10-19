# Android beta configuration fixes

Worktree branch: `codex/android-beta-fixes`, based on
`66ec5464d278a08ec5b7f4e4d80fbea8fafbca5e` / `1.0.7-android-beta.1`.
Local test APK version: `1.0.7-android-beta.1-fix.2`, version code 5.
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
The launcher pads its content for system bars and the display cutout after rotation,
so the landscape navigation bar does not cover text or controls.

## Android display settings

The old PC page showed stored desktop window dimensions (960×540 initially, or
2340×1080 after choosing the S22's display mode), while Android still rendered at
960×443. Its desktop display-mode choices could change this stored value without
changing the activity's fixed surface.

Android now shows the live Vulkan surface extent, labels the display Fullscreen,
and disables desktop display-mode/resolution changes with Android-specific notes.
Desktop rows retain their existing behavior. The render width, aspect ratio,
uploaded-mip limit and 30 fps default are unchanged.

## Build and verification

Use Python 3.11+ and `tools/android/build.py` with the retained SDK, JDK and `--liborbis` checkout;
omit `--source-cache` for the fresh build. Keep all outputs in this worktree.
Build Release with the existing private beta signing files, version code 5 and
`--out dist/android/beta-fixes`. Do not publish or push this branch.

Keep `AndroidEnableMarshalMethods=false` and `AndroidIgnoreAllJniPreload=true`.
Compare the signed APK certificate with the installed beta, install over it with
storage preserved, and test the actual signed launcher/game. Verify presets in
both directions, subtitle sizes, launcher rotation/reopening, controls, microphone
capture and diagnostic export. Fresh import and first-boot checks must use a
separate test profile, never clear either existing app's storage.

Other phones, portrait gameplay and sustained performance remain unverified.

## S22 verification — 2026-10-14

The final Release APK was installed over the public beta without uninstalling or
clearing storage, and its launcher and native game ran on the SM-S901B / Android 16.
The installed APK's SHA256 matches the local signed artifact byte for byte.
Its certificate matches the public beta; microphone permission remains granted.
The APK was built from `a832f7a5ec09c7f72356a6507addde722bfd2595`.
Later documentation-only commits do not change that tested binary.

| Check | Result and evidence in `build/android-validation/` |
| --- | --- |
| Presets after reopening | Low remains recognized (`final-preset-persisted.png`). |
| Custom recovery | Changing Shadows to Medium produces Custom; Left then selects Ultra, High, Original and Low correctly. Traced effects remain unavailable (`final-preset-custom.png`, `final-preset-ultra.png`, `final-preset-high.png`, `final-preset-original.png`). |
| Presets in the other direction | Right selects Original, High and Ultra; returning Left restores Low (`final-preset-right-*.png`, `final-preset-low-restored.png`). |
| Resolution | PC Settings shows 960×443; trying to change that disabled row leaves it unchanged. SurfaceFlinger reports 960×443 game buffers (`final-resolution-verified.png`, `final-surfaceflinger.txt`). |
| Frame cap | PC Settings shows 30. The exported native log reports a 30 fps limit and 960×443, with samples mostly at 30 fps and some at 29. This is a settings check, not a sustained performance benchmark. |
| Launcher preferences | Touch controls Off survives force-stop/reopen; microphone On and subtitle size 150% remain selected (`final-touch-off.xml`, `final-touch-off-reopened.xml`). Touch controls were restored to On afterward (`final-launcher-restored.xml`). |
| Orientation and insets | The final launcher renders in portrait and landscape; landscape content clears the navigation bar and cutout (`final-launcher-portrait.png`, `launcher-landscape-verified.png`). |
| Package picker | Select P.T. .PKG opens Android's document picker; cancelling returns to the ready launcher without importing or replacing files (`final-pkg-picker.xml`). |
| Diagnostic export | Save diagnostic log creates a new file, `Download/PT-Android-fixes-final-20261014.txt`. The copied `final-diagnostic.txt` identifies fix.2, touch On and subtitle scale 1.5, and contains no warnings/errors in the exported native tail. |
| Signed package | Version code 5, beta application ID, original signing certificate and matching installed/local APK hashes (`final-installed-package.json`, `dist/android/beta-fixes/BUILD.json`). |

Graphics-preset and settings round-trip tests pass both on the host and as native
Android executables on this S22. The Android Lua startup/collection regression
test also passes. Earlier checks of this fix branch exercised PC Settings column,
direction, OK/Back navigation, gameplay/menu touch layouts and built-in microphone
capture with a live input meter. The user confirmed that the subtitles work.

Both beta and prototype remain installed, with their separate game data and
profiles preserved. The tested configuration was left at Low, volume 100%, touch
and microphone On, subtitle size 150%, and the phone's original automatic rotation
settings were restored. The original host PKG and `game/CUSA01127` remain present;
the main checkout still has only its pre-existing untracked `logs/`.

Fresh import/first boot, a new spoken “Jack” recognition run, the broader
multilingual subtitle regression, full-game progression and desktop release builds
were not rerun. Other devices, portrait gameplay and sustained performance remain
unverified. The changes are local to `codex/android-beta-fixes`; nothing was merged,
pushed or published.
