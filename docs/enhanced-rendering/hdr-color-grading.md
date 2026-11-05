# Native HDR color controls: design audit

Local development from public commit 12edab0152da2672a92c932e564686987a2bab22,
branch hdr-color-grading. The user approved the tested result for publication as
1.0.8.1-hdr; the release retains the fork update endpoint and signing key.
The previous Fix P.T. PC Port HDR session and HDR1–HDR9 audit documents were
reviewed before implementation. The source baseline is archived outside this
checkout in pt-pc-hdr-work/color-grading/baseline-12edab0.zip.

## Proven pipeline and placement

scene_frame.cpp / scene_post.cpp preserve float lighting and reflections, apply
exposure, bloom and flares, then optical DOF/motion blur before the authored LUT.
The LUT bridge uses the existing N2 intensity proxy and a 64³ float LUT with
tetrahedral sampling. Backdrop/date/history effects and lens distortion follow.
Renderer::Composite maps this still-extended scene to display range, sharpens,
then applies grain and separate UI before scRGB or PQ/BT.2020 encoding.

The new user grade is after the complete native scene/effect chain and before
the display shoulder, sharpening, grain and UI. This avoids accumulating user
grading in temporal histories or coloring menus. Scene-copy noise overlays use
the same mapping function; they are scene effects, not interface brightness.
Optical integration, authored LUT strength and the released shoulder remain.

## Sources and engineering choices

* [ACES output transforms](https://docs.acescentral.com/system-components/output-transforms/)
  separate rendering/limiting gamut from output encoding. Use P3-D65 limiting
  primaries with the existing scRGB or PQ/BT.2020 transport. Cinema DCI white
  and gamma 2.6 are not appropriate substitutes for the D65 desktop pipeline.
* [ACES gamut compression](https://docs.acescentral.com/system-components/output-transforms/technical-details/gamut-compression/)
  motivates a continuous compression knee rather than independent RGB clipping.
  This patch does NOT implement the ACES 2 JMh output transform or replace the
  game's look with ACES. Its inexpensive constant-luminance D65 ray compressor
  follows RenoDX colorcorrect::GamutCompress's neutral-axis construction, with
  a local smooth N2 knee at 80% of the target cone boundary. The knee is an
  engineering choice requiring numerical and scene validation, not a standard.
* RenoDX src/shaders/colorgrade.hlsl supplies the exposure/contrast and version-4
  highlight/shadow behavior. src/games/asscreed3remastered/common.hlsli and
  src/games/asscreedeziotrilogy/swap_chain_proxy_pixel_shader.ps_5_0.hlsl show
  target-space conversion before neutral-axis compression. Newer weighted-LMS
  gamut helpers additionally model a spectral locus; this compact native path
  does not claim that observer model. Repository examples were inspected, not
  treated as proof that unrelated game-specific implementations apply here.
* [Björn Ottosson's Oklab definition](https://bottosson.github.io/posts/oklab/)
  supplies the signed cube-root transforms for perceptual saturation, also used
  by RenoDX. Saturation preserves Oklab hue; the separate inexpensive gamut
  compression preserves linear luminance and chromaticity-ray direction, not
  exact perceptual hue. These are distinct guarantees.
* [Microsoft Advanced Color](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
  defines scRGB's BT.709 coordinates, extended/negative channels, and 80-nit
  reference. Signed channels must survive until conversion to output primaries.

The original LUT assets are bounded BT.709 data. Wide gamut permits colors from
user saturation and other signed inputs; it does not expand every BT.709 color
or recover colors absent from source assets. Default grading is identity and
wide gamut is on by default (updated at the user's request). Explicit saved On/Off choices are retained. SDR retains its existing presentation and brightness control.

## UI and compatibility

HDR game brightness keeps the existing hdr_paper_white config key and controls
scene reference white. HDR UI brightness has its own nits setting, applied to
interface RGB before alpha blending (never to coverage); missing values migrate
from the saved game brightness so existing configurations retain their UI level.
SDR eotf emulation retains hdr_sdr_eotf and default Gamma 2.2. It emulates the
SDR reference monitor response, especially shadows, before HDR encoding.
The original skull-screen brightness control is inactive in HDR and its stored
value cannot affect HDR scene or scene-copy output; it remains usable in SDR.

UI asset inspection identified original option-model mesh 2 as the full-screen
black backdrop, mesh 37 as additive noise, and meshes 155/158 as full-screen
masked brightness backdrops. Hide these while an HDR preview row is focused
or the Graphics page is open with preview enabled, retaining readable text,
pause state, transitions and dialogs.

## Validation

Release build: pt, production GLSL compiled to SPIR-V and DXIL. The default
neutral/BT.709 path passed the pre-existing numerical expectations. The final
HDR pipeline regression passed 504 configurations across scRGB/PQ, all three
reference EOTFs, 400/1000/2000-nit peaks, two reference-white levels, grading
extremes, RCAS and grain. Wide tests require bounded P3 output, signed BT.709
channels surviving scRGB, exact black and ungraded opaque UI white.

Composite output, settings persistence/migration, PC settings refresh, platform
(including updater routing) and Vulkan/D3D12 backend tests pass. Signed scRGB
frame-generation UI reconstruction has a numerical regression: its payload
must retain negative premultiplied channels rather than clamp to BT.709.
Actual interpolated-frame gameplay was not exercised.

Native D3D12/scRGB and Vulkan/HDR10 menu automation pass. The original SDR
menu regression also passes all 33 expectations. tests/pad/hdr_color_menu.txt records the
reproducible control/reset checks. Scene captures completed on Vulkan and D3D12;
the transparent preview was visually inspected with a lit hallway behind it.
Render API precedes HDR; brightness order is peak, game, UI.

Frozen-scene floating-point captures prove UI independence: 200→120-nit UI
changes leave a 1100×30 gameplay-only region pixel-identical (0-nit error).
The sampled title changes from about 196 to 117.5 nits (0.600 ratio; texture
coverage keeps the sample slightly below nominal white). Changing game
brightness from 200 to 300 nits then changes the scene while that UI sample
remains at about 117.5 nits. Alpha coverage is not rescaled.

The existing fork routing was already correct: generated PT_UPDATE_MANIFEST_URL
is https://api.github.com/repos/hartapfel/pt-pc-hdr/releases/latest. No updater
source modification or public release operation was necessary.

Local logs, captures, baseline archive and test package are in the sibling
pt-pc-hdr-work/color-grading directory. No physical-display measurement is
claimed; final subjective assessment on the user's HDR display remains useful.

Local installer: P.T.PC.Port.HDR-Color-Grading.Local.Setup.exe. Integrity and
installer self-tests pass; all 559 payload ranges, sizes and hashes verify.
The package contains no original game archives and has not been published.

## Iteration 2: expanded controls and graphics preview

Contrast, highlights, shadows, saturation and highlight saturation now span
0–200% in 5% steps, with 100% neutral. Exposure stays -4 to +4 EV in 0.1 EV
steps. Wide gamut defaults On for missing configuration keys and grade reset;
explicit saved Off remains Off. Blowout defaults 0%, spans 0–100% in 5% steps,
and resets to 0%. All ranges are enforced during settings loading as well as UI.

Blowout follows the currently checked-out Witcher 3 Classic and Remastered
`tonemap/psychov_tonemap.hlsli` output extension: after scene display mapping,
mix linear mapped RGB toward its BT.709 luminance gray by the Blowout fraction.
It preserves luminance, gives exact grayscale at 100%, and never grades UI.
This is distinct from the older shared RenoDX luminance-dependent exponential
blowout and the hartapfel Witcher fork's variant; those were reviewed and not
selected because the user explicitly requested the grayscale endpoint.
The shared library reference is https://github.com/clshortfuse/renodx/blob/main/src/shaders/colorgrade.hlsl.

Blowout is after the gamut fit/shoulder and before RCAS, grain, overlays and HDR
encoding. The same helper grades scene-copy effects. Composite/frame-generation
push constants are 128 bytes; UI constants are 80 bytes. Both backends and their
fixtures use the updated layouts. The extended tonal ranges also have a finite
headroom guard before the highlight exponent, far above physical luminance, to
prevent overflow with extreme FP16 inputs at +4 EV / 200% contrast/highlights.

The shared See-through settings preview toggle now covers Graphics, lighting,
textures, effects and upscaling in both SDR and HDR, with access from Graphics
as well as HDR settings. At iteration 2, disabled rows retained their backdrop;
iteration 3 below removes that restriction for the entire Graphics menu. Dialogs,
first-boot screens and menu transitions retain their backdrop. Settings requiring
restart keep that behavior.

The iteration-2 Release regression passes 612 production composite cases,
including all percentage endpoints, maximal HDR inputs, and numerical checks
of 0/50/100% Blowout against the unmodified display-mapped baseline. Settings
roundtrip, default migration, menu refresh, composite and both-backend fixtures
also pass. Native menu checks pass on D3D12/scRGB and Vulkan/HDR10. HDR and SDR
graphics preview captures and the grayscale endpoint were visually inspected.
The baseline source archive and new outputs are in pt-pc-hdr-work/color-grading-v2;
the first local installer remains available separately. Nothing is published.

Iteration-2 capture checks: the gameplay-only region at 100% Blowout has zero
RGB channel spread; its luminance differs by at most 0.271% after the FP16
encode/decode path. Bloom 0/200% visibly changes the preview. The v2 installer
passes integrity/self-tests and verification of all 559 payload files.

## Iteration 3: grading presets and continuous graphics preview

The HDR color-grading Preset row derives its label from all seven float grading
controls plus wide gamut; no redundant preset identifier is persisted. Original
is the default neutral grade (exposure 0 EV, contrast/highlights/shadows/saturation/
highlight saturation 100%, Blowout 0%) with wide gamut On. HDR look changes
highlights to 145% and saturation to 150%, retaining wide gamut On and neutral
values for the other controls. Other combinations show Custom automatically.
Restoring matching values restores the preset name; a 1e-5 float tolerance
allows equivalent UI-step and INI representations. Existing saved grading is
preserved. Preset application and reset do not change brightness calibration,
UI brightness, EOTF emulation or the preview toggle.

Graphics preview is now page-wide, including disabled sliders, tab strip and
Back focus, on the graphics/lighting/textures/effects pages. The shared preview
toggle still disables it. Dialog, first-boot and transition safeguards remain.
HDR and upscaling retain their existing row-based preview behavior. No shader
processing or color pipeline order changed in this iteration.

Release build, settings persistence/preset matching and PC menu refresh tests
pass. Native preset and control automation passes on D3D12/scRGB and Vulkan/HDR10.
Captures verify Original, HDR look and Custom labels/values; a Path Tracing
capture shows the scene behind disabled Shadows, and disabling preview restores
the opaque backdrop. The SDR graphics preview capture also passes visual review.
Iteration-3 outputs and the pre-change source archive are in
pt-pc-hdr-work/color-grading-v3. Nothing has been published.

Iteration-3 native menu runs pass 53 expectations each. Installer integrity,
self-test and all 559 embedded payload sizes/hashes pass. The prior local
installers and installed game are unchanged.

## SDR/HDR comparison: shared shadow curve

The smooth shadow toe already applies to both output modes: `ScreenTonemap`
(SDR) and `ScreenTonemapHdr` call the same `ShadowToe` with the same
`graphics.shadow_toe` value, independently of HDR enablement. Defaults and
legacy-settings migration enable the smooth curve in both modes. The 64-cube
float LUT and tetrahedral sampling quality are also shared. Applying another
shadow correction to SDR would apply the adjustment twice.

The backend regression now renders an identical 256-sample scene-linear dark
ramp through both production tonemap branches, checking RGB agreement, black,
monotonicity and the analytic toe for both original and smooth quality. Bloom
and flares are zero to isolate the shadow curve from differing highlight energy.

For a neutral comparison, use the Original HDR color preset, neutral SDR
brightness, the same exposure/camera/effects, and match HDR game brightness to
the SDR display's measured white. Choose SDR EOTF emulation to match that
display's response (Gamma 2.2 remains the default). HDR look intentionally
changes saturation and highlights and is unsuitable for an otherwise neutral
range comparison. This proves shared shadow treatment; it does not claim
pixel-identical scenes when HDR highlight energy affects bloom, exposure or
other spatial/temporal effects, or when display calibration differs.


The cross-mode GPU ramp passes on Vulkan and D3D12. Maximum smooth-toe RGB
difference is 5.21540642e-8 (original toe: 6.70552254e-8), with exact zero black.
The original SDR brightness slider is deliberately preserved: its default
midpoint (5/10) maps to 0.84 and its neutral position (7/10) maps to 1.0. HDR
uses neutral response (1.0), independent of the SDR slider and its saved value.
No additional shadow transform is applied to either mode.
The options regression verifies the original midpoint and monotone, clamped
steps. For a neutral SDR/HDR range comparison use SDR brightness 7/10, Original
HDR grading, and matched display white/EOTF; otherwise SDR correctly reflects
the user's brightness choice. Neither switching HDR nor selecting a grading
preset rewrites that saved SDR choice.

## Vanilla shadow response restored (1.0.8.2-hdr)

This supersedes the smooth-toe behavior described above and in the HDR7/HDR8
historical audit. Upstream v1.0.8 (`3b1392a:shaders/tonemap_curve.glsl`) already
contains `r / sqrt(max(1/512, r))`. Its linear segment near black and square-root
segment above the knee resemble an approximate encoding curve, but it is not
IEC sRGB. The input is already encoded by `SrgbEncode` in the upstream tonemap
pass; treating this extra operation as the sole linear-to-sRGB encoding would
misidentify its domain. Exact sRGB encoding instead uses slope 12.92 below
0.0031308 and `1.055 * L^(1/2.4) - 0.055` above that threshold.
Reference: [ICC sRGB registry](https://registry.color.org/rgb-registry/srgb).

The fork's smoothstep transition was a custom change to this vanilla response,
not a standards-based EOTF correction. It is now removed. SDR uses the upstream
ScreenTonemap function verbatim; HDR retains the equivalent vanilla toe with
no SDR ceiling, following its existing additive linear-light bloom operation.
Old `shadow_toe` settings and the former frame flag cannot reactivate smoothing.
The SDR brightness slider, HDR neutral brightness, user grading controls, LUTs,
post-effect order, and standardized sRGB/gamma display-response emulation are
unchanged. Exact sRGB replaces neither the authored toe nor the whole tonemap.

Validation uses the production GPU branches against the upstream analytic
reference, including black, monotonicity, and the now-unused smooth flag.

Local Release build and all six selected regressions pass: settings roundtrip,
graphics presets, composite output, native settings refresh, HDR pipeline, and
D3D12/Vulkan backend. The upstream SDR function is preserved textually verbatim.
Both backends pass 256-sample vanilla shadow checks for both values of the
obsolete frame flag, with exact black and monotonic output. Evidence and a
pre-change source backup are in `pt-pc-hdr-work/vanilla-shadow-toe`.
