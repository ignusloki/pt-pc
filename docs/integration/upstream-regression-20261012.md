# Upstream integration test build — 12 October 2026

This local preview integrates upstream `61139aa3752a4afbd41318015db77d8084efe9bd` into the fork's v1.0.6 Mac source on `codex/upstream-integration-20261012`. Packages use **1.0.7-rc1**, macOS 14 minimum, and separate Apple silicon and Intel architectures. The stable branch and published release remain unchanged while gameplay is checked. Spatial-audio research remains on `codex/spatial-audio-research`.

## Changes and repairs

- Adopt the upstream renderer/MetalFX submission interfaces as a coherent implementation. Keep the fork's asynchronous uploads, bloom optimization, occlusion/filter skips and sampled timing diagnostics.
- Release the Vulkan continuation if a MetalFX pass fails before submission. Prevent a second external split from overwriting the current frame's continuation.
- Match all two-channel motion-blur targets to their pipelines; the first merged graphics test exposed two remaining four-channel targets.
- Compose generated-frame UI into a per-slot floating-point target before converting to the swapchain format. Copy MetalFX inputs without output transforms, apply brightness/grain once after UI composition, and do not draw UI twice when repeating a frame. The native frame-generation check exposed the original direct-to-swapchain format mismatch.
- Retain native PKG/folder Setup apps, forest icons, startup microphone consent, saved microphone switch, fullscreen mouse mapping, optional Fast Walk and profile migration.
- Keep background microphone state alive until its mutex unlocks, retain cancelled workers, and join them before destruction. Rapid cancel/retry cannot start overlapping driver opens.
- Resolve annotated release tags to commits and validate both Setup packages alongside portable archives before publishing. No publishing action was run.
- Keep crash/log sharing opt-in and identify LoreanXavier as its recipient. No report was uploaded.
- Include upstream mod/model support, Museum/photo controls, HDR/motion-blur work, gouge/X-mark/Lisa fixes and saved Game+ announcement state.

## Automated validation

Apple silicon uses the current SDK; Intel compiles against macOS 15.5 SDK with deployment target 14. CPU checks run natively on Apple silicon and through Rosetta for Intel. Rosetta does not validate an Intel GPU.

- Game and graphical Setup compile for both architectures.
- CPU suites cover settings, save load/write/reset, installer transactions, paths/migration, input/Fast Walk, display modes, voice, asynchronous microphone lifecycle, audio channels, Museum camera and caches.
- Actual Vulkan/Metal queue-handoff regression passes, including abandoned/repeated splits and frame-slot reuse.
- Packaging tests: 16 passed; publishing validation: 8 passed; upstream motion-blur checks: 7 passed.
- Headless game routes pass for gouge respawn from the browser, selecting the previous gouge loop, final-loop X-mark boundaries and Lisa look behavior.
- A copy of the existing finished profile loads with one completion and all 18 loops unlocked. Unit fixtures separately verify the old finish word and the new packed announcement field. Live save slots and settings match their pre-test hashes.
- Metal validation checks pass for normal MetalFX Quality and an injected failed upscale pass. Screenshots show the initial options screen, PC settings, microphone switch on the Sound page and the first room; both exit normally with all 11 scripted expectations met.

- Native HDR selects a floating-point HDR swapchain and passes the same startup/menu/exit route under Metal validation.
- The corrected MetalFX frame-generation route runs for 96 seconds and queues 1,532 interpolated frames, including camera movement, startup/menu UI and clean shutdown. All 11 scripted expectations pass; this validates queue/format behavior, not subjective interpolation quality or performance with validation disabled.

The graphical-package builder additionally checks the extractor fixtures, packaged voice runtime, architecture/dependency limits, signatures and native Setup self-test. Physical Intel gameplay and Windows/Linux CI remain required before claiming those platforms validated for this update.

## Profile and rollback

The pre-test profile has one completed game, all **18/18 loops** and **72/87 Museum entries**. Finishing the game does not automatically unlock every Museum entry. Its missing entries are subliminal images 5–10; radio recordings 01, 02, 03, 05, 06, 07 and 08; riddle 2; and fetus 1. The audit did not alter unlock flags.

A verified local ZIP and extracted copy preserve both save slots and settings. The new save format packs the Game+ announcement tier into the finish word. For a rollback, quit the game and restore both original slots and `pt.ini` together, following the backup's `RESTORE.txt`.

## Manual regression checklist

Use the **graphical Setup ZIP** for your CPU. Unzip it, open `P.T. PC Port Setup.app`, select your PKG or decrypted folder, and install in a separate test location. The package contains no game assets. The normal game profile is shared across installations, so preserve the supplied backup before normal play.

Start with **Original graphics**, **1280 × 720**, **30 FPS**, **HDR Off**, **Enhanced textures Off**, **Frame generation Off**. On Apple silicon, use **MetalFX / Quality** and **Reflections On** for the rendering comparison. On Intel, start with **Low**, **Upscaler Off**, **Reflections Off**; then raise settings gradually. MetalFX can be unavailable on Intel GPUs; frame interpolation requires supported Apple silicon and macOS 26+.

- [ ] Setup and the game show the forest/P.T. icon; installation and reopening find the selected game files.
- [ ] Existing completion, all loops and Museum entries remain available. Resume works, then use the loop browser for targeted checks.
- [ ] A fresh profile shows the brightness/subtitle settings screen. Existing microphone permission is respected; with no prior OS decision, an enabled microphone requests access during startup.
- [ ] PC Settings → Sound: microphone On/Off works, microphone test hears speech, and the final-loop voice puzzle works with your intended headset/microphone.
- [ ] Windowed/fullscreen mouse hover and clicks align; controller navigation and optional Fast Walk work.
- [ ] Loop 6: flashlight pickup, bathroom, mirror and reflections. Loop 8: gouge puzzle, replay and respawn. Later loops: X-mark state, Lisa, maze and moving flashlight.
- [ ] Museum navigation, transcripts and model lighting; photo mode including the ending; the Game+ notice appears once for the appropriate tier.
- [ ] Repeat a hallway/mirror/maze comparison with Upscaler Off and MetalFX Quality where available. Check moving-camera blur and UI clarity.
- [ ] On a supported Mac, enable MetalFX frame generation; play for several minutes, pause/resume, and change window/resolution mode. Watch for freezes, flicker or UI smearing. Turn it Off and compare.
- [ ] With an HDR display, enable Native HDR and compare the hallway and settings UI, then switch back to SDR. Do not use the original HDR/frame-generation combinations as a performance baseline.
- [ ] Quit/reopen and check progress/settings again. For a problem, record the CPU/GPU, macOS version, loop, graphics settings and `pt.log`.

A full first-to-last replay is optional for this preview. The completed profile and loop browser cover the changed scenes; add a short fresh-profile startup check to cover first boot.
