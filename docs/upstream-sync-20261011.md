# Stable Mac fork refresh — 11 October 2026

The fork's `main` is fast-forwarded from `fa6fabb` to upstream `edb5a85ca51cb7d6701f862150308b30dff80e3a`. The same upstream history is merged into `codex/macos-arm64-port`, starting from `40ffa6800d6f724f55afb49d7560782b22d3c86d`. The Mac branch retains its 1.0.4 version, selected forest icon, startup microphone permission and menu toggle, fullscreen pointer mapping, default-off Fast Walk, accepted PR #2 improvements and tested MetalFX scaler. PR #3's frame generation remains separate.

## What upstream changed

- [`c0af362`](https://github.com/LoreanXavier/pt-pc/commit/c0af36214dd27d78a99f3c7a0b902f7b19422a2d): rendering, audio, controller and setup fixes. Reflections blend in the original encoded color space; bathtub dust/liquid lighting and upscaled droplets/glass receive corrections; distortion resets temporal history to avoid ghost lanterns. Subliminal text brightness is corrected.
- The same update resets audio and unset puzzle state between sessions, corrects the fake-crash loop's sink and overlapping subtitles, disables the borderless resolution selector, hides the menu cursor while using a controller, and shows restart notices for HDR/surround changes. Enabling the speedrun timer during play starts it immediately without treating that as a complete recorded run.
- Controller changes expand Lisa's speaker/haptic events, add player-footstep haptics, adjust limited output gains, select the wired DualSense speaker route, and implement Bluetooth DualSense haptic reports. Physical controller behavior still needs hardware verification.
- Windows setup detects existing shortcut/install destinations, updates in place and refreshes shortcuts/icons. Shared setup preserves the selected game's title picture. Mac automation adds a DMG alongside the portable ZIP; the fork's Apple-silicon-preview release exclusion remains in place. Generated pipeline caches are excluded from desktop preview packages.
- [`edb5a85`](https://github.com/LoreanXavier/pt-pc/commit/edb5a85ca51cb7d6701f862150308b30dff80e3a): the bug-report form asks for `pt.log`; suggestions/questions are directed to Discord.

## Merge decisions and fixes

The only textual merge conflict was the texture loader's includes: the fork's worker-thread include and upstream's image-file includes are all retained. Both Mac packaging paths use `assets/macos/pt.icns`, retaining the user's selected icon.

The upstream save-migration scan followed linked files and discarded directory-read errors. Carrying the already-tested integration fix retains linked-only warnings, avoids offering to move/delete linked-only saves, and leaves unreadable migration pending for retry. Its two regression fixtures are included. Mac profiles continue to use `~/Library/Application Support/pt-port/pt/`.

Regression test translation units keep assertions active under RelWithDebInfo, with compiler-specific flags. Production compile flags are unchanged.

After these resolutions, the complete `src/` and `shaders/` trees match the preserved Intel integration checkpoint. That checkpoint already included these upstream commits; the stable branch and fork main were the branches still behind.

## Validation

Host: Apple M1 Pro, macOS 26.6.2. A separate build directory was used; the paused Intel/ARM integration build directories and installers were retained.

- Apple silicon game, release game, shaders and native Setup compiled.
- 21 CPU/input/settings/save/voice/controller regression suites passed with assertions enabled.
- 14 Python packaging tests passed.
- GPU reflection-mix and Vulkan/Metal submission-order/lifetime checks passed.
- First-boot options/controller test: 9 expectations, zero failures.
- Microphone menu enable/disable and disabled-test behavior: 21 expectations, zero failures.
- MetalFX Quality smoke test: active at 853×480 → 1280×720 over 111 rendered frames.
- Packaged native Setup self-test passed, including payload unpacking and bundle rollback. Both game packaging paths and Setup have the selected icon; dependency/deployment audits and deep/strict signatures passed.
- Mac workflow syntax validation passed. No native Windows/Linux CI or new Intel hardware test was run during this refresh.
- `pt.ini` and both normal save slots retain the hashes recorded before the Intel work. The installed game was retained.

Earlier harness failures are retained in the logs: the sandbox could not expose Metal; a direct unpackaged Setup invocation had no payload; the microphone-menu preset initially started Off although its script expects On. The corrected GPU environment, packaged Setup, and corrected menu preset pass.

The original evidence directory was `/Users/suporte/PT-Mac/build/upstream-sync-20261011/`, and its validation installer was under `/Users/suporte/PT-Mac/dist/macos/upstream-sync-20261011-arm64/`. Those build/distribution folders were removed during the requested cleanup. The fresh installer, passing checks and user-confirmed targeted regression are recorded in [the accepted Apple silicon baseline](apple-silicon-regression-20261011.md). These are local validation packages, not published releases.

## Preserved Intel work

`codex/upstream-macos-integration` stays local at `a32dbc6665668eaf60ea39b99045228674c055b6` in `/Users/suporte/.codex/worktrees/upstream-macos-integration/PT-Mac`. Its executables were built from `ac93ac92f317cc2ab7415f065409b89c770b2158`; the last commit only updates documentation.

Backup: `/Users/suporte/PT-Mac/.local-backups/intel-integration-20261011/`. It contains a verified, complete-history Git bundle, copies of both Setup ZIPs with checked SHA-256 hashes, integration documentation, working notes, selected validation evidence and recovery instructions. The original build/distribution folders were cleared for the fresh Apple silicon baseline; the frozen ZIPs and source checkpoint remain available.

The Intel preview can resume from its existing checklist for the 2019 MacBook Pro. Exact macOS/GPU details and native Intel graphics, live microphone, physical controls and sustained performance remain pending. A new stable Mac release should follow a visual regression check; no tag or release was changed by this refresh.
