# Native HDR investigation and implementation

Based on the supplied `pt-pc-1.0.8.zip`. This is a native renderer change, not a RenoDX addon. The original archive is unmodified. No original P.T. game assets are included. HDR6 includes readback validation using the user's installed archives. Synthetic GPU tests alone cannot establish visual fidelity in the hallway.

This is the chronological HDR1–HDR4 audit. Later changes are documented in [HDR6](hdr6-rolloff-grain.md), [HDR7](hdr7-post-quality.md), [HDR8](hdr8-exposure-effects.md) and [HDR9 optics](hdr9-optics.md); later revisions supersede earlier details. See [the README](../README.md) for the current feature summary.

See [HDR5 post-processing audit](hdr-postprocess.md) for the current bloom, grain, RCAS and filtering changes; it supersedes the earlier bloom-composition approach below.

## HDR4: full desktop HDR bounds sweep

HDR4 audits shader bounds, implicit attachment limits, CPU color packing, the generated LUT, bloom, materials, screen copies, and the standard/SSR, RT, PT and reconstruction paths. It retains all HDR3 fixes. The installed executable was verified byte-for-byte against HDR3 before reproducing the fluorescent-light scene.

| Area | HDR4 change / audit result |
|---|---|
| Color grade before white | HDR3 only normalized inputs whose linear max exceeded 1. The generated LUT already bakes channel gains (the hallway uses 1, 1.12, 1.3), and can flatten upper midtones earlier. HDR4 uses a reversible N2 max-channel proxy above linear 0.18, approaching 0.5 inside the LUT. For excess `d=maxRGB-0.18`, proxy max is `0.18+d/sqrt(1+(d/0.32)^2)`. Grade the proxy, then restore the same RGB scale. Neutral grades reconstruct the source; dark pixels at/below 0.18 retain the original LUT address. This deliberately changes the upper-midtone/highlight grade to avoid the SDR plateau. |
| Material attachments | Albedo and material MRTs are now RGBA16F. These are not exclusively physical reflectance: legacy eye, hair, incidence, light-cover and view-reflection shaders also put lighting contributions/amplitudes in them. RGB and the specular-amplitude channel can now exceed 1 in HDR. SDR explicitly retains UNORM-like limits/rounding. Roughness, material IDs, translucency, packed vectors and coverage remain bounded. |
| Eye material | Removed the explicit pre-decode RGB saturation in HDR, in both standard and PT G-buffer variants. |
| Material-ID decoding | Round the FP16 material ID before the subsurface integer comparison, preventing half-float representation error from selecting the adjacent material. |
| Bloom storage | All three bloom buffers and their pipelines use RGBA16F instead of 8-bit sRGB. |
| Bloom extraction | Preserve the original polynomial in its SDR domain and extend HDR extraction by a reversible max-channel scale above white. This avoids both the original input/output clipping and an unstable unbounded degree-five polynomial. |
| Bloom blur / accumulation | Removed the HDR Kawase/Kawase-sum RGB ceilings and simulated 8-bit quantization. Gaussian filtering already preserved range. Additive accumulation now has float storage. |
| Bloom composition | Removed the final loss of bloom above 0.5. The original screen-blend portion stays bounded to 0.5 because it is a blend weight; excess bloom is added to the squared signal before the original toe. Feeding an unbounded weight directly into `x*x + b - x*x*b` would darken or reverse highlights. |
| PT indirect / glass / mirror / coat | Removed the fixed exposure-relative sample ceiling (default 8) in HDR. SDR keeps it. Dynamic denoiser neighborhood rejection and final finite-storage guards remain. Valid bright samples may now produce more visible fireflies; this build does not silently replace the removed cap with another artistic ceiling. |
| Optional scene-color reflection readback | Removed its HDR ceiling at 1 and 8-bit quantization. SDR retains the legacy behavior. |
| CPU mesh-particle tint | Bypass legacy byte-wrapped RGB packing in HDR; otherwise values above 1 can wrap around and change hue before the shader. Preserve the authored alpha convention. |
| CPU flare color | Bypass the packed low-precision RGB emulation in HDR. Authored float colors reach the existing float GPU buffers. |
| Subliminal strings | Calculate their contribution with the same extended HDR scene/bloom curve, instead of subtracting two clipped SDR results. |
| Scene-copy noise overlays | Remove HDR scene/color ceilings. Stop inferring a linear scene domain from the FP16 storage format: SceneColor already contains encoded post-process RGB. Match the scene's display shoulder before overlaying the copy, then use the common final reference EOTF once. Ordinary UI stays at reference white. |
| Debug radiance views | Diffuse, specular and mirror debug views preserve HDR values rather than hiding them behind an SDR-white cap. |
| Standard lighting / probes / SSS | Float lighting targets; angular, visibility, BRDF and subsurface blend weights are not RGB white limits. The shared early lighting shoulder remains disabled in HDR. |
| Forward emissives / sky / glass / mirror | HDR2 fixes retained, including extended encoded premultiplication. Material intensity and light colors are uploaded as floats. No new SDR radiance ceiling found. |
| SSR / RT / PT reflections | Existing HDR2 source, blend and storage fixes retained. Temporal bounds are calculated from current local radiance, not fixed to 1. |
| DOF / FXAA / motion blur / history / distortion | Float post targets. Remaining bounds control radii, weights, UVs or temporal neighborhoods. DOF ratio writes alpha only. The retained motion-blur encode/decode pair is a filtering convention, not an additional output EOTF. |
| Upscalers / ray reconstruction | Scene color, demodulated light and output resources remain float with HDR input flags. Neural proxy bounds avoid the singularity when decoding `x/(1-x)`; they are not an SDR output cap. Neural rendering is off in the tested user configuration. |
| Output / frame generation | Common scRGB/PQ conversion and reference-EOTF handling retained. The display peak is the intentional final bound. Frame-generation output-selection tests pass; generated gameplay frames were not tested. |

### Bounds intentionally retained

Opacity/coverage, Fresnel/reflectance factors, normal reconstruction, visibility/AO/shadow ratios, roulette/sample probabilities, coordinates, history rejection, and indices remain bounded. Texture bytes and authored SDR LUT samples cannot recover detail already absent from the assets. The grade proxy avoids the LUT's near-white boundary; it does not claim to recover arbitrary clipping baked into a source texture or every possible artistic LUT. Source mipmap quantization applies to normalized source textures, not the float scene signal. Backdrops, UI assets, noise masks, and SDR/VR output retain their own domains. OpenXR is disabled in this desktop build.

The 60000 max-channel storage rescale/65504 guards remain numerical protection for FP16, not a paper-white limit. Removing those without changing storage and accumulation would cause infinities. The PQ 10000-nit domain and configured display-peak bound are also required. Raising every normalized lighting factor above 1 would change material/visibility behavior rather than recover HDR.

### HDR4 validation, 2026-10-24

* Clang Release native executables and all 95 SPIR-V / 95 DXIL shader variants built.
* Extended the production-shader GPU fixture with bloom extraction, Kawase preservation, identity LUT reconstruction, an intentionally clipping 1.3x grade, monotonic detail below and above white, unchanged dark LUT addresses, and SDR bounds, on D3D12 and Vulkan.
* Negative control: swapping only HDR3's tonemap shader into the new build makes both new backend checks fail. Restoring HDR4 passes.
* Existing output (22), composite/EOTF (14), GPU output pipeline (18 combinations), reflection (46), frame-generation output-selection (10), and settings-roundtrip tests passed. The full backend suite passed.
* Real captures at 960x540, 60 warmup frames, 200-nit white / 1000-nit peak / gamma 2.2: fluorescent tube and hallway in standard, RT and PT on D3D12; tube/PT and hallway/standard on Vulkan. These eight runs exited normally. D3D12 scene logs reported zero debug-layer errors.
* Tube checks also enabled bloom, lens ghosts, depth of field and motion blur (their visibility still depends on scene settings). With pinned EV -3.5 the HDR4 tube view reaches about 211-213 nits across backends/modes, and retains finite scene detail. A matched HDR3 PT capture reaches 189.84 nits (no pixels above 200); HDR4 reaches 213.44 nits (31 pixels above 200). Hallway captures reach the configured 1000-nit ceiling. No nonfinite output samples were found in the nine captured frames.
* DLSS Quality + PT Ray Reconstruction also produced a valid hallway capture. Its already-reproduced headless shutdown hang persists; only that test process was terminated after capture. The user's running installation was not modified.

A lower-exposure tube comparison (EV -4.1, effects off) is instructive: HDR3's pre-grade scene maximum was only about 0.65. HDR4's output maximum is about 184 nits there. A lamp is not guaranteed to exceed paper white when its authored/exposed source is below white. This build preserves the available range; it does not assign extra brightness to every lamp or infer lost lighting from a finished SDR screenshot.

This is a broad source and numerical audit, with representative scene validation, not a full playthrough or a guarantee that every appearance of flattening is a clamp. HDR grade changes, increased G-buffer precision/memory, bright-sample noise, strongly colored LUT transitions, all scene-specific effects and physical monitor mapping still merit gameplay review. The two upgraded G-buffer attachments cost an additional 8 bytes per render pixel (about 63 MiB at native 4K); bloom adds about 1.5 MiB at 16:9. The SDR algorithms remain, but float storage changes rounding and SDR is not bit-identical.

## HDR3: the remaining shared lighting shoulder

HDR2 did not resolve the reported wall/table highlight flattening. The missed operation was `TppShoulder` in `compose.frag`, **before** the post-process tonemap. Despite its name, `RenderScene::ResidentSettings` enables this curve by default in P.T.; it does not depend on TPP fog being enabled. Standard rendering, full ray tracing, and normal path-traced lighting all feed their diffuse/specular buffers through this composite. Forward emissives are drawn afterward and bypass the curve.

For the default threshold 0.3 and range 8, the curve above the threshold is `1.07 - 0.5929 / (x + 0.47)`. Input 8 maps to 1, input 64 to approximately 1.061, and arbitrarily bright lighting approaches 1.07. This is severe SDR highlight compression even though there is no explicit `clamp(color, 0, 1)` and the target is FP16. Removing later clamps cannot recover the lost lighting contrast.

HDR3 skips this early shoulder only when the scene is rendering in HDR. Lighting stays linear through composition, reflection/upscaling and the existing HDR post pipeline. The output composite then maps highlights to the selected screen peak, before UI composition. The SDR branch keeps the original shoulder. Scene values at or below 0.3, including the original linear midgray, are unchanged by this fix. Gamma 2.2 reference emulation, optional sRGB/gamma 2.4, LUT handling, and the HDR2 material fixes remain in place.

### HDR3 validation, 2026-10-24

* Added a regression to `pt_d3d12_backend_test` that renders the **production `compose.frag`**, including the enabled TPP flag and real frame HDR flag. A 256-pixel colored diffuse/specular ramp from 0 to 64 retains its range in HDR and matches the original shoulder in SDR, on both Vulkan and D3D12.
* Negative control: substituting the shipped HDR2 compose SPIR-V/DXIL makes exactly those two checks fail. Restoring HDR3 makes the full backend suite pass.
* Real hallway A/B captures: same camera, pinned EV -4.1, 960x540, 60 warmup frames, 200-nit paper white, 1000-nit peak, gamma 2.2. Standard rendering/SSR, full ray tracing, and path tracing ran successfully with the user's own archives. D3D12 reported zero debug-layer errors in these scene runs.
* In a fixed wall region excluding the lamp itself (x 130..234, y 130..259), output luminance's 95th percentile increased from about 296 to 585 nits in standard rendering and from 296 to 587 nits in path tracing. Its peak increased from about 349 to 804/805 nits. The final frame stayed within the configured 1000-nit RGB ceiling. These are measurements for this test view, not brightness targets for every wall or material.
* DLSS Quality plus ray reconstruction/path tracing also produced HDR output with wall highlights extending above SDR white and a 1000-nit final ceiling. The headless process hung during shutdown after successful capture; the same shutdown hang was reproduced with the unchanged HDR2 executable. This is an existing headless DLSS limitation, not a clean shutdown validation.
* The existing `PT_HDR_DUMP=1` writes final-output float RGB nits as PFM beside captures. Temporary diagnostic readbacks of the post buffers were used during investigation and are not included in the shipped runtime. The normal PNG screenshot path produces an SDR preview and clips values above paper white; use the PFM when judging HDR headroom.

The final HDR3 Release runtime, both shader backends, output/EOTF tests, frame-generation output-selection test, reflection test, and complete backend test suite were rebuilt/rechecked. This is scene/readback validation, not a full playthrough or an assessment of the monitor's PQ tracking. The unrelated original compiler warnings remain.

## HDR2 reflection and material audit

HDR2 fixes additional losses before post-processing, across standard SSR, ray-traced reflections, and path-traced lighting. HDR mode is propagated to all scene views, including mirror captures. The SDR branches retain their original limits.

| Path | Problem and HDR2 change |
|---|---|
| SSR / RT reflection blend | Reflected RGB was clipped to 1 before encoding. Both the base and reflected source now retain HDR range in direct and temporal blending. |
| SSR base / upscale resolve | HDR preservation was incorrectly conditional on RT reflections being enabled. It now follows the actual desktop HDR output mode. |
| Forward glass, mirror, unlit and sky | Removed the encoded output ceiling at 1 and the mirror-source ceiling at 1. Premultiplied opacity and the original blend domain are retained. |
| Path-traced mirror | Removed the independent reflection and lit-dirt ceilings at 1 before the original mirror blend. |
| Emissives in raster / RT / PT | Replaced fixed per-channel 32 ceilings, including the path tracer's exposure-dependent emissive cap, with finite FP16 protection in HDR. |
| Traced GI, glossy and PT layer storage | Replaced per-channel 64 ceilings with a 60000 max-channel safety rescale, preserving channel ratios. This protects storage, rather than mapping to a monitor. |
| RT bounce light | Removed its exposed-luminance ceiling at 1 in HDR. |
| Path denoiser statistics | Moved the four moment histories and two variance buffers to FP32. Squaring unclipped radiance can overflow FP16 statistics; retaining FP16 here would undermine the radiance fixes. Additional memory is 48 bytes per half-resolution pixel (about 95 MiB at native 4K). Radiance/history buffers remain FP16. |
| Imported TPP fog | Scene and particle scattering now preserve the same HDR range. This is not the P.T. hallway's primary lighting path. |
| Mode transitions | Reset post, reflection, mirror, path-tracing and upscaler histories before rendering when changing between SDR and HDR; invalidate stale mirror captures. |

Retained bounds have specific roles: albedo/reflectance, Fresnel, normals, opacity/coverage, visibility, sample probabilities, texture coordinates, finite storage, and dynamic neighborhood rejection for denoising. At the HDR2 stage, the path tracer's separate exposure-relative firefly filter (`PT_PATH_CLAMP`, default 8) remained; it limits individual traced samples and can still suppress extreme outliers. It is not an SDR-white ceiling, but it is a deliberate remaining source of bias. At the HDR2 stage, the experimental `PT_REFLECT_SAMPLE` readback remained bounded because it controls an authored feedback-light multiplier, not the reflection image. HDR4 supersedes these retained HDR2 choices for bloom, path samples and reflection readback as described above.

This is not a claim that every reflection artifact is clipping. Denoising, roughness, texture detail, authored materials and the SDR LUT extension can also reduce visible detail. Full-scene comparisons remain necessary; no user game assets were available for this build's validation.

## Findings in the original 1.0.8 code

The lighting and scene composite already use RGBA16F. The problem is downstream:

```
float scene -> clamp to 0..1 -> sRGB encode + clipped flare
 -> bounded screen/bloom curve -> RGBA8 post-process ping-pong
 -> blur / SDR LUT / screen effects -> RGBA8 scene color
 -> multiply finished SDR RGB by a separate scalar HDR gain -> display encode
```

`shaders/tonemap.frag` clamps the scene before encoding, clamps the flare composite, and invokes the bounded `ScreenTonemap`. `SceneRenderer::Init` selects RGBA8 UNORM for both post-process targets and their history. `Renderer::kSceneColorFormat` adds another RGBA8 bottleneck. Changing only the final tonemap or swapchain cannot preserve RGB detail through these stages.

`shaders/hdr_boost.frag` reads upstream scene lighting, so the original implementation is not purely an inverse tonemap of the final SDR image. However, it reduces that source to one brightness scalar and uses it to brighten already clipped and graded SDR RGB. That cannot reconstruct the lost RGB ratios of colored lights or emissives. The final composite gates this multiplier using `smoothstep(0.4, 0.8, max(gradedRGB))`, so a grade or darkening effect also changes whether the highlight is expanded.

The old `HdrShoulder` maps source brightness 3 to the selected display peak and clips every larger source value to the same peak. At paper white 200 nits and peak 1000 nits, source levels 3, 10, and 100 all become the same 1000-nit multiplier. Raising the display peak boosts the source rather than merely providing room for it. The old unit test explicitly requires this plateau.

The gain map is calculated separately from FXAA, depth of field, motion blur, LUTs, temporal screen blur, and clarity. Its lens distortion uses one sampling position while the visible image uses different positions per channel. Brightness can therefore be spatially inconsistent with the processed image. The scalar is also carried in alpha, complicating fades and overlay coverage.

HDR output uses gamma 2.2 while the internal post chain is sRGB-shaped. That is not automatically a bug: the correct display response depends on the SDR reference. Simply replacing gamma 2.2 with an sRGB decode can lift the shadows compared with SDR viewed on a gamma-2.2 monitor. This build makes that choice explicit; see the EOTF section below.

The existing scRGB scale (`paperWhite / 80`) and HDR10 BT.709-to-BT.2020/PQ encoding are broadly the right output operations. An HDR swapchain is necessary but does not, by itself, fix the upstream information loss. scRGB is a valid native Windows output path and remains supported.

## What RenoDX contributes to the design

The relevant local references are:

* `src/games/hades2/lutsample_0xB9AF8512.ps_6_0.hlsl`: retain the scene color around a bounded LUT proxy and reconstruct range after sampling. Its addon upgrades the swapchain/proxy resources, and its proxy shader calls `SwapChainPass`.
* `src/games/wobblylife/common.hlsli`: compress the full RGB signal into an SDR LUT domain, record the scale, and reconstruct the HDR signal after grading; it also supports more advanced gamut handling.
* `src/shaders/draw.hlsl`: `ComputeUntonemappedGraded`, `ToneMapPass`, and `SwapChainPass` separate grading, forward display mapping, and presentation encoding.

The common principles are upstream scene access, float intermediates, deliberate LUT-domain handling, SDR art-direction preservation, display-dependent highlight mapping, and separate treatment of UI. This patch uses a small GLSL extension of P.T.'s own curve rather than transplanting the entire HLSL library or changing its look with an unrelated filmic curve. It does not claim to implement RenoDRT itself.

## Implemented pipeline

```
float scene -> extended original screen/bloom curve
 -> RGBA16F encoded RGB -> original spatial effects
 -> bounded RGB LUT proxy + range reconstruction
 -> RGBA16F screen/lens effects
 -> selected SDR-reference decode + scene highlight shoulder
 -> inverse reference encode -> fade + UI
 -> brightness + selected reference decode -> scRGB or BT.2020/PQ
```

* Preserve the original screen curve, bloom contribution, and toe below white; extend its RGB calculation above white before the destructive clamps.
* Upgrade post-process ping-pong, history, and scene-color resources to RGBA16F, with stable formats across output toggles.
* For the SDR LUT, decode to linear, apply the HDR4 N2 max-channel proxy described above, encode the bounded proxy, sample the original packed LUT (including transitions), decode and restore the scale, then encode for subsequent legacy post effects. Dark LUT addresses remain unchanged; upper midtones and highlights use the protected proxy. An identity LUT reconstructs the input instead of turning colored highlights white.
* Preserve HDR through clarity, banding correction, backdrop, and subliminal composition. Preserve the original bounded behavior when HDR is off. Retain the original SDR-shaped presentation effects and their ordering.
* Remove the scalar boost texture, shader, extra pass, alpha transport, and brightness gate.
* Apply a hue-preserving maximum-channel shoulder to scene RGB before UI overlays. For display-linear maximum channel `m` and peak/paper-white ratio `p`, values at or below 1 are unchanged. Above 1, use `1 + (p-1)*(m-1)/(m-1+p-1)`. This has unit slope at white, approaches the peak smoothly, never amplifies the input, and has no finite source-white clipping threshold.
* Decode the final encoded RGB with the selected SDR-reference EOTF, preserve the existing scRGB and PQ output conventions, and retain a final peak bound for brightness adjustments. Both ordinary presentation and 1.0.8 frame-generation UI use the same output helper.
* Keep paper white at or below the selected peak. Invalidate post-process histories when switching between extended and SDR operation.
* Disable legacy 8-bit debanding noise in the HDR float path. Precision is retained instead of adding 1/255 noise to avoid a quantizer that no longer exists.

At 200-nit paper white and a 1000-nit peak, display-linear source maxima 1, 3, 10, and 100 map to approximately 200, 467, 754, and 969 nits. These are curve examples, not measurements of the game. These maxima are measured after the selected reference EOTF. Midtones and black do not change when peak brightness changes.

## SDR EOTF emulation and gamma handling

Texture encoding, intermediate storage, and the SDR monitor's light response are separate questions. The port uses explicit sRGB encode/decode functions internally; its upgraded float targets have no implicit sRGB conversions. Their stored RGB still uses the original encoded domain so LUTs and spatial effects keep their expected inputs. The smooth-motion-blur option's paired gamma-2.2 transform/inverse is retained as part of its original filtering, not confused with final presentation decoding.

RenoDX's `src/shaders/colorcorrect.hlsl` implements the forward gamma correction as `pow(SrgbEncode(linear_srgb), gamma)`. Because P.T.'s final intermediate already stores the encoded signal, the equivalent operation is `pow(encodedRGB, gamma)`. Applying both would be a double correction. The original HDR path's gamma 2.2 was therefore a plausible SDR-display emulation choice; its principal defects were the upstream clipping and separate scalar boost.

The new **HDR SDR display response** setting in Display offers:

| Selection | Response to SDR code c | Intended reference |
| --- | --- | --- |
| sRGB | piecewise sRGB EOTF | sRGB reference / color-managed SDR |
| Gamma 2.2 (default) | c^2.2 | gamma-2.2 SDR reference |
| Gamma 2.4 | c^2.4 | gamma-2.4 dark-room SDR reference |

Gamma 2.4 is an ideal power curve, not a full BT.1886 implementation with a measured nonzero display black. The original game's mastering monitor response is not known from the supplied source, and display metadata does not establish it. Gamma 2.2 is an explicit practical default, not a claim about that monitor.

At encoded gray 0.02 and 200-nit paper white, an sRGB decode produces about 0.310 nits, while gamma 2.2 produces about 0.0366 nits. Thus a pure sRGB implementation could visibly brighten a dark hallway relative to a gamma-2.2 SDR reference even though the texture transfer functions round-trip correctly. Both curves retain zero black and the same reference white.

Scene highlights are mapped in the selected reference's linear-light domain, then encoded back with that reference's inverse. The final decode uses the same selection, including UI and the frame-generation UI helper. This keeps paper white and the selected peak in physical units without double correction. The original brightness slider still operates in its original code domain. Changing the EOTF affects only HDR output and applies immediately, after temporal post histories.

The setting is saved as `hdr_sdr_eotf = 1` under `[display]` in `pt.ini`; 0 means sRGB, 1 means gamma 2.2, 2 means gamma 2.4. Old configs use 1 by default. Match the SDR reference at equal paper white and unchanged in-game brightness before assessing differences in shadows. This setting cannot correct a miscalibrated monitor's own HDR PQ tracking.

## Build scope

The final source is based on the supplied **1.0.8 ZIP**, retaining that release's rendering and stability changes. It does not use the older 1.0.4 engine. The standalone native renderer patch does not install ReShade or a RenoDX addon.

The local build uses Clang Release, Vulkan headers/loader 1.4.350, Shaderc v2026.1, and the port's pinned SPIRV-Cross/DXC dependencies. It builds both Vulkan SPIR-V and D3D12 DXIL shaders. Upscaler dependencies remain enabled. OpenXR is disabled for this desktop HDR build. The optional custom ReShade binary for the port's DLSS neural-rendering unlock route is not supplied.

## Validation and remaining limits

`pt_hdr_output_test` checks transfer functions, display selection, calibration limits, and the revised highlight curve. `pt_hdr_pipeline_test` renders the production composite shader on the GPU through an FP16 intermediate and reads emitted values back. It checks black, shadows around the sRGB knee, midgray, paper white, colored HDR detail, coverage alpha, opaque UI, scRGB, and PQ at multiple peaks and all three reference EOTFs. `pt_composite_output_test` also sweeps 4097 code values at four brightness settings for each EOTF and checks inverse/forward transfer pairs. Settings persistence and 1.0.8 frame-generation output selection have separate tests.

HDR2 baseline checks, completed on 2026-10-24:

* Clang Release `pt` and `pt_release`: built; executable version `1.0.8-hdr2`.
* All 95 Vulkan SPIR-V and 95 D3D12 DXIL shaders: built successfully.
* `pt_reflection_mix_test`: 46 GPU checks passed, including distinct HDR reflection steps, unchanged SDR limits, preserved values above 32/64, FP16 safety and invalid-input guards.
* `pt_hdr_output_test`: 22 checks passed.
* `pt_composite_output_test`: 14 checks passed, including the 4097-step ramps across three EOTFs, four brightness settings and both HDR encodings.
* `pt_hdr_pipeline_test`: all 18 combinations passed on NVIDIA GeForce RTX 5090 (three EOTFs, 400/1000/2000-nit peaks, scRGB/HDR10). This renders the production composite shader through an FP16 intermediate; readback tolerances allow its rounding.
* `pt_d3d12_hdr_framegen_test`: all 10 output-selection checks passed. This does not exercise generated gameplay frames.
* `pt_settings_roundtrip_test`: all checks passed, including default gamma 2.2 for old configs, invalid-value bounds, and persistence of the alternatives.
* Existing `pt_d3d12_backend_test`: all checks passed, including shader resource coverage, compute pipeline creation and Vulkan/D3D12 image comparisons. The D3D12 debug layer was off.
* `pt_release.exe --help`: exited successfully.

The first 1.0.8 build exposed a missing D3D12 link dependency in the new GPU test target; the target was fixed and the build and GPU test rerun successfully.

This validates numerical output, not real-scene LUT fidelity. The original archive's unrelated compiler warnings remain.

The SDR algorithm is retained, but upgrading intermediate precision removes original 8-bit rounding. Therefore SDR is not promised bit-for-bit identical. HDR4 extends bloom and replaces the first implementation's bounded extraction as described above. The grade extension outside its authored SDR domain is a reconstruction choice, requiring inspection of strongly colored lights and all LUT transitions. This is a BT.709 working-color implementation with HDR range; encoding it as BT.2020 does not invent wider-gamut source colors.

Broader visual checks still required beyond the HDR3 hallway captures: saturated lamps and flashlight highlights, bloom and lens ghosts, depth of field, both motion-blur orders, fades, pause menus, photo mode, output toggles, and frame generation. Test actual monitor peak behavior separately from shader readback. The synthetic test does not validate the OS compositor, monitor mapping, or full gameplay.

Standards references: [Vulkan color-space definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkColorSpaceKHR.html), [Microsoft Advanced Color and scRGB reference white](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).
