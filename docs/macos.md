# macOS (Apple Silicon)

This is an experimental native arm64 port for M-series Macs running macOS 14 or newer.
The runtime, Cocoa installer and packaging tools have been built and tested
on an M1 Pro running macOS 26.6.2, including the full scripted walkthrough.
The preview 4 test build has also passed the user's manual gameplay checks.
Microphone and physical controller input, and other Mac/OS combinations still
need further validation.

Apple Silicon preview 4 uses version 1.0.3 and adds Apple MetalFX temporal
upscaling from [ginnfx's PR #1](https://github.com/ignusloki/pt-pc/pull/1), together
with timeline semaphore synchronization and macOS fullscreen/resolution fixes.
Upscaling is off by default. Open PC settings > Upscaling, select Apple MetalFX
and start with Quality. Native provides anti-aliasing at the display resolution;
Balanced and Performance lower the internal rendering resolution further.
The timeline handoff regression is included in the Mac build script's checks.

Built-in Fast Walk is retained.
It is off by default; enable PC settings > Extras > Fast walk, then hold either
Shift key or the controller's bottom face button (Xbox A / PlayStation X) for
50% faster walking. Release for normal speed. The option is saved and takes
effect immediately. See [Fast Walk](fast-walk.md) for behavior and checks.

Mac validation (7 October 2026): the native preset builds with Apple Clang 21,
CMake 4.4.4, MoltenVK 1.4.2 and Microsoft's arm64 .NET 10.0.401 SDK. All 14
synthetic packaging checks, nine CPU/platform test targets, 29 reflection-mix GPU
cases and nine C# extraction checks pass. The signed runtime processes the voice
smoke WAV, and the installer self-test passes with the actual signed payload.
A local US fake PKG installs successfully, and its opening scene renders at
1280x720 through MoltenVK with a normal exit. The installed executable passes
all 15 early walkthrough checkpoints and all 27 full walkthrough checkpoints,
reaching the ending. Opening the installed app normally also finds its adjacent
game data, initializes an audio device and renders the opening scene. These
scripted checks do not establish manual visual/audio quality or microphone input.
Local build and startup logs are under `build/macos-local-checks/`; the release's
`voice-smoke.log` and `installer-self-test.txt` retain the packaged runtime results.
After old builds are cleaned, retained Mac validation logs and captures are stored
under the final release's `validation/previous-checks/`, keeping their original
relative paths.

After integrating the lighting and Arabic font fixes from PR #4, the rebuilt
installer passes its signed-payload self-test and updates the existing app while
preserving all checked game archives, settings and save files. The multilingual
test passes for all five added languages, including Arabic. The updated installed
app again passes the early (15/15) and full (27/27) walkthroughs. Logs for this
integration are saved as `graphics-fixed-*` under `build/macos-local-checks/`.

Cold-launch validation: `tests/pad/boot_focus.txt` reproduces a background launch
before the first controller tick. The previous runtime opens a pause menu under
the opaque boot fade and fails two assertions; the fixed runtime freezes startup
until focus returns, displays the first-boot options, accepts Escape and reaches
the start room. All nine assertions pass, including the normal pause menu after
focus loss during gameplay. Captures and logs are under
`build/macos-local-checks/cold-boot-before/` and `cold-boot-fixed/`.
The signed app also displays the options in a real 2560x1440 Mac window with
isolated saves; Escape closes them and startup reaches `StartGame`. Its log is
in `cold-boot-native/`. The rebuilt installer updates the existing app, and all
six checked game archives, settings and save files remain byte-for-byte intact.

Windows-host validation (7 October 2026): all 14 synthetic checks in
`tools/macos/test_packaging.py` pass. They cover the app/payload round trip,
executable ZIP permissions, game-asset exclusion, unsafe paths, incomplete or
invalid runtime files, architecture/dependency checks, signing entitlements,
voice-library placement, source preservation and existing-release preservation.
Mac signing and `otool` outputs are mocked in these checks; their success does not
verify Apple's tools or the native runtime.

The six existing Windows save/settings/language/hash targets were rebuilt and
passed, as did the platform target and the new `pt_installer_update_test`. The
latter checks whole-app repair, missing-app restoration, cancellation and rollback,
including spaces and non-ASCII paths. It passed outside the Windows sandbox after
the sandbox denied path canonicalization in a fixture. Host logs and results are
under `build/macos-host-checks/`. The pinned LibOrbisPkg source accepts both
extraction-reader patches. The C# tests have not run here because this Windows
host has .NET runtimes but no .NET SDK.

The installer selects your local P.T. fake PKG (including a file named
`[SuperPSX]-P.T-CUSA01127-USA-Game-PS4.pkg`) or decrypted console dump folder.
It reuses the same LibOrbisPkg extraction and P.T. recognition rules as Windows.
The filename alone does not establish that a package contains usable game data.
The installer copies game archives into a native Mac runtime; it does not convert
the PS4 executable. No game files are included in the app or setup distribution.

Both the game and native Setup use the supplied P.T. artwork as their default
Mac icon. The original image and its icon container are in `assets/macos/`;
packaging copies `pt.icns` into each bundle and sets `CFBundleIconFile` before
signing. Earlier preview packages without that resource show the generic app
icon and need a rebuilt package to display the artwork.

If a PKG's game-data image cannot be read, the installer reports an incomplete,
damaged or unsupported package before creating extracted game files. A full-size
download can still be incomplete: the tested SuperPSX copy had valid metadata but
its entire inner game image was zero-filled. Finish downloading/copying the file
or choose a complete PKG or decrypted dump. Changing the installation folder does
not repair missing package data. The extractor regression suite covers a PKG with
unchanged file size and a zeroed game-image header, and checks that it leaves no
output directory.
The rebuilt Mac setup also rejects that actual incomplete copy, cleans its
staging directory, and installs the complete local PKG successfully. SHA-256
hashes of all three extracted game archives match the existing working install.
All ten extraction regressions and the signed-payload installer self-test pass;
results are under `build/macos-local-checks/pkg-error-native/`.

## Build the installer on your Mac

Install these development tools:

- Xcode command line tools with a C++20 compiler and `std::format` support.
- Python 3.11+, CMake 3.28+ and Ninja (for example, `brew install python cmake ninja`).
- The **arm64 .NET 10 SDK from Microsoft**, used to build the self-contained PKG
  extractor. Homebrew's .NET runtime can link to external Homebrew libraries,
  causing the distribution's dependency audit to reject the published helper.
  See [Microsoft's macOS installation instructions](https://learn.microsoft.com/dotnet/core/install/macos).
- A current **macOS Vulkan SDK** with `glslc`, Vulkan headers and a MoltenVK build
  exposing Vulkan 1.3 or newer. Older Vulkan 1.2-only MoltenVK builds are insufficient.

Run the SDK's `setup-env.sh` in your shell so `VULKAN_SDK` and its tools are available,
then, from the repository root:

```sh
python3 tools/macos/build.py
```

Homebrew can also supply the graphics and C++ build tools:

```sh
brew install cmake ninja shaderc vulkan-headers vulkan-loader molten-vk
export VULKAN_SDK="$(brew --prefix)"
```

If Microsoft's SDK is installed locally at `.deps/dotnet`, select it before running
the build script:

```sh
export DOTNET_ROOT="$PWD/.deps/dotnet"
export PATH="$DOTNET_ROOT:$PATH"
python3 tools/macos/build.py
```

The script builds the release game and native setup, runs CPU/platform tests and
the reflection-mix GPU compute test through MoltenVK,
downloads LibOrbisPkg at revision `643477263b2644e0803e0f58b8726ea4e3f3b7d4`,
patches a separate source copy, publishes its `osx-arm64` extractor, runs synthetic
extraction tests, creates and signs the apps, and loads the packaged voice runtime
to process a silent WAV without microphone access. The installer self-test then
unpacks the actual signed payload, restores Mach-O execute permissions, verifies
the app and extractor signatures, starts both executables, and tests updates.
It stops when a build or test fails. Existing source checkouts can be supplied with
`--liborbis /path/to/LibOrbisPkg`; `--out` selects a new release directory.

Output under `dist/macos/<timestamp>/`:

- **P.T. Mac Setup.app**: double-click, choose the local PKG/dump and the parent
  installation folder. It creates `P.T. PC Port/P.T..app` and adjacent `CUSA01127/`.
- **PT-Mac-Setup-arm64.zip**: the installer ZIP to transfer to another Mac.
- **pt-port-macos-arm64.zip**: a portable runtime. Place `CUSA01127/` alongside
  `P.T..app`, or start `P.T..app/Contents/MacOS/pt --game /path/to/CUSA01127`.

Keep the installed app and `CUSA01127/` together inside `P.T. PC Port`. The app looks
up its own installation folder, so opening it from Finder does not require a
particular working directory. Settings, saves and logs use SDL's Mac preference
directory, normally `~/Library/Application Support/pt-port/pt/`.
Put a `mods/` folder alongside `P.T..app` to use mods with the default Mac launch.

For a runtime-only build:

```sh
cmake --preset macos-arm64
cmake --build --preset macos-arm64 --parallel 6
python3 tools/macos/package.py --out dist/mac-runtime
```

For public distribution, `--identity 'Developer ID Application: …'` signs with
your identity and enables the hardened runtime. Default signing is ad hoc for
local development. Developer ID releases still require Apple notarization and
stapling before distribution; the scripts do not submit anything to Apple.
Do not modify or append data to signed Mach-O executables: this setup stores its
hashed payload in app resources. The replaceable LGPL extractor and corresponding
patched source ship beside the installed app in `extractor/`.

Repairs replace the entire signed `P.T..app` bundle, removing stale resources that
could invalidate its signature. Game archives, mods and other files outside that
bundle are preserved, and a failed or cancelled update restores the previous app.
Keep custom content outside `P.T..app`.

## Graphics and feature scope

Vulkan runs over Metal through the bundled `libMoltenVK.dylib`. Both SDL's surface
creation and volk use that same library, including headless runs. The renderer
enables portability enumeration when advertised and the device's portability
subset, checks its required Vulkan features, and logs the name of a missing
feature before stopping.
No separate Vulkan SDK installation is needed on the player's Mac.

Mac shader builds define `PT_SHADOW_GATHER`: shadow sampling gathers four depth
values and performs the existing bilinear LESS comparison explicitly. A comparison
sampler on the shared `images[]` array otherwise makes SPIRV-Cross declare the
whole array as Metal depth textures, corrupting reads of normals and material
data and causing incorrect lighting and bright ceiling patches. This workaround
and the bundled Noto Kufi/Naskh Arabic font mapping come from
[ahm3texe's Apple Silicon PR](https://github.com/LoreanXavier/pt-pc/pull/4)
(commits `3615fe5` and `deaab67`). The PKG installer and signed Frameworks layout
continue to use this branch's implementation.

The first Mac target uses the existing raster renderer. DLSS, XeSS, FSR SDKs,
frame generation, OpenXR and the external enhanced-texture generator are excluded
from the default build. Upscaling uses Apple MetalFX instead (PC settings >
Upscaling, `upscaler = metalfx`; see [Upscaling](upscaling.md#apple-metalfx)).
Ray queries remain dependent on capabilities actually
reported by MoltenVK and the GPU; they are not part of Mac acceptance testing yet.
The Mac branch now includes upstream's 1.0.2 source update at `fa6fabb`, while
retaining the tested MetalFX synchronization, window-transition fixes and optional
Fast Walk. Upstream has not yet published 1.0.2. The Mac version remains 1.0.3
until a new release is explicitly prepared; preview 4 is the existing release.
Microphone access has a usage description in the game's Info.plist and an
audio-input entitlement for the hardened runtime. The packaged game requests
permission at startup when microphone input is On, without opening a recording
stream. The request is asynchronous, so denying access does not block gameplay.
macOS remembers the decision; denied access can be changed in System Settings >
Privacy & Security > Microphone. Headless checks, viewers and file-based voice
tests do not request permission.

PC Settings > Sound > Microphone input switches capture On or Off immediately
and saves the choice as `[voice] microphone_enabled = 1` or `0`. On is the default
for new and existing settings files. Off also disables device selection and the
microphone test, and skips the permission request at the next launch. The optional
keyboard/controller microphone trigger remains independent of this choice.
Turning input On requests permission if macOS has not already recorded a decision.
The actual microphone still opens only during the voice puzzle or its test.
See [Apple's microphone authorization documentation](https://developer.apple.com/documentation/bundleresources/requesting-authorization-for-media-capture-on-macos).

Voice libraries and all three
models are signed/staged under `Contents/Resources/voice`, matching upstream's
loader. Apple M1, M2/M3 and M4 CPU modules are included and scored at runtime;
the recognizer uses upstream's rescue model, diagnostics and configurable thread
priority. The installer uses Microsoft's self-contained .NET runtime and rejects
Mach-O dependencies that require an OS newer than macOS 14.

## macOS acceptance checks

The local build, signed-payload self-test, PKG installation, normal app launch,
and early/full headless walkthrough checks passed on the M1 Pro described above.
Use this checklist for further manual coverage and testing on other Macs:

1. Run the build script and keep its output, `voice-smoke.log` and
   `installer-self-test.txt`. Passing these is necessary but does not establish
   that the hallway renders or the microphone puzzle works.
2. Install from your PKG, and separately from a decrypted folder; check the helper
   log and compare the extracted archive hashes to the Windows installation.
3. Open the installed app in Finder without `--game`, then move the entire install
   folder and open it again. Test paths containing spaces and non-ASCII characters.
4. Play the hallway and final loop. Verify textures, shadows, mirrors, sound,
   microphone permission, controller input, saving and restarting.
5. Run the existing headless walkthrough with the installed executable and collect
   `pt.log`; Metal shader conversion may reveal further work. This port loads
   MoltenVK directly, which does not load Vulkan validation layers. Layer-based
   validation requires a future integration with the SDK Vulkan loader; use
   MoltenVK's shader diagnostics and Xcode Metal capture in the meantime.
6. Re-run setup on the same install: test repair, cancellation, game archive
   preservation and update rollback. Test a transferred ZIP on a second Mac.
7. Test a cold launch in the foreground and background with an isolated settings
   file and empty save folder. The options must appear after focusing the game,
   Escape must start gameplay, and later focus loss must still open the pause
   menu. Run `tests/pad/boot_focus.txt` headless with `--virtual-pads`,
   `--audio-offline`, `--no-save`, `--options-menu`, `--demo-rate 20` and
   `--frames 10000` to check the startup race without touching player saves.
8. With MetalFX enabled, switch between Native, Quality, Balanced and Performance,
   then disable and re-enable it during play. Change window mode and resolution,
   toggle v-sync and return after focus loss. Check the first-boot options, dark
   hallway, mirrors, particles and subtitles for flicker, stale frames or hangs.
   `pt_queue_handoff_test` checks submission order and synchronization lifetimes;
   Metal's validation layer can additionally check the live renderer.

Primary integration references:
[MoltenVK runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md),
[SDL bundle paths](https://wiki.libsdl.org/SDL3/SDL_GetBasePath), and
[SDL Vulkan library selection](https://wiki.libsdl.org/SDL3/SDL_HINT_VULKAN_LIBRARY),
[Apple audio-input entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input), and
[Apple bundle placement](https://developer.apple.com/documentation/bundleresources/placing-content-in-a-bundle).
