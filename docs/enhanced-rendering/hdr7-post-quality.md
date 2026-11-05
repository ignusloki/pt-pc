# HDR7: flare halos, post quality, and shadow gradients

Based on the native P.T. PC Port 1.0.8 source, retaining HDR1–6 fixes.

## What produced the halo

The lens-flare render target contains a broad textured halo independently of
bloom. Real hallway captures with bloom disabled and with flares disabled
separate these contributions. This demonstrates a source of halos in the game;
it does not establish that every contour in a supplied screenshot has one cause.
The flare sprites intentionally include optical shapes. Their original alpha
calculation also rounds to 8-bit steps, and their occlusion sample position is
rounded to 1/255 of the screen, potentially several pixels at high resolution.

**Smooth lens flares** keep the authored texture, color, size, fading and
occlusion logic, but use floating-point opacity and occlusion coordinates.
A positive 3x3 tent filter samples premultiplied flare color/alpha so transparent
texels cannot introduce color fringes. A smooth edge window prevents that filter
from ending abruptly at the sprite quad. This affects flare sprites only, not
smoke, rain, liquid particles, emissive materials or the actual light.

The new independent intensity scales flare RGB, rather than alpha before its
coverage limit. Zero removes flares, 100% retains original strength. The default
is Smooth at 75%, as requested for a softer, less intense halo. The existing
Lens flare ghosts switch still controls the large displaced reflections.

## User choices

All controls are on **Graphics**, in **Image effects** and **Image detail**.
They apply immediately and are saved in `pt.ini`.

| Control | Original option | Enhanced option | Default |
|---|---|---|---|
| Bloom filter | Authored Gaussian/Kawase chain | Smooth Gaussian, overlapping separable filters accumulated across scales | Original |
| Bloom intensity | 100% original gain | Independent 0–200% control | 100% |
| Lens flare quality | Authored sampling/quantization | Smooth texture, floating opacity/position, soft edges | Smooth |
| Lens flare intensity | 100% original gain | Independent 0–200% control | 75% |
| Post-effect sampling | Encoded-color interpolation | Decode individual texels before interpolation | Linear light |
| Color LUT sampling | 16-cube, trilinear | 64-cube, floating-point tetrahedral | Original |
| Shadow transition | Original toe | Smooth C1 transition | Original |
| Sharpening method | Original local unsharp kernel | HDR-aware luminance RCAS | HDR RCAS |
| Film grain type | Original texture/ending response | Improved Perceptual | Improved Perceptual |

The Original graphics preset selects original effect choices and flare intensity.
HDR range preservation, float buffers, output calibration and the highlight
shoulder remain active in HDR. These options reproduce original effect behavior
where compatible with HDR; they are not a switch back to SDR clamps or a promise
of bit-identical PS4 rendering. Existing on/off controls for DOF, motion blur,
distortion, bloom and ghosts remain. Artistic/scripted effects without a new
algorithm are retained, rather than assigning them an untested "high quality" label.

Post-effect sampling controls interpolation used by the shared HDR optical-filter
helper (DOF, motion blur, history/banding blur and lens distortion). It does not
revert the HDR-correct linear composition of those effects. Original sharpening
returns to the pre-display screen-effects pass; its local overshoot bounds remain,
but its SDR-white upper clamp is omitted in HDR. RCAS remains after display
mapping and before grain/UI. No effect moves after final PQ/scRGB encoding.

## Bloom quality

The optional Smooth Gaussian path replaces diagonally spaced Kawase taps with
overlapping separable Gaussian passes. Accumulating several blur scales gives a
soft core and falloff without disjoint sampling lobes. It retains the authored
extraction polynomial and size. Its total gain matches the original chain:
the Kawase path applies the authored weight in its first two passes and again
at accumulation, so matching gain means weight cubed, not weight once.
The special original Gaussian branch retains unit gain. Bloom intensity is then
applied once at composition, including the subliminal text path.

Both HDR choices use the existing fractional-coverage downsample and float
buffers, add linear radiance before grading, and preserve HDR6's smooth peak
roll-off. The Gaussian option costs an extra accumulation pass per scale; it is
optional. 75% bloom intensity is a useful starting point for less glow.

## Shadow curve and LUT handling

The original toe is `x*x/max(x,k)`, where `k=sqrt(1/512)` in the encoded post
domain. At `k`, its derivative jumps from 2 to 1. A smooth low-light gradient can
therefore reveal a contour, especially after a grade. The optional replacement
keeps the exact original below `0.5*k`, joins with a monotone cubic to identity
at `1.5*k`, and has continuous value and slope. It preserves zero black and
leaves upper shadows/midtones untouched. This is a local toe change, not global
shadow lifting. Gamma-2.2/sRGB/2.4 reference selection remains separate and is
not applied a second time. HDR6's highlight shoulder remains unchanged.

The enhanced LUT option evaluates the original asset interpolation, color-scale
and Hermite grade onto a 64-cube RGBA32F texture. This reduces discretization of
the generated nonlinear grade; it does not manufacture detail lost in the
original 8-bit 16-cube asset. Four exact texel fetches and floating-point simplex
weights replace hardware bilinear interpolation plus slice blending. Identity
and affine grades reconstruct correctly across all six tetrahedral orderings.
Both current and previous LUTs derive their size from the texture, including
live 16-to-64 transitions. The existing HDR-safe reversible highlight proxy and
scripted LUT blending remain. The LUT only rebuilds when its grade/settings key
changes. A 64-cube uses 4 MiB per LUT and costs more CPU/upload work during grade
changes; the original option remains available.

No final 8-bit dither is added to HDR floating-point buffers. The original
`deband` option remains SDR-specific. scRGB FP16 and HDR10 10-bit PQ still have
finite precision; neither LUT interpolation nor a smooth curve can guarantee
an absence of banding on every display or in already quantized source textures.

## Color gamut decision

The examined lighting, texture and grading pipeline is BT.709-based and does
not provide a proven wider-gamut source. HDR10's BT.2020 encoding and scRGB are
capable of carrying wider colors, but a correct conversion of BT.709 content
preserves its chromaticities; it does not expand them to fill P3.

No artificial P3 saturation expansion or primary relabeling is included. That
would change the original look rather than recover source color. Supporting
genuine P3 assets would require a defined source color space and signed-color
handling through grading, post effects, grain and UI. If a creative expansion is
added later, it should be explicitly optional and target **P3-D65** for HDR
monitors, not cinema DCI-P3's different white point/gamma. Neutral preservation
remains the baseline.

## Research and validation

Research references, in addition to the local RenoDX tonemap/LUT, resource and
analysis-graphing skills and the previous [post audit](hdr-postprocess.md):

* [Jimenez, Next Generation Post Processing](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/):
  multiscale filtering, positive normalized weights and temporal stability.
  This build uses its existing Gaussian resources, not a claim to implement the
  complete Call of Duty bloom pyramid.
* [OpenColorIO interpolation](https://opencolorio.readthedocs.io/en/v2.6.0/api/enums.html):
  distinguishes trilinear and tetrahedral 3D LUT interpolation.
* [Khronos texture sampling](https://github.khronos.org/Vulkan-Site/spec/latest/chapters/textures.html):
  texture-coordinate interpolation has limited subtexel precision; shader-side
  floating-point weights avoid that particular source of steps.
* [Microsoft Advanced Color](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range):
  scRGB representation, negative channels and wider-gamut color transport.
* [ACES output parameters](https://docs.acescentral.com/system-components/output-transforms/parameters/):
  separates limiting gamut, white point, peak luminance and display encoding.

Release shaders compile for both backends. Production GPU tests cover original
and smooth shadow ramps, black, monotonicity, 16/64-cube affine grades across
color orderings/boundaries, identity and clipping-LUT HDR reconstruction, and
the existing lighting/bloom/filter/output tests. Hardware trilinear comparisons
allow its documented finite weight precision; tetrahedral tests use a tighter
floating-point tolerance. Settings persistence, bounds, presets and menu refresh
tests pass. The actual in-game menu passes 33 scripted checks, including live
LUT quality changes in both directions; screenshots were visually inspected.

Six real hallway captures isolate original/enhanced/flares-off/bloom-off modes
and cover standard rendering/path tracing, D3D12 and Vulkan. Float readbacks
remain finite and below the configured 1000-nit peak. These are representative
scene checks, not a complete playthrough or physical HDR display measurement.
The native desktop package continues to have OpenXR disabled.
