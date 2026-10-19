# macOS

The game (`pt`) builds for macOS 14 or newer on Apple silicon (M1 and later, arm64) and on Intel Macs (x86_64). It ships as
`P.T. PC Port.app`, one app per CPU: `P.T.PC.Port-macOS-arm64.zip` and `P.T.PC.Port-macOS-x64.zip` on the Releases page.
A native `P.T. PC Port Setup.app` also accepts a complete P.T. fake PKG or decrypted game folder and installs the app beside the extracted archives in a location you choose. Its release ZIP is `P.T.PC.Port.Setup-macOS-arm64.zip` or `P.T.PC.Port.Setup-macOS-x64.zip`. The portable app still remembers a selected dump folder.

The macOS version is the port's own and does what the Linux one does, with these limits: FSR 3, DLSS and XeSS (Windows
SDKs), the VR mode and the ray-traced shadows, ambient occlusion and reflections (MoltenVK has no ray queries) are off, and
the settings page greys them out. Validation results and hardware limits are recorded in the credits below and the [current integration report](integration/upstream-1.0.4.md).

## Running

1. For direct PKG installation, unzip the **Setup** ZIP for your Mac and open `P.T. PC Port Setup.app`. Select your complete fake PKG or decrypted folder, then choose a writable parent folder. The installer creates a `P.T. PC Port` folder containing the app and `CUSA01127` archives. Updates and repairs replace the signed app as a whole, preserving player files and usable game archives.

   For the portable app, unzip the zip for your Mac (`...-arm64.zip` for an M1 or later, `...-x64.zip` for an Intel Mac; Apple menu > About This
   Mac says which) and move `P.T. PC Port.app` where you like (Applications, or a folder of its own). The game's own
   update notice names the right zip for the CPU it runs on.
2. The app is signed ad hoc, without a paid Developer ID or notarisation. After you try opening the downloaded app once,
   go to System Settings > Privacy & Security, scroll to Security, choose **Open Anyway**, then confirm **Open**. Do this
   only for a copy you downloaded from the project's Releases page and trust. See [Apple's instructions for opening an app
   that hasn't been notarised](https://support.apple.com/en-gb/102445).
3. On the first start without game files the game asks for them (docs/installer.md, First start without game files):
   **Choose PKG file** extracts your P.T. fake PKG with the same helper the Windows and Linux setups use (in the app as
   `Contents/MacOS/extractor/PT.PkgExtract`) into `~/Library/Application Support/pt-port/pt/CUSA01127`, with a progress
   window; **Choose game folder** takes an extracted CUSA01127 folder (or the folder containing it). The choice is
   remembered. A `CUSA01127` or `game/CUSA01127` folder next to the app is found without asking, and `pt --game <folder>`
   (`P.T. PC Port.app/Contents/MacOS/pt`) still works.
4. On a normal game launch with microphone input enabled, macOS requests microphone permission at startup if it has no decision recorded for this app. Granting permission does not start recording. Recording still begins when the game needs voice input or the microphone test. PC Settings → Sound → Microphone input turns capture on/off and saves the choice; Off also disables its test. Turning it back On requests access if no decision exists. If access is denied, gameplay continues; enable the app in System Settings > Privacy & Security > Microphone for the voice puzzle. Already granted/denied decisions are respected. `[voice] key = J` in `pt.ini` works here too. Headless runs, previews, WAV voice tests and installer verification do not request startup microphone access.

Settings, the save and `pt.log` are in `~/Library/Application Support/pt-port/pt/`. Mods go
in `mods/` in that folder (docs/modding.md), since the app bundle is no place for user files.

The `pt-port` or `pt` directory may be a symbolic link or a Finder alias to a writable folder on an already mounted drive.
The app resolves the target before preparing its data folder; the link/alias, settings and saves stay in place. Missing
targets and read-only folders still fail their checks. The app does not mount a missing volume automatically.

## Optional features

HDR uses floating point scRGB through MoltenVK. The Mac presentation layer keeps the shader's 80-nits-per-unit scale in
Core Animation's HDR metadata, reapplies it after a resize or HDR-level change, and clears it on return to SDR. This avoids
MoltenVK 1.4.2's default metadata scale of one nit per unit dimming the image. PC Settings → Display → HDR paper white
controls ordinary whites and menus (200 nits by default); HDR peak brightness controls highlights (Auto falls back to
1000 nits when the display peak is unknown). Screen capability and room lighting still affect the visible result.

MetalFX temporal upscaling is available when the Mac GPU supports it. It uses the existing render inputs and ordered
Vulkan/Metal timeline handoff, and remains Off by default. The menu explains unavailable choices; choosing an
unsupported scaler does not remove the native rendering path. See [upscaling](upscaling.md). MetalFX frame generation
is experimental, Off by default, and available only on supported Apple silicon with macOS 26 or newer.
For scRGB HDR, the frame interpolator receives RGB with the highlight gain and brightness curve included. Keeping the
display transfer in these half-float intermediates preserves dark channels at low brightness. The
Mac backend restores the compositor's colour/gain representation afterward; it does not rely on MetalFX preserving
custom alpha. SDR follows its existing path. The real MetalFX GPU regression checks static highlights, colour, dark
areas and fades after interpolation warm-up, including brightness changes and output resize. This check skips devices
and SDKs without the macOS 26 frame interpolator.
The native HDR-presentation check skips hosts without a Metal device or the renderer's required argument buffers tier 2.
It checks all GPUs, including a discrete GPU on a dual-GPU Intel Mac; supported hardware still runs the full check.
The build record distinguishes skipped GPU checks from passed checks.

Fast Walk is also Off by default. PC Settings → Extras enables a 1.5× walking boost while Shift or the controller's
bottom face button is held; normal interactions and game timing are retained. See [Fast Walk](fast-walk.md).

## Platform layer

macOS takes the POSIX side of `src/engine/platform` (docs/linux.md), with these differences:

| Piece | macOS |
|---|---|
| Vulkan | MoltenVK 1.4.2 (Vulkan 1.4 on Metal, Apache-2.0), loaded by the game from `Contents/Frameworks/libMoltenVK.dylib` or next to `pt` in a build folder; SDL is pointed at the same file (`SDL_HINT_VULKAN_LIBRARY`). The instance asks for portability drivers and the device enables `VK_KHR_portability_subset`. `PT_VULKAN_LIBRARY` picks another library, such as the Vulkan SDK's loader for the validation layers |
| `http.h`: HTTPS GET (update check) | the system's `/usr/lib/libcurl.4.dylib`, loaded with `dlopen` as on Linux. The manifest entry is `macos-arm64` or `macos-x64` by the CPU the game was built for (`Platform()` in update_check.cpp) |
| voice recognizer (whisper.cpp) | `libwhisper.dylib`, `libggml*.dylib` and ggml's CPU variants in `voice/` (CMake modules keep `.so`), rpath `@loader_path`, worker thread at QoS utility. The variants are the target CPU's: `libggml-cpu-apple_m1.so`, `-apple_m2_m3`, `-apple_m4` on Apple silicon; `-x64`, `-sse42`, `-haswell`, `-skylakex` and the other x86 ones on an Intel Mac, each loaded only on a CPU that has what it needs. CPU only, as elsewhere: ggml's Metal and Accelerate backends are off |
| microphone permission | asynchronous AVFoundation authorization during normal game startup, with the usage description and audio-input entitlement in the signed game bundle; permission and actual recording are separate |
| audio mixer | flush to zero through ARM64 FPCR.FZ on Apple silicon, the SSE control register (FTZ and DAZ) on Intel |
| `LiveSplitClient` | no `MSG_NOSIGNAL` on macOS: `SO_NOSIGPIPE` is set on the socket |
| window opened in the background | no pause menu before the controller's first tick (an app started from Finder or the Dock can lose the focus while it starts) |
| shadow maps | compared in the shader from a `textureGather` (`PT_SHADOW_GATHER`, shaders/lighting.glsl) instead of a comparison sampler: with one on the bindless `images[]` array SPIRV-Cross declares the whole array `depth2d`, and Metal reads the G-buffer's normal and material through it as one channel, which lit the hallway ceiling in blotches |
| missing GPU features | the device is checked for the features the renderer needs (`vk_context.cpp`) and the log names the one that is missing |
| first-run screen when no game is found | `NSAlert` and `NSOpenPanel` through SDL; the app is brought to the front first (`SDL_HINT_MAC_BACKGROUND_APP` 0), since SDL 3.4 leaves an app with no window in the background on macOS 14 and later; the PKG helper is a single-file .NET executable for the Mac's CPU, signed with the app |
| crash dump | none |

Off on macOS: FSR 3, DLSS, XeSS and the VR mode. Linux has its own upstream FSR 3/DLSS backends. MoltenVK has no ray queries, so the
ray-traced shadows, ambient occlusion and reflections are greyed out in the settings; Metal has no sampler LOD bias,
which MoltenVK ignores.

Upscaling on macOS is Apple MetalFX (`upscaling.md`, Apple MetalFX; `src/engine/render/upscale/metalfx_backend.mm`): the
port's temporal upscaler, run on a Metal command buffer beside MoltenVK's. It uses `VK_EXT_metal_objects` to name the
Vulkan targets' Metal textures, and splits each frame's submission in two around the Metal pass, ordered by an
`MTLSharedEvent` imported into a Vulkan timeline semaphore. `MTLFXTemporalScalerDescriptor.supportsDevice:` decides whether
a machine can run it: Apple silicon and the supported AMD GPUs, not an Intel Mac's integrated GPU, which greys the row out
with the reason. `PT_UPSCALER=metalfx` selects it for a run (upscaling.md, Test overrides). On Apple silicon with macOS 26
or newer, `frame_generation = metalfx` also enables MetalFX frame interpolation: the renderer presents a generated image
between each pair of rendered frames, forces FIFO/v-sync while it runs, and composites the game's UI on both images. This
is experimental; fast bright lights such as the flashlight can flicker because the frame interpolator has no reactive-mask
input. The MetalFX framework is part of macOS; nothing of it ships with the app. Built with `-DPT_METALFX=OFF` the
Objective-C++ backend and both features are left out and the game behaves as before.

Intel Macs: the renderer indexes a bindless array of images, which MoltenVK builds on Metal argument buffers tier 2.
Intel's own integrated GPUs and older AMD ones may not offer that; the game then stops at start with a log line naming the
missing Vulkan feature (`pt.log`, "required feature ... is not available"). The user accepted native installation/gameplay on a 2019 MacBook Pro, with slower performance; the exact GPU and frame rate were not supplied. Start at Windowed 1280 × 720, Low, 30 FPS, Reflections Off and Enhanced textures Off. See the [Intel test record](intel-regression-20261011.md) and [checklist](integration/intel-mac-test-checklist.md); the model year alone does not establish GPU compatibility.

## Building

Xcode's command line tools (`xcode-select --install`), then from Homebrew `cmake ninja shaderc` (for `glslc`),
`vulkan-headers` and `vulkan-loader` (CMake's FindVulkan wants a library; the game itself loads MoltenVK):

    cmake -G Ninja -B build/macos -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build/macos --target pt

The build is for the CPU of the Mac it runs on (arm64 or x86_64; `-DCMAKE_OSX_ARCHITECTURES=` names it). A universal build
is refused: ggml's CPU variants for the voice recognizer are chosen per architecture. Build once on each kind of Mac, as the
workflow does; `make_app.py` reads the architecture from the executable and names the zip `...-macos-arm64.zip` or
`...-macos-x64.zip`.

CMake downloads MoltenVK next to the other dependencies (cmake/MacOS.cmake). `build/macos/pt` runs from the build folder. The unit tests build with the default
target, as elsewhere.

    python3 tools/macos/make_app.py --build build/macos

makes `dist/P.T. PC Port.app` (MoltenVK in `Contents/Frameworks`, the shaders, fonts, voice runtime and texture tools in
`Contents/Resources`, where `SDL_GetBasePath()` points in a bundle), signs every binary in it ad hoc and zips it with
`ditto`. `--exe build/macos/pt_release` packages the release game; with `--game <folder>` the bundled game also shoots
the loop browser's previews, as tools/package.py does.

The default icon in both Mac apps is `assets/macos/pt.icns`, generated from the supplied `assets/macos/pt-icon.png`. Package resources and Info.plist are populated before signing.

For a complete game and installer build, install the .NET 10 SDK as well, then run:

    python3 tools/macos/build.py --arch arm64 --out dist/my-mac-build
    # On an Intel Mac:
    python3 tools/macos/build.py --arch x64 --out dist/my-intel-build

Players use the ready graphical installer from [the fork's Releases page](https://github.com/ignusloki/pt-pc/releases); they do not compile it. The [native Intel build guide](integration/intel-native-build.md) records the earlier source handoff and development prerequisites.

The helper builds `pt_release`, its resources, the in-app PKG helper and the native installer, runs the CPU and installer regressions, publishes the self-contained extractor, and calls `make_app.py` followed by `make_setup.py`. It also runs `pt_hdr_output_test` for HDR calculations, `pt_macos_hdr_test` against the actual SDL/MoltenVK window to check brightness metadata, level changes, resize and HDR/SDR transitions, and `pt_metalfx_hdr_test` through the real MetalFX frame interpolator to check highlight brightness after warm-up. These checks do not measure physical screen luminance; final appearance still needs a display comparison. It preserves upstream's three voice models and the target architecture's ggml CPU modules. The matching patched LibOrbisPkg and wrapper source and LGPL notices are embedded in the installer. Users do not need the .NET SDK installed. The build helper uses Microsoft's official runtime packs, including with a Homebrew SDK. The package audit checks that every Mach-O dependency is contained and targets macOS 14 or earlier. Custom extractors that reference external Brotli libraries can bundle those libraries and their MIT license, but host-only bottles requiring a newer macOS are rejected.

`--liborbis <checkout>` uses an existing source checkout; otherwise the pinned revision is downloaded. `--build <folder>` changes the build/output cache. `--identity <Developer ID Application identity>` signs using that identity; the default is ad hoc. Signing with an identity does not perform notarization.

Cross-compilation is available with `--arch x64 --skip-tests` on Apple silicon, or the reverse target. It verifies and signs the target binaries but skips target-runtime tests. Native hardware testing remains required, especially for GPU support and voice CPU feature selection. Release-only Mac builds stage the same shaders, fonts, voice runtime and MoltenVK as developer builds. Upstream 1.0.7 removes Enhanced Textures; its retired tools and licenses are excluded even when reusing an older build cache.

## Releases

`.github/workflows/macos.yml` builds on native Apple silicon (`macos-26`) and Intel (`macos-15-intel`) runners. It runs for pull requests, this integration branch, manual dispatch and published releases. Each runner builds the game and installer, tests packaged voice loading, verifies signatures and exercises native installer rollback. The published-release job attaches portable and Setup ZIPs for both CPUs. The updater keeps its existing portable ZIP asset names and GitHub Releases API endpoint.

`.github/workflows/desktop-validation.yml` checks Windows and Linux separately: both game targets, the native installer, shared installer transactions, Unicode paths, source preservation, synthetic PKG extraction, self-contained extractor publishing and Python payload regressions. Windows keeps all required upscaler backends and verifies their executable fingerprint and staged runtime files. The Windows resource/link check uses a compile-only payload fixture that is never distributed as an installer. Neither workflow ships original game archives or proves GPU gameplay compatibility.

The public runners have no game files, so loop browser previews are generated by the game at first use. Fork releases that already contain both audited Setup and portable packages are retained unchanged by the release workflow; native manual builds and upstream releases still run. Published builds use `PT_UPDATE_MANIFEST_URL_OVERRIDE` to follow the building repository's releases, leaving the source's upstream default intact. The [current integration report](integration/upstream-1.0.7.md) records the Mac 1.0.9 candidate, its upstream changes and the player-validation checklist. The [macOS 1.0.8 release notes](release-notes-macos-1.0.8.md) describe the previous stable release.

## Credits and what was tried

- Apple silicon: ahm3texe (LoreanXavier/pt-pc pull request 4), M4, macOS 27: the 22 unit tests and the 28 walkthrough
  scenarios; the bundle played the first room and the hallway. Not tried on M1 to M3 or on an older macOS.
- ignusloki (github.com/ignusloki/pt-pc, `codex/macos-arm64-port`, preview 1 to 3): Apple silicon, M1 Pro. From that fork
  come the `SO_NOSIGPIPE` socket option, the check for missing GPU features, enabling the portability subset's features and
  the no-pause-before-the-first-tick fix. The native macOS installer is adapted on this integration branch, with whole-app repair and rollback. Default-off Fast Walk, the tested MetalFX scaler and fullscreen pointer mapping are now adapted to current upstream. Startup microphone authorization, its input toggle and the selected Mac icon are included. Intel hardware gameplay still requires testing.

- Integration foundation: local M1 Pro, macOS 26.6.2; native installer and portable game compiled, synthetic extractor and shared installer tests passed, a real PKG installed and repaired, and packaged headless graphics/first-boot focus checks passed. Interactive microphone consent and live capture remain manual checks.

- Refreshed integration (11 October 2026): M1 Pro, macOS 26.6.2. ARM64/x86_64 packages, 19 suites per CPU (Intel through
  Rosetta), extractor and native installer checks passed. A full MetalFX route reached the ending (27/27 milestones);
  bathroom, photo mode, microphone controls, Fast Walk persistence and first-boot focus passed. Native Intel gameplay
  was subsequently accepted by the user on a 2019 MacBook Pro, with slower performance; native Windows/Linux CI remains pending. See the Intel test record and current report above. ginnfx/brajan zalta's MetalFX contributions and
  subsequent tested synchronization work are retained. The 1.0.7 integration adopts upstream's MetalFX frame generation
  with submission and generated-frame UI fixes; its Apple silicon regression is recorded in the current report.
