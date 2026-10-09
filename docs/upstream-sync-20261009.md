# Upstream sync and Mac release assessment

Checked on 9 October 2026. Upstream: [LoreanXavier/pt-pc at fa6fabb](https://github.com/LoreanXavier/pt-pc/commit/fa6fabbf24102e43e1de054f472e690b335a1e25).

## Branches and release scope

The fork's GitHub `main` now matches upstream `fa6fabbf24102e43e1de054f472e690b335a1e25`.
The local `main`, `origin/main` and `upstream/main` refer to that same commit.
Both active development branches were merged with this upstream snapshot:

- `codex/macos-arm64-port`: updated from the tested preview 4 commit `877aa3b`; this is the only development branch selected for a push.
- `codex/upstream-macos-integration`: updated locally from `1b320a4`; its startup microphone request, supplied icons and unified packaging remain local.

Local backup branches preserve both pre-sync states. Historical Fast Walk and MetalFX review branches remain checkpoints.
No tag, release asset, existing installation or player profile was replaced.

Upstream source is labelled **1.0.2**, but GitHub still publishes **v1.0.1** as its latest release at the time of this check.
Our existing Mac release remains **1.0.3 / macos-arm64-preview-4**. The source sync deliberately retains that version until a new release is prepared.

## What changed since Mac preview 4

There are 12 upstream commits after the shared `ca60666` ancestor. The upstream delta is 401 files,
18,457 added lines and 2,861 removed lines; many additions are comments and documentation.
The integration branch had already incorporated most earlier updates and needed the two commits after `0e0e147`.

| Area | Change and Mac impact |
| --- | --- |
| Voice recognition | A larger Whisper `small.en` model supplies a second opinion for short utterances that the base model misses. Speech onset/duration handling, diagnostics and worker tuning were expanded. Apple M1, M2/M3 and M4 CPU modules are selected at runtime. Live microphone accuracy and frame-time cost still need manual Mac testing. |
| Rendering | Updated particle/depth handling, frame interpolation, temporal/upscaler rendering and pipeline-cache storage. HDR and Windows DLSS frame-generation work are included in shared source; their presence does not establish Mac HDR support. The fork's MetalFX timeline synchronization remains in place. |
| Display and controls | Supported fullscreen display modes, gamepad look sensitivity, controller capability handling and the controller-trigger voice fallback. The fork's window-transition fixes and optional Fast Walk remain available. |
| Audio and controller feedback | Surround channel-layout support and enhanced vibration options. Wired Sony controller speaker playback and audio-derived DualSense haptics are Windows-specific; these are not claimed as Mac features. |
| Photo mode and museum | Photo crops, filters, native/4K export and panel/preset handling; museum model-camera adjustments. |
| Languages and paths | Czech and Polish translations, Unicode-path checks, writable user-data protections and migration helpers. |
| Updates and desktop builds | Update discovery now uses the GitHub Releases API. Windows/Linux release and installer changes are retained, including the fork's strict Windows upscaler validation. Native Windows/Linux builds were not run during this sync. |
| Mac packaging | Packaging now includes all three voice models and the `.so` CPU modules alongside `.dylib` libraries in `Contents/Resources/voice`. MoltenVK 1.4.2 is pinned/staged for both game targets. The self-contained extractor uses Microsoft's runtime packs and includes redistribution notices; distribution audits reject dependencies requiring an OS newer than macOS 14. |

The stable Mac branch keeps `P.T..app`, `P.T. Mac Setup.app` and `PT-Mac-Setup-arm64.zip`.
Signed apps are still replaced as complete bundles, with rollback and preservation of player files outside the app.
The clearer incomplete-PKG diagnostic is retained.

## Download and memory impact

The second voice model adds approximately **181 MB** of model data. Upstream's
[voice measurements](https://github.com/LoreanXavier/pt-pc/blob/fa6fabbf24102e43e1de054f472e690b335a1e25/docs/formats/voice.md#cost-of-the-second-opinion)
report a voice-test peak working set rising from **190 MB to 659 MB** on its Windows test machine.
Those are upstream measurements, not RAM measurements on this Mac or an Android phone.
The extra model is used while listening or running the microphone test, and deserves a new memory baseline before the Android port.

Our local validation ZIPs are approximately **281 MiB for Setup** and **250 MiB for the portable app**,
compared with preview 4's approximately 105 MiB and 74 MiB. These are local test packages, not published release assets.

## Checks completed on the M1 Pro

- Native ARM64 game, release game and Cocoa installer compiled successfully.
- All **22 selected regressions passed**, covering core/platform, settings/saves, graphics, voice matching, installer transactions, Fast Walk movement/collision, queue handoff, Unicode/user-data handling, display modes, pipeline cache, controller routes, HDR math, audio layout and photo controls. The reflection-mix target passed **29 GPU cases**.
- All **14 stable packaging checks** and the synthetic PKG extraction fixtures passed.
- Signed packaged voice runtime loaded all three models and the M1 CPU backend, processed silence and returned zero detections.
- Installer signed-payload self-test, actual local PKG installation and source-free repair passed. Test saves/settings/mods and archives retained their hashes; all 14 protected original files remained unchanged.
- First-boot options rendered correctly. The startup-focus/controller script passed all **nine expectations** and reached the first room.
- MetalFX Quality, Balanced and Performance initialized the real temporal scaler and timeline queue handoff with Metal API validation enabled. Each also passed the nine startup, gameplay and pause/resume expectations; first-room captures were inspected.
- Refreshed local integration: release game and Cocoa installer compiled; core, voice, settings and installer regressions passed, along with all **16 packaging checks**.

Evidence is under `build/upstream-sync-20261009/`: `regression-results.json`,
`preview-gpu-checks/gpu-results.json`, `native-install-checks/results.json`,
`integration-regression-results.json` and accompanying logs.
Validation packages are under `dist/macos/upstream-sync-20261009/` and carry the pre-commit dirty build identifier.
They must be rebuilt from a clean, tagged commit for a release.

The prior full walkthrough and Intel cross-build results belong to the earlier snapshots; they were not repeated for this update.
Live microphone input, physical controller feedback, fullscreen transitions, HDR hardware and native Intel/Windows/Linux execution remain outside these automated checks.

## Release recommendation

**Prepare a new Mac 1.0.4 / preview 5 before continuing the larger upstream integration**, after a short manual acceptance pass.
This sync changes recognition, rendering, settings and installer payloads enough to warrant a separate tested release.
Keep the local integration's startup microphone request, custom icons and broader platform work as the next milestone.

Before tagging:

1. Confirm a cold launch shows the settings screen, Escape starts play, and existing saves still load.
2. Start with Original graphics, 1280x720 and upscaling off; inspect hallway lighting, mirrors, subtitles and particles. Then test MetalFX Quality and switch quality/window mode/resolution during play.
3. Test a physical controller, normal vibration and Fast Walk off/on. Check pause/resume after losing focus.
4. Open the microphone test and try real speech; check recognition and gameplay responsiveness during the final listening loop. Record Mac memory use there.
5. Check photo mode exports, the new subtitle languages and repair of a test installation.
6. Bump the Mac version, rebuild from the clean release commit, rerun package/signature/installer checks and publish new ZIPs and hashes.

The default updater still points at upstream's releases. It ignores prereleases and nonnumeric tags, so these Mac preview tags do not automatically notify players. A fork-specific notification policy can be addressed separately when preparing the release.
