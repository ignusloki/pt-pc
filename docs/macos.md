# macOS (Apple Silicon)

This is an experimental native arm64 port for M-series Macs running macOS 14 or newer.
The source changes, Cocoa installer and packaging tools are provided; they have not
yet been compiled or tested on macOS. Windows-side packaging tests do not establish
that the game renders correctly on a Mac. A Mac build and gameplay test are required
before treating the installer as a working release.

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

## Build the installer on your Mac

Install these development tools:

- Xcode command line tools with a C++20 compiler and `std::format` support.
- Python 3.11+, CMake 3.28+ and Ninja (for example, `brew install python cmake ninja`).
- The **arm64 .NET 10 SDK**, used to build the self-contained PKG extractor.
- A current **macOS Vulkan SDK** with `glslc`, Vulkan headers and a MoltenVK build
  exposing Vulkan 1.3 or newer. Older Vulkan 1.2-only MoltenVK builds are insufficient.

Run the SDK's `setup-env.sh` in your shell so `VULKAN_SDK` and its tools are available,
then, from the repository root:

```sh
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
enables portability enumeration and the device's portability subset, checks its
required Vulkan features, and logs the name of a missing feature before stopping.
No separate Vulkan SDK installation is needed on the player's Mac.

The first Mac target uses the existing raster renderer. DLSS, XeSS, FSR SDKs,
frame generation, OpenXR and the external enhanced-texture generator are excluded
from the default build. Ray queries remain dependent on capabilities actually
reported by MoltenVK and the GPU; they are not part of Mac acceptance testing yet.
Microphone access has a usage description in the game's Info.plist and an
audio-input entitlement for the hardened runtime. Voice libraries are signed in
`Contents/Frameworks`; models stay in `Contents/Resources/voice`. The voice
recognizer uses a baseline ARM64 GGML CPU backend and utility thread priority.

## Acceptance checks still required on macOS

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

Primary integration references:
[MoltenVK runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md),
[SDL bundle paths](https://wiki.libsdl.org/SDL3/SDL_GetBasePath), and
[SDL Vulkan library selection](https://wiki.libsdl.org/SDL3/SDL_HINT_VULKAN_LIBRARY),
[Apple audio-input entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input), and
[Apple bundle placement](https://developer.apple.com/documentation/bundleresources/placing-content-in-a-bundle).
