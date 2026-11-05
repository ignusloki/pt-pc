# HDR8: effects controls and stable exposure

## Explicitly requested settings changes

Graphics / Image effects now lists Depth of field, Motion blur, **Bloom
intensity**, **Lens flare intensity**, and **Lens flare ghosts intensity**, in
that order. All three intensity controls support Off through 200% in 25% steps.
Defaults are 100%, 75%, and 100%, respectively. The flare control is a master
gain for halos and ghosts; ghost intensity is an additional relative gain.
The ghost 100% reference retains the previous 0.75 nominal ghost multiplier.

Removed menu choices are fixed to Smooth flares, Smooth Gaussian bloom,
linear-light HDR post sampling, Smooth shadow transition, 64-cube float
tetrahedral LUTs, and Lilium HDR RCAS. Loading an older file or applying the
Original preset cannot silently revert those quality choices. Original still
selects original film grain and its graphics features. Both film grain choices
remain available. Sharpening strength remains adjustable, including zero.

Older disabled bloom/ghost switches migrate to zero intensity. Existing bloom
and flare strengths are preserved. The new ghost intensity takes precedence
over the legacy ghost boolean. The compatibility fields and original shader
branches remain internally for regression tests; they are no longer selectable
quality settings in the supported menu.

## Ghost changes

Ghost gain now multiplies RGB after the opacity limit instead of scaling alpha
before saturation. This makes the 0–200% control linear in emitted radiance,
including at high opacity. It does not remove legitimate coverage bounds.
Large displaced ghosts use a wider positive premultiplied tent filter and a
smooth source-edge window across the outer 12% of the screen. Actual source
coordinates are retained outside the viewport, so the fade cannot jump back to
full strength when occlusion falls back to its offscreen position. Authored
textures, colors, placement, occlusion and visibility fades are retained.

[Chapman's lens flare article](https://john-chapman.github.io/2017/11/05/pseudo-lens-flare.html)
supports smoothing ghost features, suppressing source-edge contributions and
additive compositing. This implementation keeps the game's sprite system;
it is not a replacement screen-space flare generator. The filter width and
12% edge window are local tuning choices, not prescribed optical constants.

## Exposure faults identified in the code

* The old clock intentionally subtracts one from unsigned zero, then converts
  the wrapped value into seconds. The following smoothing step ingests a value
  around 18 trillion seconds, making the exponential adaptation factor saturate.
* GPU measurements arrive later than their source frames. The engine already
  records `slot.exposure_used`, but the previous feedback calculation ignores
  it and instead removes compensation for the current EV.
* An additional five-tick feedback delay and a 30 Hz gate add lag. The near-target
  multiplier `min(1, abs(diff))` makes convergence disproportionately slow.
* The GPU rounds meter samples and their reduction to FP16, and rejects an
  entire cell if any of eight coverage samples is missing. The CPU's
  `log(luminance + 2)` statistic changes its effective weighting with exposure.

## Replacement and research basis

Metering still reads the scene-linear **diffuse lighting** buffer before
composition, bloom, flares, grading, tone mapping, UI, PQ/scRGB encoding and
display calibration. Keeping this input also prevents a specular highlight
or post effect from directly controlling adaptation. Standard and path-traced
rendering use the same metering shader and controller.

A fixed 128x128 distributed grid reduces to 1024 float32 weighted cells.
Center weighting is smooth, with a nonzero peripheral weight; valid covered
samples contribute individually. Invalid samples are excluded. Exact weighted
2nd–98th percentile trimming on the small readback limits outliers without
histogram-bin stepping. The retained cells use an **arithmetic** mean, a
deliberate adaptation to this game's diffuse calibration rather than a claim
to implement geometric-mean metering. The old statistic was approximately
arithmetic at its usual low exposed values. Metering changes can still change
settled brightness in nonuniform scenes.

The reading is divided by the full exposure actually used for that frame.
The absolute target is `log2(authored_key / unexposed_meter)`, bounded by the
game's existing EV limits. Authored compensation curves and manual/pinned EV
remain separate. No display peak or paper-white value enters this calculation.
No new cap is imposed on scene radiance or final HDR highlights.

[Filament's exposure discussion](https://google.github.io/filament/Filament.html)
explains pre-exposure, robust metering, spatial weighting and time-dependent
adaptation. [Epic's auto-exposure documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine)
describes percentile rejection and metering masks. These motivate the estimator;
the percentile limits and center mask are conservative implementation choices.

Adaptation now runs once per measured rendered frame using the supplied game
delta time. It follows constant speed in exposure stops when far from target,
joining an exponential approach with continuous first derivative within
1.5 stops. The integration splits exactly at the boundary, avoiding timestep
dependence. Default rates are 3 stops/s toward a brighter scene and 1.5 stops/s
toward a darker scene, scaled by the authored speed relative to its 1.6 default.
Delta time is capped at 100 ms so a loading stall cannot create a large jump;
zero/invalid delta time holds exposure. Invalid/empty measurements hold it too.
Camera-cut settling discards outstanding readbacks from the old view.

[John Hable's Epic analysis](https://www.unrealengine.com/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25?lang=en-US)
explains why perceptual stop-space movement and a linear/exponential join avoid
jarring distant transitions and near-target jitter. The chosen speeds are
gameplay tuning, not a physiological model of a human eye.

## Validation scope

Release builds compile all 95 shaders for Vulkan and D3D12. CPU tests cover
uniform gray, percentile outliers, invalid/empty/black samples, pre-exposure
invariance, bounded monotonic convergence at 15/30/60/120/240 fps, pauses,
stalls and alternating targets. Production GPU metering tests on both backends
cover partial visibility and levels from black through 100,000, including values
that the previous FP16 round trip could not represent. Existing production
HDR output/roll-off, bloom, LUT, grain, RCAS and settings tests remain required.

Real scene validation uses camera-turn exposure traces with every frame rendered,
separate ghost-intensity captures, and scripted menu navigation. These are
representative checks, not a full playthrough or measurements on every display.
HDR6's peak shoulder, EOTF choices, and HDR7's gamut-preservation decisions remain.

Validation results: all Release CPU/settings/preset tests passed; all 252
production output combinations passed; production Vulkan/D3D12 metering and
post-process tests passed. The in-game menu passed 16 scripted expectations.
Three real rendered exposure sequences each contain 1,857 measurements over
about 31 simulation seconds (standard D3D12, path-traced D3D12, standard Vulkan).
The largest adaptation step was 0.05 stops at 60 Hz and final target errors were
below 0.00012 stops. Separate isolated flare captures at 0/100/200% ghost gain
measured a 1.9999995 ratio between the 200% and 100% ghost contributions; maximum
per-channel linearity error was 0.0009766 in the FP16 intermediate. All seven
output captures were finite and below their configured 1000-nit peak.

Reproducible trace CSVs, the exposure graph and numerical report are retained
in the development workspace under `scene-audit/hdr8-*`. Startup settles are
intentional; the reported smooth transitions refer to ordinary camera turns.
