# HDR6: highlight roll-off and selectable film grain

## Highlight mapping

The existing HDR shoulder was already smooth at normal settings. It scales RGB
together using the largest linear channel, preserving chromaticity. With paper
white 200 nits and peak 1000 nits, scene maxima of 1000, 2000 and 10000 nits map
to approximately 600, 754 and 940 nits. Increasing scene light continues to
approach 1000 nits rather than meeting a fixed scene-white clipping threshold.

HDR6 fixes a subsequent ordering issue: the game's brightness adjustment used
to run *after* that shoulder. Nondefault brightness could push mapped highlights
above the peak into the final safety limiter. HDR brightness now runs before the
display shoulder, RCAS and grain. The scene-copy/noise overlay uses the same
ordering. The final output stage decodes/encodes display color without applying
brightness a second time. UI is display-referred and retains its reference-white
level; SDR's original brightness handling is unchanged.

For linear maximum `m`, peak/paper-white ratio `p`, and knee `k=min(1,0.8*p)`:

```
f(m) = m                                  if m <= k
       k + (p-k)*(m-k)/(m-k+p-k)           otherwise
```

The knee has unit slope. Above it, the derivative is strictly positive and
decreases smoothly toward zero, and the mathematical curve approaches `p`
asymptotically. Normal HDR configurations (`p >= 1.25`) retain the previous curve
and SDR-range appearance. When paper white is at or very near peak, the knee
moves slightly below paper white; this replaces the previous hard-clip fallback.
Preserving an unchanged SDR white *and* extra highlight contrast at the same
peak is impossible without making room below that peak.

The final peak guard remains for FP16 rounding or overlay overshoot, not as the
scene tonemapper. Finite-precision output will eventually round very bright
values to the same display code; an asymptotic curve cannot prevent that. Bloom,
grading, RCAS and gamma-reference behavior from HDR5 are otherwise retained.

## Film grain setting

In the Graphics effects page's Image section, **Film grain type** offers:

* **Original:** original texture, phase, luminance response, and alternate ending
  variant from the port. Its behavior is restored rather than approximated by
  the new grain model.
* **Improved Perceptual:** the HDR5 RenoDX-style luminance-dependent grain.

The existing Film grain strength slider controls either algorithm; zero turns
grain off. Selection applies immediately and persists as `graphics.film_grain_type`
in `pt.ini` (`0` Original, `1` Improved Perceptual). Missing keys retain Improved
Perceptual so HDR5 upgrades keep their current look. The Original graphics preset
explicitly selects Original grain. Desktop composition, frame-generation UI
composition and the XR copy shader share the selection logic; OpenXR remains
disabled in this desktop package.

## Validation

* Release build and all 95 SPIR-V / 95 DXIL variants compile.
* Production two-stage FP16 output passes 252 combinations: three reference
  EOTFs, scRGB/HDR10, multiple peaks and paper-white settings, nondefault
  brightness, RCAS, Improved Perceptual grain, and both Original grain variants.
  Improved grain includes 64 animated seeds per configuration. Tests cover black,
  highlight color/range, constant fields, original response and UI white.
* D3D12/Vulkan shader comparisons pass for both grain algorithms and RCAS.
* Negative control: HDR5's compiled composite fails the new nondefault-brightness
  and low-headroom tests; HDR6 passes. The validated HDR6 shader was restored.
* Setting round-trip, missing/invalid values, preset behavior, menu refresh,
  output, gamma, reflection and frame-generation output-selection tests pass.
  Two pre-existing optional preset-test expectations were corrected: the test
  had edited a comment instead of the setting, and treated the old Ultra ray
  quality as the newer path-traced preset. Production ray-quality behavior is
  unchanged.
* Four real hallway captures cover native rendering and path tracing, D3D12 and
  Vulkan, both grain choices, and brightness 0.5/1/2. These are representative
  checks, not a complete playthrough or physical-display validation.

The previous [post-processing research and audit](hdr-postprocess.md) and
[native HDR audit](hdr-native.md) describe the underlying pipeline and limits.
