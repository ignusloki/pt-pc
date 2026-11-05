# Optional Enhanced rendering on Mac

Implemented on `codex/dual-rendering-pipelines`, based on
`codex/macos-arm64-port` at `9f98d51fa75bdb41e17226faec8905c4cabcaeaa`
(1.0.12). The integration branch and `main` have not been modified. This work
does not publish a release, change the updater destination, or modify Windows
builds and packaging.

## Player behaviour

PC Settings → Display → Rendering offers **Original** and **Enhanced** on
desktop Mac. Original is the default for new profiles and existing settings
files. Selecting another pipeline saves the choice and shows the existing
restart notice. Rendering resources and shaders switch at the next launch.

Each pipeline retains its own effect settings. `[graphics]` continues to hold
Original settings, including legacy migrations; `[enhanced_graphics]` holds
Enhanced settings. Saving while a different pipeline is pending still writes
the running pipeline's changes into its own profile. Display, upscaling and
ray-tracing options remain shared. Enhanced HDR grading has its own section.

Enhanced includes floating-point lighting and post processing, HDR-aware colour
grading, independent UI brightness, SDR response emulation, improved LUT and
effect sampling, smoother bloom/flares, perceptual grain and object-only motion
blur. Its see-through settings preview pauses gameplay and can be disabled.
Enhanced also works with SDR output; HDR calibration and grading controls appear
when HDR output is available and enabled.

Android and iOS keep Original, omit the toggle, and do not compile/package the
Enhanced shader family. Copying an INI with `rendering_pipeline=enhanced` to an
unsupported platform still resolves to Original. Windows stays on Original;
no Windows port, build or test work is included.

## Source and integration

The HDR changes were adapted from
[hartapfel/pt-pc-hdr](https://github.com/hartapfel/pt-pc-hdr/tree/dd90825c060722b9c0cbc90fe5f79ad7c302550e),
pinned to `dd90825c060722b9c0cbc90fe5f79ad7c302550e` (1.0.8.3-hdr).
Its common upstream base is `3b1392abe5215773d4c353f5bc1ecb85233be68a`
(1.0.8). The integration selectively adapts the render, effect, UI and settings
changes to our newer 1.0.12 Mac/mobile code. Fork release scripts, updater
changes, signing and Windows-specific work were excluded.

- Root shader sources stay unchanged. CMake overlays `shaders/enhanced` onto a
  generated include tree and builds a separate `enhanced_*.spv` family on Mac.
  The Vulkan loader selects the family once per renderer. The 97 Original
  SPIR-V files matched the baseline build byte for byte; 96 Enhanced shaders
  are built, excluding the retired legacy HDR gain shader.
- Original keeps its RGBA8 scene target, gain buffer and 80-byte compositor
  constants. Enhanced uses RGBA16F, no legacy gain buffer and 128-byte grading
  constants. Scene formats, scratch targets, VFX, mirrors and upscale resolve
  follow the selected pipeline; both sets of render targets are not allocated.
- Enhanced post/exposure methods are separated from the existing Original
  methods. Shared dispatches preserve Original's settings and math.
- Mac's existing HDR presentation and MetalFX integration are retained.
  Original uses its existing gain-based frame-generation transport. Enhanced
  carries extended signed RGB and reconstructs opaque alpha, preserving
  negative wide-gamut channels and highlights. Generated-frame copy passes
  retain the Mac compositor's passthrough convention.
- The HDR test fixture uses the production compositor. Additional tests cover
  settings migration, restart semantics, profile isolation, renderer allocation,
  exposure behaviour and both native MetalFX colour transports. Packaging
  rejects an incomplete Enhanced shader family and carries the MIT notices.

Imported design notes are under `docs/enhanced-rendering`. Their historical
Windows test results must not be read as results from this integration.

## Validation on 2026-11-05

All local artifacts are under the ignored `.deps/dual-rendering` directory of
the primary checkout. The feature worktree is `.deps/dual-rendering/worktree`.
Tests use isolated settings/saves and the existing extracted game folder.

| Check | Result / evidence |
| --- | --- |
| Apple silicon Mac build | `pt` and `pt_release`, normal Mac configuration, pass |
| Mac regression suites | 58/58 pass; `mac-unit-final/results.json` |
| Production Enhanced HDR compositor | 612 GPU configurations pass, including scRGB/PQ, HDR grading, EOTF, wide gamut, grain, sharpening and UI |
| Native MetalFX HDR colour transport | 288 checks pass across both modes, including negative channels, highlights, near black, resizing and brightness |
| Real renderer allocation | Both pipelines initialize/render; chosen scene format and gain-buffer presence verified |
| Gameplay/render matrix | 8/8 pass: Original/Enhanced × SDR/HDR × native/MetalFX Quality; `mac-smoke-final/results.json` |
| Full gameplay route | 27/27 milestones to ending in each pipeline, MetalFX Quality; `mac-walkthrough-r1/results.json` |
| Menu/restart | Original → Enhanced → Original passes three launches and visible menu captures; `mac-menu-visible/results.json` |
| Native HDR + MetalFX frame generation | Both windowed pipelines pass with native scRGB HDR output; Original queues 1,020 interpolated frames, Enhanced 659; `mac-native-framegen-final/results.json` |
| Intel Mac | x86_64 game and both shader families compile against the 15.5 SDK; settings/profile, exposure and HDR math suites pass under Rosetta; `intel-cpu-checks/results.json` |
| Mac packaging | 20/20 Python tests pass; portable app generated and ad-hoc signature verified |
| Android | arm64-v8a/API 29 Release `libmain.so` and Original shaders build with NDK 28.2.13676358; eight device-analysis and four payload Python checks pass |
| iOS | arm64 device Release app builds, deployment target 17.0; bundle audit passes (238 files), no desktop-only frameworks/runtime dependencies |
| Mobile shared-code host checks | Touch controls, iOS lifecycle/scene state and BC decoder pass; real texture upload-budget checks pass at 512 and 1024 px limits |

The final gameplay build has `PT_BC_DECODE_TESTS=OFF`. A separate host fixture
build with that option enabled supplies the BC/texture-budget checks; enabling
it on a normal Mac gameplay build would impose the mobile texture cap.
The native frame-generation counts verify interpolation occurred; they are not
a performance comparison. New Enhanced controls currently use English fallback
where the selected language has no translation.

Use the existing platform presets and build helpers to configure dependencies.
`tools/macos/test_rendering_pipelines.py` runs the isolated checks:

```sh
python3 tools/macos/test_rendering_pipelines.py --build /path/to/mac-build \
  --out /path/to/new-results --phase unit
python3 tools/macos/test_rendering_pipelines.py --build /path/to/mac-build \
  --game /path/to/CUSA01127 --out /path/to/new-results --phase smoke
```

The `walkthrough` and `menu` phases use the same arguments as `smoke`.
The `framegen` phase opens temporary game windows and requires a Mac with native
HDR and MetalFX frame interpolation. Each output directory must be new.
The unit phase requires the Mac helper's `TESTS` targets and its explicitly
listed additional host test targets, including the BC decoder fixture.

## Coverage limits

Mac GPU/gameplay results above are from an M1 Pro. Shader compilation and CPU
tests cannot establish native Intel GPU behaviour. The M1 Pro has no Vulkan ray
queries, so ray/path-tracing shaders compile but their runtime paths still need
testing on supported hardware. Numerical HDR and native presentation tests do
not establish calibrated physical display brightness on every monitor.

No Android or iOS device was connected for installation/gameplay tests. Their
checks cover compilation, packaging and shared-code regressions, not physical
mobile gameplay. No Windows testing was performed.
