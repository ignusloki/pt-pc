# Mac integration foundation checks

Updated: 9 October 2026. Branch: `codex/upstream-macos-integration`.

## Scope and priorities

The integration was refreshed onto upstream `0e0e147649c05be3b7782f89f55d9e125ce7671e`. Commit `475dd12` adds reconciled Mac packaging and installation, startup microphone permission, and the supplied default Mac icon. Commit `a4aa77c` adds native platform validation workflows. The original baseline and conflict inventory remain historical evidence; the prior baseline checkpoint is preserved as `codex/upstream-baseline-20261009`.

The current order is Mac build/packaging, startup microphone consent, default icons, Intel Mac, then Windows/Linux. MetalFX, remaining window behavior and optional Fast Walk remain follow-up integration work. Upstream's version, updater endpoint, controller behavior, enhanced-texture support and newer voice runtime are retained. The existing tested fork release remains the comparison build.

## Implementation

- One portable app packer: upstream's `tools/macos/make_app.py`. The native Setup packer consumes its ZIP instead of maintaining a second runtime layout. Both use `P.T. PC Port.app`, upstream's bundle identifier and per-architecture archive names.
- Separate ARM64 and Intel presets and installer builds. Release-only Mac targets stage shaders, fonts, voice, MoltenVK, texture tools and licenses. Windows/Linux compiler presets and release upscaler defaults are preserved.
- Native Cocoa installer accepts a complete local fake PKG or decrypted game folder, then a writable destination. The self-contained .NET 10 helper includes matching patched LGPL source and notices. Microsoft runtime packs are selected explicitly rather than inheriting host-specific Homebrew runtime dependencies. Custom extractor inputs can relocate Brotli with its MIT license; all Mach-O files are checked against the macOS 14 minimum and unbundled dependencies are rejected.
- Signed apps are replaced as whole folders. Repairs remove stale/untracked sealed resources, and rollback restores the old bundle. Migration recognizes the older fork's `P.T..app` name. Upstream's writable-data protection, Unicode paths and runtime-validation hooks remain in place.
- AVFoundation requests microphone authorization asynchronously during normal game startup. It respects existing allow/deny decisions; permission does not start recording. Headless execution, WAV voice tests, asset tools and installer verification do not enter that startup path. A bare executable without usage-description metadata skips the request with a log message.
- Both Mac bundles contain `assets/macos/pt.icns` and name it in Info.plist before signing. The original supplied PNG is retained unchanged as `assets/macos/pt-icon.png`.
- The extractor retains the clear diagnostic for a full-size PKG whose inner game data is missing or unreadable.

Apple's [media authorization guidance](https://developer.apple.com/documentation/bundleresources/requesting-authorization-for-media-capture-on-macos) and [asynchronous request API](https://developer.apple.com/documentation/avfoundation/avcapturedevice/requestaccess(for:completionhandler:)) describe the permission behavior. Microsoft's [.NET macOS support table](https://learn.microsoft.com/en-us/dotnet/core/install/macos) includes .NET 10 on macOS 14.

## Completed local checks

Host: Apple M1 Pro, macOS 26.6.2, Apple Clang 21, CMake/Ninja and .NET SDK 10.0.401. Build products are isolated under `/Users/suporte/PT-Mac/build/upstream-integration/`.

| Check | Result |
| --- | --- |
| Native ARM64 release game and Cocoa installer | Build passed |
| Eleven selected CPU/installer regressions | Passed: core, platform, settings, saves, language defaults, graphics presets, voice matching, installer transactions, Unicode paths and user-data migration |
| Voice regression fixture | 68/68 matches; models load from Unicode paths; missing model path reports an error |
| Python archive/signing/source regressions | 16 passed, including deployment target and Microsoft runtime redistribution notices |
| Synthetic extraction fixtures | Passed, including alternate P.T. regions, incomplete inner data and retail/other-game refusal |
| Packaged voice smoke test | All three models and ARM CPU backend load; three seconds of silence yield zero detections |
| Installer self-test | Payload hash, unpacking, signed runtime startup, rollback, Unicode paths and icon helpers passed |
| Real local PKG installation | `PASS installed` from the user's complete US PKG into an isolated fixture folder |
| Repair with no source PKG | `PASS updated`; uses existing usable archives, removes a stale app resource and restores deep/strict signature validity |
| Player-file protection | Test saves/settings/mods and all installed archives retained their hashes; all 14 protected original files unchanged |
| GPU reflection mix | 29 cases passed on the local GPU |
| Packaged first-boot/game startup | Options and gameplay startup passed at 1280×720; screenshots inspected |
| Initial focus/controller route | Nine expectations, zero failures; options, pause and first room render correctly |
| Headless microphone isolation | No startup microphone request logged during packaged headless/voice checks |
| Intel cross-compilation | Separate x86_64 game, installer, CPU variants and self-contained extractor built and packaged successfully |
| ARM64/Intel distribution audits | Correct executable architecture, all three voice models, matching icons, valid deep/strict signatures, all Mach-O minimums at or below macOS 14, no `.pkg`/`.psarc`/`.qar` in distribution ZIPs |
| Official installer runtime | Both final packages use Microsoft's .NET 10.0.12 runtime with its redistribution notices; compression has no external Homebrew/Brotli dependency |
| Workflow syntax/expressions | `actionlint` passed for Mac and desktop workflows |

The GPU checks use isolated settings/save paths and offline audio; they do not grant microphone access or test physical controller hardware. The Intel build was cross-compiled on Apple silicon with target-runtime tests skipped. It demonstrates compilation and packaging, not Intel CPU/GPU gameplay.

## Native platform gates still pending

`.github/workflows/macos.yml` uses native Apple silicon and Intel runners; `.github/workflows/desktop-validation.yml` uses Windows and Linux runners. The [runner labels](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) were checked against GitHub's runner documentation.

The initial push was rejected because the GitHub CLI OAuth login lacked `workflow` scope. Reauthorization subsequently succeeded through the official GitHub device flow. The local permission prompt rejected the next push, and the user explicitly chose to keep the integration branch local. These workflows have therefore not run; their definitions and syntax checks are not passing native platform results.

Windows validation keeps strict FSR/DLSS/XeSS requirements and checks the executable fingerprint plus staged SDK files. The Windows installer resource/link check uses a compile-only payload fixture that is never shipped. Both desktop jobs build the game and installer, exercise shared transactions/Unicode paths, test synthetic extractor input and publish the self-contained helper. Neither CI job proves actual GPU gameplay or installs a real game PKG.

Native Intel voice/backend execution and GPU support, Windows/Linux graphics and controller hardware, and first-launch macOS microphone consent/live input remain manual or native-hardware gates. Use the [preview checklist](upstream-macos-preview-checklist.md). Compilation and headless startup are insufficient grounds to publish this as the next tested fork release.

## Local preview outputs

- Apple silicon: `/Users/suporte/PT-Mac/dist/macos/upstream-integration-preview/`
- Intel cross-build: `/Users/suporte/PT-Mac/dist/macos/upstream-integration-intel-preview/`

Each folder contains `P.T. PC Port.app`, `P.T. PC Port Setup.app`, and separate portable/Setup ZIPs. Versions follow upstream's 1.0.2 default; they are integration previews, not a replacement for the tested fork's 1.0.3 release.

The final packages were built from `8428c2ecf20c0ddca09eaa460f2b71b91d5d5695`. The Apple silicon portable ZIP is 263.8 MiB and Setup ZIP is 298.7 MiB; Intel portable/Setup ZIPs are 269.0/306.0 MiB. SHA-256 hashes are recorded in the package audit. The final runtime also passed a fresh real-PKG install and source-free repair.

Machine-readable evidence: `package-audit.json`, `native-final-install-checks/results.json`, `preview-gpu-checks/gpu-results.json`, and the build/test logs under `/Users/suporte/PT-Mac/build/upstream-integration/`. Original PKGs, game archives and player-profile files are excluded from source control and distribution packages.
