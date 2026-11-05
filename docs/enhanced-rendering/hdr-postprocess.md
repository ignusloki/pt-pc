# HDR5 post-processing audit

Native desktop build 1.0.8-hdr5, based on the user's 1.0.8 source archive.
This supersedes the HDR4 bloom-composition description in `hdr-native.md`.

## Findings and changes

* **Bloom composition:** HDR4's extended screen blend changed slope independently
  when each bloom channel crossed 0.5. Below that boundary it also subtracted
  scene-dependent energy. A smoothly varying colored halo could therefore acquire
  channel-dependent contours. HDR5 adds linear bloom radiance before the grade
  and display shoulder. It retains the original no-bloom black toe, and the SDR
  screen-blend branch. HDR bloom is intentionally brighter than the old screen
  blend in some scenes; it no longer dims bright surfaces underneath the halo.
* **Bloom downsampling:** a single nearest source pixel represented an entire
  low-resolution block. HDR now integrates the source footprint, with fractional
  pixel coverage for noninteger scale factors. This reduces aliasing and temporal
  instability from small bright emitters. Removed the HDR half-texel offset at
  bloom composition. Gaussian/Kawase filtering and accumulation retain the HDR4
  floating-point buffers and radiance range. Authored extraction/blur weights are
  retained; this is not a replacement of the game's lens profile.
* **Lens flares:** their linear particle output now adds to linear scene light
  before encoding. Previously it was added directly to encoded scene RGB.
* **Generated color grade:** CPU interpolation, channel gains and Hermite grading
  no longer get rounded back to eight bits. The generated 16-cube uses RGBA32F.
  Original game LUT samples and the HDR4 reversible highlight proxy remain. This
  removes an additional quantization step, not the original asset's finite
  resolution or authored hue changes. A screenshot alone cannot establish that
  every colored transition was caused by bloom.
* **Depth of field:** decode individual source texels before bilinear filtering;
  the blur pyramid stays linear. Blend near/far/sharp light, then encode once.
  Coverage and circle-of-confusion values remain bounded and unchanged.
* **Motion blur:** both the normal and street paths filter linear HDR. The final
  blur/sharp alpha blend is now explicit in linear light into the other ping-pong
  target, avoiding encoded fixed-function blending and read/write aliasing. SDR
  retains its authored normal/street algorithms.
* **Full-screen history blur:** decode before spatial filtering and history
  blending, encode for its existing history storage. The uncovered-background
  banding smoother likewise filters linear light in HDR.
* **Lens distortion/chromatic aberration:** retains the authored channel offsets;
  interpolation now decodes each tap before blending. Colored lens fringes that
  are explicitly authored remain intentional. The option still disables them.
* **Screen VFX input:** bind the current ping-pong image instead of always binding
  buffer zero, which could supply stale post-effect color.

## Perceptual film grain

Replaces the original overlay with the shared RenoDX luminance-dependent
`ApplyFilmGrain` response, ported from `src/shaders/effects.hlsl`. Density is
`3 * luminance / referenceWhite`; graininess is
`10^(0.880 - 0.736*density - 0.003*density^7.6)`. A common RGB multiplier preserves
chromaticity. Black remains black; bright regions naturally receive less noise.
The existing Film grain slider and scripted strength control its amplitude.
100% uses the common RenoDX 0.04 strength scale, rather than the old overlay's
different visual strength. A Hash33-family noise source avoids the old texture
and the large sine multiplier's precision pattern. The animated engine seed is
retained. Noise is centered and its positive/negative amplitudes are limited
symmetrically near the display boundary to avoid a clipping-induced bias.

Desktop output and the frame-generation UI layer use the same helper. The XR
copy shader also uses it, although OpenXR remains disabled in this package.
Authored horror/static overlays are separate effects and have not been replaced.

## HDR-aware RCAS

The Image clarity slider now controls Lilium's luminance RCAS adaptation, as used
in RenoDX's Crimson Desert and Alien Isolation mods. It replaces the old encoded
RGB unsharp mask. It runs at output resolution **after the display shoulder and
photo filter, before fade, film grain and UI**. It decodes the selected SDR EOTF,
normalizes linear luminance to the configured peak/reference-white ratio, uses
RCAS noise rejection and the -0.1875 lobe limit, and applies a common RGB gain.
A max-channel headroom bound protects saturated highlights as well as luminance.
Zero skips sharpening. There is no hardcoded SDR-white or scRGB-125 normalization
in this differently scaled pipeline. Distortion precedes this final clarity pass
so it sharpens the actually presented scene; grain and UI cannot be sharpened.

FSR's separate built-in sharpening was already RCAS with HDR input flags on both
backends. It is retained. Use one sharpening control when comparing results to
avoid stacking FSR's own pass with Image clarity. DLSS/FSR reconstruction remains
upstream of bloom, grain and final display mapping, with floating-point linear
inputs and their HDR flags enabled.

## Domains and ordering

1. Scene lighting, temporal reconstruction: linear floating-point HDR.
2. Bloom extraction/filtering and flare composition: linear floating-point HDR.
3. Original toe; extended sRGB storage for the legacy post chain. FXAA retains its
   perceptual edge-detection domain and does not clamp highlights.
4. DOF / normal motion blur; grade through the HDR-safe LUT bridge; street motion
   blur and scripted backdrop/history effects. Radiometric blurs decode/filter/
   encode; artistic LUT/backdrop/ink operations retain their authored domains.
5. Lens distortion, screen VFX; photo grade and calibrated display shoulder.
6. Display-linear RCAS; fade; existing below-grain overlays; perceptual grain;
   top UI; final brightness/output encoding to scRGB or HDR10.

The chain deliberately keeps the game's scripted grade/motion-blur ordering.
It does not move every effect into linear light: LUT grading, edge detection,
authored fades and UI have different requirements from optical blur. Pre-grade
filter decoding reverses the engine's sRGB **storage** encoding. Post-map grain
and RCAS use the selected **display reference** EOTF. These paired transforms do
not apply gamma-2.2 emulation twice. Default gamma-2.2 shadow handling is retained.

## Research basis

* [AMD FSR integration deck](https://raw.githubusercontent.com/GPUOpen-Effects/FidelityFX-FSR/master/docs/FidelityFX-FSR-Overview-Integration.pdf):
  placement of RCAS before grain/UI, and the need to respect the algorithm's
  signal domain. The stock FSR1 perceptual path differs from Lilium's modified
  linear-luminance RCAS; its input assumptions were not blindly transplanted.
* [AMD FSR2 manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/):
  RCAS limits local sharpening and suppresses noise, at presentation resolution.
* [Epic post-process materials](https://dev.epicgames.com/documentation/unreal-engine/post-process-materials-in-unreal-engine):
  distinguishes pre-tonemap HDR scene processing from post-tonemap effects.
* [Unity's tonemapping documentation](https://docs.unity.cn/520/Documentation/Manual/script-Tonemapping.html):
  bloom and DOF require access to the source highlight range before tonemapping.
* Local RenoDX `effects.hlsl`, `random.hlsl`, `games/crimsondesert/lilium_rcas.hlsl`,
  `games/alienisolation/common.hlsl`, and `games/asscreedeziotrilogy-taa/taa_rcas.ps_3_0.hlsl`
  supplied the implementation models. Repository tonemap/LUT and resource-analysis
  skills governed upstream range preservation and the display/scene distinction.
  Applicable MIT notices are included in `hdr-post-licenses.txt`.

## Validation

* Release build: all 95 SPIR-V and 95 DXIL shader variants compile.
* Actual production shaders on D3D12 and Vulkan: bloom is continuous additive
  radiance across former thresholds; a 2:1 black/4.0 pattern averages to 2.0;
  DOF filters decoded texels; motion-blur input preserves HDR energy; previous
  compose, LUT, reflection and output regressions pass.
* Two-stage FP16 production output: 54 combinations of baseline/RCAS/grain,
  three EOTFs, scRGB/HDR10, and 400/1000/2000-nit peaks. Grain uses 64 animated
  seeds per combination; checked finite range, black, chromaticity, mean/variance,
  constant-color sharpening and UI preservation.
* Negative control: substituting HDR4's compiled bloom-composition shader makes
  both D3D12 and Vulkan production post tests fail. The HDR5 shader was restored.
* Nine effects-enabled real captures using the user's game archives cover
  standard/RT/PT, Vulkan and DLSS RR. All float readbacks are finite; hallway
  peaks reach the configured 1000 nits. Additional bloom-only captures isolate
  the halo from grain, clarity and lens distortion.

These are representative captures and numerical checks, not a full playthrough
or measurement of the user's display. Live frame generation and all scripted
effect transitions need gameplay review. The previously observed DLSS RR
headless shutdown timeout remains: its capture completes, then the test process
needs termination. No original game assets are redistributed.
