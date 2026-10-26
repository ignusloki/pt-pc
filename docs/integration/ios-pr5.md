# PR #5 source integration

Base: `28a3a11cb29198836f535c0c3625becbefd3e096`, including the unpushed upstream
documentation merge. Branch: `codex/ios-pr5-integration`. PR fetched with
`--no-tags` into `origin/pr5-ios`; unchanged head `7deae4c591691ba7d072699b87d19efaa02b7586`.
Merge base: `61139aa3752a4afbd41318015db77d8084efe9bd`.

Original contributor: **Alex Blount (ilsonb)**. The merge retains the PR commit
ancestry and attribution. iOS packaging, touch overlay and BC decode foundation
are adapted from that contribution; its measurements are not our validation.

| Area | Reconciliation |
| --- | --- |
| Build | Explicit PT_IOS; no MacOS.cmake, installer or MetalFX implementation on iOS. Current Mac/Intel and Android targets remain. Host shader compiler, static iOS SDL/Whisper; dynamic embedded MoltenVK. |
| Voice | iOS static symbols; preserve Mac/Android dynamic library locations, CPU selection and models. Reuse AVFoundation permission API and existing startup/toggle logic. |
| Graphics | Adapt BC decoder to current gpu::Format/image API, decoder-only pinned dependency; no Enhanced Textures restoration. Preserve Android per-format checks and surface recovery. Bound texture mips before decoding. |
| Input/lifecycle | Adapt contributor virtual pad overlay to safe areas, cancellation and real-controller use. Current Android overlay/menu bridge stays. iOS background gating drains pending GPU work and blocks subsequent game frames. |
| Storage | Documents game folder and Documents/data profile. Finder/devicectl prototype; native Files/PKG importer deferred with requirements in mobile guide. |
| Existing behavior | Keep upstream render graph, Mac HDR/fullscreen/MetalFX corrections, Fast Walk, metadata namespace, extras pack and Museum preview. Keep narrow Android Lua FORTIFY compatibility patch. |

No CI/release work, tag fetching, publishing, pushing or merge into the shared
branch. Existing logs, packages, installed apps, credentials and older Android
worktree are outside this change. See [mobile guide](../mobile.md) for build,
Personal Team provisioning and the physical-device checklist.

## Validation

Local checks on 2026-10-26:

- **iOS arm64 device Debug app compiled and linked** using Xcode 27.1's iOS 27.1
  SDK via Ninja, deployment setting iOS 17.0. Bundle:
  `build/ios/pt.app` (235 files at audit). Static Whisper/ggml, dynamic MoltenVK,
  shaders, fonts, two voice models, notices and extras pack are present.
- Bundle audit verifies arm64/device Mach-O platform, microphone usage text,
  file-sharing metadata, required resources, no original game archives, no
  Homebrew/AppKit dependencies and no fork/spawn/system/popen imports.
  SDL's process probe and ggml's optional fork-based debugger path are disabled
  only for iOS; a generated ggml copy retains native backtrace symbols.
- **Current native Apple silicon Mac executable compiled.** Touch controls,
  synchronous lifecycle event-watch ordering, settings round-trip (including
  microphone On/Off and render-scale validation), graphics presets, fullscreen
  mouse mapping and Fast Walk regressions passed. The CPU BC decoder regression
  passed for sRGB, reversed BC2 endpoints, RGB BC1 alpha, BC5 channel layout,
  cropped tails, truncated input and invalid BC7 blocks.
- **19 Mac packaging**, **8 Android device-report** and **4 payload-index** Python
  tests passed. Android launcher/build/signing sources and Mac packaging sources
  are unchanged. The Android Lua compatibility patch remains unchanged.
- Source whitespace/conflict-marker checks and Python helper compilation passed.

**Not verified:** simulator execution, iPhone/iPad installation, provisioning,
interactive microphone consent/live recognition, GPU rendering, touch feel,
resume behavior under UIKit, thermal/memory limits and gameplay on iOS. No Intel
or Android native rebuild/device test was performed for this integration; Mac
HDR/MetalFX GPU gameplay was not rerun.

Xcode generator/provisioning preparation was attempted but the host's
`xcodebuild` fails loading `IDESimulatorFoundation`: a required symbol is missing
from `/Library/Developer/PrivateFrameworks/DVTDownloads.framework`.
It recommends `xcodebuild -runFirstLaunch`; this integration does not repair
system frameworks or change credentials. No usable Xcode project was generated.
The unsigned Ninja bundle is ready for signing once the host toolchain/profile
is working. The device MoltenVK archive has no simulator slice.

Free Personal Team signing steps and seven-day renewal limits are documented in
the mobile guide. User test targets: iPhone 15 Pro Max, iOS 26.6.2, and 12.9-inch
iPad Pro (5th generation). Native Files/PKG extraction remains a separate future
feature; Finder/devicectl transfer of extracted files is the prototype workflow.

Regression targets: `pt_touch_controls_test`, `pt_ios_lifecycle_test`,
`pt_settings_roundtrip_test`, `pt_graphics_preset_test`, `pt_mouse_render_test`,
`pt_fast_walk_test`. The decoder regression can also be enabled for a host CMake
build with `PT_BC_DECODE_TESTS=ON` and built as `pt_bc_decode_test`.

## First physical-device follow-up (2026-10-26)

The user repaired Xcode account authentication and renewed the development
certificate. Xcode project generation and Personal Team provisioning now succeed.
The iPhone 15 Pro Max accepted the signed app and all three extracted archives.
After the user trusted the developer profile, the OS launch succeeded. The user
reports that the game works, with poor performance. This was a short user test,
not a measured frame-rate, full playthrough or microphone validation. The current
candidate is a Debug build; performance work is separate from the icon change.

The app now compiles the Mac forest artwork into iPhone and iPad app-icon assets,
including opaque PNGs and primary-icon metadata for both device families. The
original Mac icon is unchanged. The deployment helper now uses Xcode's explicit
`--timestamp=none --generate-entitlement-der` flags, which corrected the initial
manual development-signing failure. Updates retain the existing bundle ID and
app container; game files and saves are not removed.

The icon-bearing app was installed over the iPhone app and launched successfully,
preserving its game/profile container. A retrieved iPhone log reaches gameplay
and samples 30 fps at the configured 30 fps cap; this is not a broad performance
benchmark or a resolution of the user's performance report.

The connected **iPad Pro 12.9-inch (5th generation, M1), iPadOS 26.6.2** was
provisioned after the user enabled Developer Mode. The icon-bearing app and all
three extracted archives were installed. Device-reported byte sizes match the
Mac source files. After the user trusted the developer account, OS launch
succeeded. Its startup log selects the Apple M1 GPU, finds the game archives,
records microphone permission granted and reaches the first-boot options menu.
Interactive iPad gameplay, touch feel, sustained performance, recognition and
background/resume tests are still for the user to perform.
