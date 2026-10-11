# Mac integration preview — 11 October 2026

The local `codex/upstream-macos-integration` branch now combines current upstream with the tested Mac features and separate Apple silicon/Intel installers. Native Intel gameplay and Windows/Linux CI remain release/PR gates. No integration branch was pushed, no upstream PR was opened, and the stable Mac branch, release packages and installed game were retained.

## Source and behavior

Upstream snapshot: `edb5a85ca51cb7d6701f862150308b30dff80e3a`. Its `c0af362` rendering, controller, audio and setup changes are preserved. The integration previews use upstream's **1.0.2** version and release API endpoint; they do not replace the fork's 1.0.4 preview.

Local checkpoints:

- `80bf394`: merge current upstream, retaining its portable DMG alongside both native Setup architectures.
- `2adca50`: adapt the tested MetalFX scaler, ordered Vulkan/Metal handoff, bounded uploads/texture decoding, fullscreen pointer mapping, microphone controls and default-off Fast Walk to the newer upstream interfaces. Attribution for the MetalFX work is retained.
- `ac93ac92f317cc2ab7415f065409b89c770b2158`: repair the legacy-profile scan before building the final previews.

One Mac app packer serves both portable and Setup packages. The native installer accepts a complete fake PKG or decrypted game folder and a chosen destination. It replaces signed apps as whole bundles, with repair/rollback and legacy `P.T..app` recognition. The selected forest icon is used in the game and builder. All three upstream voice models and the target CPU's backend modules are included, with a self-contained .NET 10 extractor and its matching source/notices.

Normal Mac startup requests microphone authorization when input is enabled and macOS has no existing decision. PC Settings → Sound → Microphone input can disable capture and its test; the choice survives restart. Permission does not start recording. Headless, preview and WAV-test paths remain isolated from startup consent.

MetalFX is an optional scaler with runtime availability checks. Its Objective-C++ source and Metal/MetalFX frameworks are Apple-only; non-Apple builds retain a stub. Fast Walk stays off by default and preserves the original interaction button and game timing. PR #3's MetalFX frame generation is excluded while its older-SDK compatibility concern remains unresolved.

## Regression found during the refresh

The upstream migration preflight skipped a linked-only legacy tree without the warning required by its existing test. It also discarded directory-read errors before writing the migration-completed marker. Both ARM64 and translated Intel checks reproduced the existing failure.

The fix inspects links without following them, preserves linked-only warnings, and leaves unreadable migration pending. Added fixtures verify that linked-only saves neither trigger the move/delete question nor delete the legacy tree, and that restoring read permission permits a successful retry. Upstream's Yes/No migration behavior for ordinary data remains intact.

## Completed checks

Host: Apple M1 Pro, macOS 26.6.2, Apple Clang 21. Intel was cross-built with the macOS 15.5 SDK; this is not an Xcode 16.4/native Intel runner result. Both packages target macOS 14.0. Test translation units retain assertions in RelWithDebInfo.

| Check | Result |
| --- | --- |
| ARM64 game, native installer, extractor and packaging | Passed |
| Intel game, native installer, extractor and packaging | Passed |
| Selected CPU/settings/save/installer/controller regressions | 19 suites passed natively on ARM64 and 19 through Rosetta on x86_64 |
| Voice match fixtures | 68/68 on each architecture; Unicode and missing-model paths passed |
| Packaged voice | All three models loaded; silence processed on ARM64 and translated Intel (SSE4.2 backend) |
| Native Setup self-tests | ARM64 and translated Intel passed, including unpacked extractor startup and bundle rollback |
| Synthetic PKG extraction | Passed, including alternate title IDs, renamed archives, incomplete inner data and retail/other-game refusal |
| Python payload/signing/source tests | 16 passed |
| GPU reflection mix and queue handoff | Passed; repeated handoffs, frame-fence lifetimes and abandoned work covered |
| Packaged first boot and MetalFX startup | Passed; first settings screen inspected; scaler active over 111 frames in the Quality smoke test |
| Complete MetalFX game route | 27/27 ordered milestones, reaching the ending |
| Native bathroom route and photo mode | 8/8 and 5/5 milestones |
| Live Fast Walk and restart persistence | 28 and 4 expectations, zero failures |
| Microphone menu toggle/disabled test/persistence | 21 expectations, zero failures |
| First-boot focus/options/controller route | 9 expectations, zero failures |
| Localization and light culling | Passed |
| Distribution audits | Both target architectures, all voice modules/models, selected icons, Game Mode metadata, deep/strict signatures, contained dependencies and macOS minimum checked |
| macOS and desktop workflow definitions | `actionlint` passed |
| Existing player profile | `pt.ini` and both save slots retained their hashes throughout the isolated checks |

Metal debug validation was enabled for GPU checks; Metal shader validation was also enabled for the Quality smoke test. Direct MoltenVK did not provide the Vulkan validation layer. These runs verify scripted logic and selected rendered captures, not physical controllers, live speech, Intel graphics or release performance.

## Local packages

Final binaries are built from `ac93ac92f317cc2ab7415f065409b89c770b2158`. Later documentation commits do not change those binaries.

| Architecture | Setup ZIP | Size | SHA-256 |
| --- | --- | --- | --- |
| Apple silicon | `pt-port-1.0.2-setup-macos-arm64.zip` | 313,249,118 bytes | `5377145f220a93d203757d78259570c7276d8ffd5ab3c879fc53eb646c814654` |
| Intel | `pt-port-1.0.2-setup-macos-x64.zip` | 320,899,886 bytes | `6e6443c2be2c7e9c1ee558be57c11cb85a2a1dc39814e6d11dccd04d1dde6c06` |

The original build/distribution folders were cleared at the user's request before the fresh Apple silicon regression. Both verified Setup ZIPs in this table are now frozen at `/Users/suporte/PT-Mac/.local-backups/intel-integration-20261011/`, together with the complete-history source bundle and notes. Original game archives are excluded. The runtime is Microsoft's .NET 10.0.12 with its redistribution notices; no external Homebrew dependency is required by the extractor.

Selected historical logs/results, audit records, screenshots and validation scripts were retained under `/Users/suporte/PT-Mac/.local-backups/intel-integration-20261011/validation-evidence/` before the original build directory was cleared. They include earlier failed checks and the later corrected runs.

## Remaining gates

The user will test a 2019 MacBook Pro using the [Intel checklist](intel-mac-test-checklist.md). Its exact GPU and macOS version remain to be recorded. Native Intel graphics, microphone consent/live speech, physical controls and sustained performance require that hardware test. Rosetta is not a substitute.

Windows/Linux release scripts, strict Windows upscaler policy, native installer entry points, updater discovery and voice recognizer implementation remain upstream's. Apple framework dependencies are guarded, shared package layouts pass their fixtures, and native validation workflows are prepared. Those workflows have not run because the user chose to keep the integration branch local. Windows/Linux builds and GPU behavior must be verified before claiming complete cross-platform validation or publishing an upstream PR.

Use the [local PR draft](upstream-pr-draft.md) after the hardware and CI gates pass. The [foundation report](upstream-macos-foundation-checks.md) describes earlier snapshots and remains historical evidence.

## Native Intel work resumed

The user accepted the clean stable Apple silicon installer in a targeted regression on 11 October 2026; see [the baseline record](../apple-silicon-regression-20261011.md). The complete game and shader trees on this integration branch match the accepted `b687754` build. Resuming this checkpoint changes documentation and handoff instructions only. Its earlier integration runtime and packaging validation remains applicable; native Intel gameplay is still pending.

The [native Intel build guide](intel-native-build.md) uses a transferred local source bundle, an x64 .NET SDK and `--arch x64 --version 1.0.4`, with runtime tests enabled. It also documents profile backup and transferring the two completed save slots for targeted Loop Browser checks. No integration push, tag, release or upstream PR is part of this handoff.
