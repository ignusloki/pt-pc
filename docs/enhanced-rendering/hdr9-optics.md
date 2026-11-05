# HDR9: lens reconstruction, motion blur and depth of field

## Requested preset values

| Preset | Bloom | Lens flare | Lens ghosts | Grain type | Grain |
|---|---:|---:|---:|---|---:|
| Original (PS4) | 100% | 100% | 100% | Original | 100% |
| High | 100% | 50% | 25% | Improved Perceptual | 50% |
| Ultra | 100% | 50% | 25% | Improved Perceptual | 50% |

Original retains its vanilla zero sharpening, original texture/lighting choices,
and enabled lens distortion, DOF and motion blur. The quality defaults requested
in HDR8 remain enforced. High still disables motion blur; Ultra enables it.
Ghost intensity is relative to the authored 0.75 ghost multiplier, and the lens
flare master gain also affects ghosts. Existing custom settings are retained;
select a preset to apply these values to an existing installation.

## Lens distortion and chromatic aberration

P.T.'s exact 0.9 centre scale, dome curvature, radius exponent, aspect correction
and RGB offsets (1.42 / 1.275 / 1.12) are retained. This is deliberately not the
Witcher game's different fringe shape or intensity. The local reference was
`thewitcher3classic/effects/chromatic_aberration.hlsli` and the remastered
`grading/saturation_common.hlsli`: separate channel coordinates and filtering
decoded light. The shared `lens_mapping.glsl` also supplies the green-channel
anchor for frame-generation motion/depth, preventing geometry drift.

Reconstruction now uses Catmull-Rom with a local central-texel bound, avoiding
negative lobes and bright HDR overshoot. Minified regions use four positive
samples spanning the warped footprint. A smooth blend between footprints avoids
an artificial transition ring. The local bound is derived from source texels;
it is not a 0–1 radiance clamp. Disabled distortion remains an exact texel copy.

[GPU Gems 2, chapter 20](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-20-fast-third-order-texture-filtering)
describes cubic reconstruction and the difference between interpolating and
non-interpolating kernels. The bounded Catmull-Rom / footprint blend here is our
engineering choice, not that chapter's B-spline implementation.
[AMD FidelityFX Lens](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/lens/)
provides a further reference for per-channel lens resampling.

## Motion blur

The existing velocity/depth reconstruction remains the foundation. The old
normal path had seven point taps at half resolution, a sudden activation cutoff,
and a smoothed frame duration unrelated to the interval of its displacement.
The legacy fixed-shutter branch also tied blur to render FPS. Shutter values now
represent seconds for both branches, retaining P.T.'s half-shutter convention and
60 Hz reference strength. No temporal frame-rate compensation is applied twice.

Both routes now use dedicated full-resolution scratch, 17–49 stratified linear
samples, continuous activation, point-sampled dominant vectors (opposing vectors
must not cancel through bilinear interpolation), and relative linear-depth
comparisons. Depth no longer saturates at 32 metres. Invalid projection and
initial camera history are guarded. Camera and object motion share a rate-based
safety radius; extremely large motions and frame intervals remain bounded.
The old street velocity smoothing retains its time-based exponential response.
Signed FP16 velocity replaces the old 0.5-biased encoding throughout camera,
object, dominant-tile and gather inputs, retaining precision for small motions.
The relative depth transition uses a locally chosen 1% band.

[GPU Gems 3, chapter 27](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-27-motion-blur-post-processing-effect)
explains reconstruction from previous/current transforms and frame-rate scaling.
[McGuire et al.](https://casual-effects.com/research/McGuire2012Blur/McGuire12Blur.pdf)
is the velocity/depth gathering basis.
[Guertin et al.](https://casual-effects.com/research/Guertin2013MotionBlurReport/Guertin2013MotionBlur.pdf)
informs relative depth and careful velocity reconstruction. This is an improved
P.T. gather, not a claim to implement their complete variance/feature-aware
algorithm. Hidden geometry, reflected motion and transparency without matching
depth/velocity remain limitations of a single-layer screen-space method.

Motion blur now precedes the creative LUT in both indoor and street scenes.
Its scratch images remain separate from the date sequence's persistent history.
This matters because those older paths reused similarly named render targets.

## Depth of field

The former chain averaged foreground/background colours together, reduced near
blur to one-eighth resolution, and used two sparse seven-tap filters. The new
path keeps separate foreground/background half-resolution layers. A 2x2 signed
CoC bilateral prefilter selects the appropriate layer. Near colour is
premultiplied before filtering, so bright background colours cannot bleed into
transparent foreground coverage. A deterministic 49/97-sample disk gathers
source-radius near coverage and CoC-weighted far colour. Far upsampling is also
CoC-aware, and small radii blend continuously into the sharp image.

Authored focus distance, focal length, aperture and near-disable flags remain.
At saturated CoC, disk radii match the second moment of the old two hexagonal
passes, excluding their coarse prefilter. Radius scales with output height from
the 1080p reference; changing render resolution does not change apparent lens
strength. Invalid/zero aperture and zero CoC cannot produce division by zero.

[AMD FidelityFX DOF](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/)
documents half-resolution CoC-aware prefiltering, near/far separation, source
coverage and reconstruction. Those principles inform this smaller native
implementation. It is not the SDK's ring-bucket algorithm, has no hidden
background layer, and does not synthesize missing bokeh behind foreground objects.
The tap budget and second-moment matching are local design choices.

## HDR and validation

Optical effects decode each extended-sRGB texel before interpolation and perform
RGB integration in linear light, in both SDR and HDR. Intermediate buffers are
RGBA16F; colour values are not saturated to SDR white. Coverage, normalized CoC
and sampling coordinates remain legitimately bounded. The final HDR peak
shoulder, gamma-emulation option, grading bridge, RCAS, grain and UI separation
remain in place. Lens distortion remains after scene grading, preserving the
original fringe presentation; DOF and motion blur run before the creative LUT
and final display shoulder, after temporal upscaling.

The Release GPU suite exercises the actual production shaders on D3D12 and
Vulkan, including constant HDR light, analytic lens-coordinate ramps, exact
disabled identity, checker overshoot bounds, foreground/background DOF separation,
premultiplied coverage, zero motion, and a moving impulse at 30/47/60/120/240 Hz.
The impulse comparison uses real signed FP16 velocity, allows 0.1% of its source
amplitude for FP16/bilinear quantization, and independently requires centroid and
RMS width within 0.01 pixels. This is distinct from subjective validation on a
physical HDR display. Native/RT/path-traced modes feed the same optical pipeline;
live frame-generation quality and a full playthrough are not certified here.

Release validation completed on 2026-10-25: both backend suites passed, all 252
production HDR output configurations passed, and settings/preset/exposure/output
regressions passed. A scripted menu verified 25 expectations. Eleven HDR scene
and menu readbacks were finite and within the configured 1000-nit peak. Captures
include native D3D12/Vulkan, path tracing, moving cameras at 30/60/120 Hz, and two
4K focus settings; a separate SDR scene completed too. These are targeted tests,
not performance guarantees or physical display measurements.
