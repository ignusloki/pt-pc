# Upscaling and frame generation

The original renders at 1920x1080 with FXAA. The port can render the scene at a lower internal resolution and
reconstruct the output with a temporal upscaler (AMD FSR 3.1, NVIDIA DLSS 4.5, Intel XeSS, or Apple MetalFX on macOS), or keep the display
resolution and use the upscaler as anti-aliasing (FSR native AA, DLAA). Everything here is off by default; with the
upscaler off the frame is the same as without this feature.

## Settings

`pt.ini`, section `[upscaling]`, and the Upscaling rows of the PC settings page (F10):

| key | values | meaning |
| --- | --- | --- |
| `upscaler` | `off`, `fsr3`, `fsr4`, `dlss`, `xess`, `metalfx` | backend. MetalFX is the Mac backend; the page greys choices this machine or build cannot run, with the reason on the help line |
| `quality` | `native`, `quality`, `balanced`, `performance`, `ultra_performance`, `custom` | render scale 1/1.0, 1/1.5, 1/1.7, 1/2.0, 1/3.0 per axis (the FSR ratios, used for every backend), or `scale` |
| `scale` | 0.25 to 1.0 | render scale per axis for `custom` |
| `sharpness` | 0 to 1 | FSR: RCAS after the upscaler. DLSS has no sharpening |
| `dlss_model` | `auto`, `k`, `l`, `m` | the DLSS Super Resolution model. `auto` is NVIDIA's default per quality (K for DLAA, quality and balanced, M for performance, L for ultra performance). L and M run in FP8, which RTX 20 and 30 cards lack, so they cost about twice as much there |
| `frame_generation` | `off`, `fsr3`, `dlss`, `metalfx`, `xess` | one generated frame between two rendered frames. Needs a window and an upscaler. MetalFX interpolation requires supported Apple silicon, macOS 26 or newer and the MetalFX upscaler |

## What the port does per frame

With an upscaler active the render extent is `output / ratio`, the projection is jittered with the Halton(2,3)
sequence, and material textures are sampled with a mip bias of `log2(render / output)` so the mips are those of a
native frame. The scene passes (G-buffer, SSAO, lighting, forward, effects, reflections) run at the render extent.
Then:

- a reactive mask is written from what the effects pass blended over the opaque scene (particles, glass, blood);
- motion vectors are drawn for the whole frame, and again for objects whose transform or skin changed;
- the flashlight's contribution is divided out of the colour the upscaler sees and multiplied back in after it.
  Without this, the spot moving over still surfaces (whose motion vectors are those of the surface) left dark echo
  rings in the upscaler's history;
- the two fixed per-pixel patterns the original draws (the 8x8 Bayer dither of alpha-tested hair and the 2x2 offset of
  the shadow taps) are cycled frame by frame, because a temporal upscaler keeps a fixed pattern as texture. The history
  averages them into the coverage they stand for;
- the backend writes an output-sized image; the post chain (bloom, tonemap, depth of field, motion blur, colour LUT,
  grain) runs at the output resolution as before. FXAA is skipped: the upscaler is the anti-aliasing.

A camera cut or a missing previous frame resets the upscaler history.

## Backends

### AMD FSR 3.1

FidelityFX SDK 1.1.4 through the FidelityFX API, using AMD's signed `amd_fidelityfx_vk.dll` (MIT),
loaded at run time from the executable's folder; without the DLL FSR is unavailable. Sharpness drives RCAS.

### AMD FSR 4

listed and greyed. AMD has released FSR 4 for DirectX 12 only; the newest FidelityFX release with a
Vulkan backend is 1.1.4, which has no FSR 4 provider. The row looks for a `4.x` upscaler provider in
`amd_fidelityfx_vk.dll`, so a later Vulkan DLL from AMD that carries one would be picked up. A `pt.ini` with
`upscaler = fsr4` renders without an upscaler until then.

### NVIDIA DLSS

DLSS SDK 310.9.1, `nvngx_dlss.dll` next to `pt.exe`. The qualities map to DLAA, MaxQuality, Balanced,
MaxPerf and UltraPerformance. NGX is loaded only once DLSS is selected; loading it starts the driver's `nvngx_update.exe`,
the model updater every DLSS game starts. DLSS computes its own exposure, which handles P.T.'s dark frames better than the
frame's exposure value. If the driver asks for Vulkan extensions the device was not created with, selecting DLSS reports
that a restart is needed.

### Intel XeSS

XeSS SDK 3.0.2, `libxess.dll` next to `pt.exe`, loaded when XeSS is selected. GPUs other than Intel Arc
run its DP4a model. The qualities use the XeSS preset with the same ratio; `custom` takes the nearest.

The three SDKs are Windows binaries, so the upscalers are left out of the Linux build (docs/linux.md).

## Frame generation

### FSR 3

AMD FSR 3.1 frame generation from the same DLL, with the Vulkan frame interpolation swapchain. It needs an
active upscaler (FSR, DLSS or XeSS; the interpolation reads the upscaler's depth and motion vectors). While it is on the
swapchain presents with v-sync whatever the v-sync setting: with v-sync off the driver's present call blocked within
seconds in every test run here. Each rendered frame and its generated frame take whole refreshes, so a frame that does
not fit two refreshes takes three; at 2560x1440 DLAA on a 260 Hz display this showed 176 frames a second but rendered 88
instead of 162. The UI is drawn into the rendered frames only and is not interpolated. AMD lists the Radeon RX 5000
series as the minimum, and the port checks for it: on a GCN card (RX 400 and 500, Vega) the row is greyed, and a
`pt.ini` asking for it renders without. A tester's RX 580 lost the Vulkan device repeatedly with it on.

### DLSS Frame Generation

through NVIDIA Streamline 2.14.1. It needs a GeForce RTX 40 or newer, a driver with DLSS
Frame Generation, and hardware-accelerated GPU scheduling turned on in Windows graphics settings; the row names which
of these is missing. It is experimental: I have no RTX 40 or 50, so the generated frames themselves have never run on
hardware here. Streamline loads only for a start whose `pt.ini` has `frame_generation = dlss` and a window; choosing
DLSS in the menu takes effect at the next start. If a start with Streamline crashes or hangs, the next start leaves it
out, sets `frame_generation = off` and greys the row for that session. On Vulkan Streamline cannot present with
v-sync, so the swapchain runs without it while frame generation is on, and the fps limit goes to Reflex's frame
limiter. In menus and the paused game frame generation is off. A build configured with `-DPT_STREAMLINE=OFF` leaves the
code and the DLLs out. Not covered: the UI alpha tag (the HUD-less copy alone separates the UI), the distortion field,
multi frame generation (RTX 50).

If you have an RTX 40 or 50: set `frame_generation = dlss` with DLSS or FSR in `pt.ini`, start in a window, and check
`pt.log` for `streamline: DLSS ok, DLSS Frame Generation ok` and the summary `presented N frames for M rendered` at
exit (N about 2 M). The Streamline log is `streamline/sl.log` next to `pt.ini`. An issue with that log is welcome
either way.

Licences in `licenses/` next to `pt.exe`: Streamline's (MIT), the NVIDIA RTX SDKs licence (DLSS and `nvngx_dlssg.dll`)
and Reflex's. Only the signed production DLLs ship.

## DLSS 5

DLSS 5 neural rendering is not in the port. NVIDIA's public SDKs (DLSS 310.9.1, Streamline 2.14.1) carry no plugin,
header or runtime for it, and the community route runs through an unsigned DLL taken from a game, which the NVIDIA RTX
SDKs licence forbids using. If NVIDIA publishes it for Vulkan, the port has the inputs it takes (the tonemapped frame
before the UI, depth, motion vectors, the G-buffer).

## What to expect

Measured on an RTX 3090 at fixed poses against a 4x supersampled render: DLAA and DLSS quality score at or above the
native frame on still views, the other modes come within 1 to 4 dB of it (the lower the render resolution, the
further), and all three upscalers follow thin geometry in motion (window mullions, picture frames) more steadily than
the original's FXAA. GPU time at 1920x1080 in the corridor, native with
FXAA 3.1 ms: FSR quality 2.7 ms, DLSS quality 3.0 ms, XeSS quality 3.2 ms; at 3840x2160, native 10.3 ms against 7.7, 8.5
and 8.2 ms. The native AA modes cost 0.8 to 1.1 ms more than native at 1080p. On RTX 20 and 30 the DLSS models M and L
cost about twice K, so DLSS performance can take more GPU time than DLSS quality there.

Known differences: under FSR the wet asphalt of the ending street comes out a few levels darker under the flashlight
(its accumulation loses some of the sparkle); the eye adaptation is a little behind native for the first second after
a cut in every mode.

## DLSS Frame Generation on RTX 20 and 30

DLSS Frame Generation needs an RTX 40 or newer card. On an RTX 20 or 30 the row is greyed out, unless RTX30MFG-Unlock
(github.com/mcsoderh/RTX30MFG-Unlock) sits next to pt.exe: `RTX40MFGCore.dll`, `RTX40MFG.asi` and the Ultimate ASI Loader as
`version.dll` with its `global.ini` values in `version.ini`. The game then lets Streamline decide, and Streamline accepts the
card. Tested once on an RTX 3090: it generates frames. It is third-party and experimental; nothing of it ships with the game.

### Apple MetalFX

macOS only (`metalfx_backend.mm`). MoltenVK's `VK_EXT_metal_objects` gives the Metal textures behind the scene's
images, and `MTLFXTemporalScaler` upscales them on its own Metal queue. Two Vulkan timeline semaphores exported as
Metal shared events keep the queues in order. The renderer submits the scene inputs before committing the MetalFX
command buffer; the remaining Vulkan work waits for MetalFX's output signal. Synchronization resources stay alive
until the frame fence completes, and timeline values keep increasing across settings and size changes.
The qualities use the same ratios as FSR. The frame's exposure is converted to an R16Float texture for MetalFX;
MetalFX has no sharpening. Upscaling remains off by default.

## Frame generation

### FSR 3 frame generation on GCN

AMD lists the Radeon RX 5000 series (RDNA) as the minimum for FSR 3 frame generation, and the port checks for it: on an AMD GPU that runs wave 64 only (`VkPhysicalDeviceVulkan13Properties` `minSubgroupSize` and `maxSubgroupSize` both 64, every GCN part from the RX 400 and 500 series to Vega; RDNA reports 32 to 64) `FsrFrameGenHardware` (`upscale.cpp`) reports FSR 3 frame generation unavailable, the Frame generation row shows FSR 3 greyed with `pc_note_fsr_fg_gpu`, and a `pt.ini` with `frame_generation = fsr3` renders without it (`UpdateFrameGeneration` drops a wanted frame generation that is unavailable). The check runs before the window check, so the log says `frame generation: unavailable: needs an AMD Radeon RX 5000 series or newer GPU (<name> is GCN: subgroup size 64 to 64, PCI device 0x....)` headless too. `PT_FSR_FG_ALLOW_UNSUPPORTED=1` skips it (a test override, never set by the game). `pt_upscaler_gpu_test` covers the rule. The evidence (2026-10-07, build 4be56b6, 0.9.1 hotfix): a tester's Radeon RX 580 2048SP (a Polaris 20 at RX 570 speed, driver 2.0.279 of AMD's legacy branch, 7.8 GB device memory) lost the device six times within six minutes, five at frame submit and one at present, with FSR 3 upscaling at native AA and FSR 3 frame generation on, 3272 of 7368 MB in use (no memory pressure) at 1920 x 1080, with the RivaTuner Vulkan layer and the OBS hook loaded. The minidumps hold no device fault description (the AMD driver reported `VK_EXT_device_fault` but the dumps carry the stack only). The upscaler alone is within AMD's list (FSR 3.1 upscaling runs from the RX 590 on, and its shaders fall back to FP32 where `shaderFloat16` is off, as on GCN), so it stays offered; if FSR 3 upscaling alone still loses the device on that card, the next step is the FidelityFX debug checking (`PT_UPSCALE_CHECK=1`) with the Vulkan layers of RivaTuner and OBS removed.

AMD FSR 3.1 frame generation from the same `amd_fidelityfx_vk.dll` (FidelityFX API: frame generation 3.1.4 and the Vulkan frame interpolation swapchain). It shows one generated frame between two rendered frames. It needs a window and an active upscaler (FSR, DLSS or XeSS, any quality, native AA included): the interpolation reads the depth and motion vectors of the upscaler inputs at the render resolution. The PC settings page (F10) and the settings window of `--dev-ui` offer it when the DLL has frame generation and an upscaler is on; switching it on or off recreates the swapchain. While it is on the swapchain presents with v-sync (FIFO) whatever the v-sync setting, and the interpolation swapchain paces the rendered and generated frames between the refreshes. The Frame generation row offers Off, FSR 3 and DLSS; a choice this machine cannot run shows greyed with its reason (FSR 3 headless: no window; DLSS: below).

- Swapchain: `vk::Context` hands swapchain creation, destruction, image queries, acquisition and present to a `vk::SwapchainHooks` owner. The first creation makes the FidelityFX swapchain context, later ones (resize, vsync) go through its replacement functions. The back buffer goes to present in `SHADER_READ_ONLY_OPTIMAL`, the layout the interpolation swapchain expects.
- Queues: with a window the device gets three more queues at start, as in the SDK samples: a second queue of the graphics family that presents the rendered and generated frames (on GPUs with a single graphics queue a compute queue that can present), a transfer queue for image acquisition and a compute queue (pt.log names them). Async workloads are off, so the interpolation runs on the game queue during present. `vkDeviceWaitIdle` first waits for the swapchain's presenter thread (`FGSWAPCHAIN_WAIT_FOR_PRESENTS`), which still submits to the present queue after the game queue is idle.
- FIFO: with MAILBOX or IMMEDIATE (v-sync off) the presenter thread's present call blocked inside the NVIDIA driver (`NtDxgkSubmitPresentToHwQueue`) within seconds and the game then waited for it forever: 9 of 9 stress runs (the start route with DLSS quality and a screenshot every 20 pan frames, RTX 3090, driver 617.14, hardware-accelerated GPU scheduling on), with and without OBS's implicit Vulkan layer (`graphics_hook64`, loaded into every Vulkan program when OBS is installed), with the present queue on the compute family, with 4 swapchain images, with the window on top and uncovered, and with FidelityFX's own present-queue composition. With FIFO every run passed, the user's setting (2560x1440, v-sync on at 260 Hz, DLAA) included. `PT_FG_PRESENT_MODE=mailbox|immediate` restores the other modes for tests.
- FidelityFX 1.1.4 workarounds (`frame_generation.cpp`):
  - The swapchain keeps 8 command lists per queue family and dereferences a null pointer when all of them are still in flight (fixed in FidelityFX 2.0, which has no Vulkan backend). Here the game and present queues share one family; with the UI composition on the game queue a frame took five lists (interpolation, two compositions, two copies) and a run crashed when the GPU fell behind under load. The composition stays on the present queue, the swapchain's own choice for a present queue that can draw (three lists per frame), and before presenting a frame the port waits for the game queue to finish the previous frame and its interpolation (a fence submitted after each present), so at most seven lists are in flight.
  - When the real swapchain image cannot be acquired (`VK_ERROR_OUT_OF_DATE_KHR` on every resize, `VK_NOT_READY`, `VK_TIMEOUT`) the presenter thread still signals that present's binary semaphore without presenting, and a later present signals it again ("previously signaled ... not since been waited on"). The present queue's `submitFunc` drops binary semaphore signals from submissions without command buffers, the only kind that path makes, and keeps the timeline signals the swapchain waits on (16 resizes: 64 signals dropped, no validation message).
  - The presenter thread presents and submits on the present queue while the game thread can wait for idle (the swapchain's own `waitForPresents` calls `vkQueueWaitIdle` and `vkDeviceWaitIdle`), which Vulkan requires to be externally synchronised. The DLL's imports of `vkQueuePresentKHR`, `vkQueueWaitIdle` and `vkDeviceWaitIdle` are redirected to wrappers that share one mutex with the present queue's submissions and the port's `vkDeviceWaitIdle`.
  - The replacement `vkAcquireNextImageKHR` returns `VK_NOT_READY` while the game is a whole swapchain ahead of the presenter; the renderer then waits for a free buffer (0.1 ms steps, at most 1 s) and skips the frame on any other failure instead of waiting on a semaphore that is never signalled.
- Per frame: after the upscaler, `ffxDispatchDescFrameGenerationPrepare` with the upscaler's jitter, motion vector scale, depth (reverse-Z, infinite) and camera values, and the camera position and axes (`ffxDispatchDescFrameGenerationPrepareCameraInfo`); before present, `ffxConfigureDescFrameGeneration` with the frame ID, the HUD-less image and the interpolation callback. A camera cut or a frame without upscaler data resets the interpolation.
- UI: the renderer draws a HUD-less copy of each frame into one of the HUD-less targets (one per swapchain image): the same composite passes (fade, output brightness) without the game UI (menus, the PC settings page, subtitles, the subliminal messages) and the ImGui windows, which are then drawn into the frame as before. Where the frame differs from the HUD-less copy the interpolation takes the pixels of the rendered frame, so the UI is not interpolated.
- `PT_FG_DEBUG=1` draws the SDK's tear and pacing lines, `PT_FG_ONLY_GENERATED=1` presents only the generated frames, `PT_FRAME_STATS=1` logs the frame intervals at exit (`frame pacing` for the game loop, `frame generation` for the rendered frames with their generated count).

With `--validation` the FidelityFX DLL still reports messages from its own objects: pipelines with a line width of 0, a descriptor pool without storage buffers, a storage image format that differs from its shader declaration, the one binary semaphore its present-queue composition signals for every present while the presentation engine may still wait on it (the present queue runs the waits in order), and objects left at device destruction (its swapchain creates three of its present semaphores twice and loses the first ones, 3 more for every swapchain creation); none comes from the port's frame generation calls.

### NVIDIA DLSS Frame Generation: what this machine has

DLSS Frame Generation (DLSS-G) runs through Streamline on Vulkan and needs a GeForce RTX 40 or later GPU, hardware-accelerated GPU scheduling (Windows 10 20H1 or later) and Streamline's Reflex (Streamline 2.14.1, `ProgrammingGuideDLSS_G.md` sections 1.0 and 8.0). After device creation `UpscaleHost::CheckDlssFrameGen` finds which requirement fails first, so the page can grey the DLSS choice with the matching reason (`DlssFrameGenSupport::note`): a GPU that is not NVIDIA's or older than Ada (`pc_note_dlssg_gpu`), the driver (`pc_note_dlssg_driver`), hardware-accelerated GPU scheduling off (`pc_note_dlssg_hags`, read from the adapter's WDDM 2.7 caps through `D3DKMTQueryAdapterInfo`, `HardwareGpuScheduling` in `upscale_platform.cpp`), or a build without Streamline (`pc_note_dlssg_missing`). The GPU check asks NGX first (`NVSDK_NGX_VULKAN_GetFeatureRequirements` with `NVSDK_NGX_Feature_FrameGeneration`, which needs no NGX initialisation). With a window it answers (RTX 3090: support bits 0x4, AdapterUnsupported, minimum architecture 0x190, Ada). Headless it returns `NVSDK_NGX_Result_FAIL_NotImplemented` (0xBAD00012, driver 617.14), and then the PCI device ID decides. NVIDIA numbers its chips by generation, and every Turing and Ampere part (TU1xx 0x1E00 to 0x21FF, GA10x 0x2200 to 0x25FF) lies below 0x2600, while Ada starts at AD10x 0x2680. On the RTX 3090 (0x2204) the log reads `frame generation: NVIDIA DLSS Frame Generation unavailable: needs an NVIDIA GeForce RTX 40 series or newer GPU (query failed 0xbad00012; PCI device 0x2204, before Ada (0x2600))`, and the row shows DLSS greyed with that reason (`tools/upscaler_menu_check.py`). Streamline's own `slIsFeatureSupported` is the final check; it runs at the start that loads Streamline. The RTX 30 unlock used by the user's rtx-unlock tool (the third-party `dlssg_for_sm86` proxy) is not part of the port and is never looked for.

### NVIDIA DLSS Frame Generation through Streamline (experimental)

DLSS Frame Generation runs through NVIDIA Streamline 2.14.1 (github.com/NVIDIA-RTX/Streamline, `src/engine/render/upscale/streamline.cpp`). It is experimental. No GPU the project has (RTX 3090, RTX 3050 laptop) can run DLSS Frame Generation, so the frame generation itself has not run on hardware. It has to be verified on an RTX 40 or 50 before a public release. A release build configured with `-DPT_STREAMLINE=OFF` leaves out the code and the DLLs.

- When Streamline loads: only for a start whose `pt.ini` has `frame_generation = dlss` and a window (`StartStreamline` in `main.cpp`), or with `PT_STREAMLINE=1` (tests, headless included). Choosing DLSS in the menu takes effect at the next start (`pc_note_dlssg_restart`). Without it the game never loads a Streamline file, and the frame is the one of the builds before.
- Crash guard: `streamline-starting.txt` next to `pt.ini` stands while Streamline starts and through the first 600 rendered frames, and is removed at exit. A start that finds it (the last one crashed or hung there) leaves Streamline out, sets `frame_generation = off` and greys DLSS with `pc_note_dlssg_failed` for that session.
- Loading: the interposer's signatures (Windows and NVIDIA's own certificate, `sl_security.h`) are checked before `LoadLibrary`. Then `slInit` runs with sl.dlss, sl.dlss_g, sl.reflex and sl.pcl, Vulkan, frame-based tagging and NGX updates; plugins downloaded over the air are not loaded. volk loads Vulkan through the interposer's `vkGetInstanceProcAddr` (`vk::Context::loader`), so Streamline's proxies create the instance, the device and the swapchain with the extensions and queues its plugins need. Its device proxy adds `VK_NV_low_latency2` without `VK_KHR_present_id` and creates private data slots without `privateData`. The port adds both with a window (`UpscaleHost::DeviceSetup`); headless the one message about `VK_NV_low_latency2` stays. `slShutdown` runs after the swapchain is destroyed and before `vkDestroyDevice` (`vk::Context::before_device_destroy`).
- Support: after device creation `slIsFeatureSupported` with the `VkPhysicalDevice` decides for DLSS, DLSS Frame Generation, Reflex and PCL. On the RTX 3090 it logs `DLSS ok, DLSS Frame Generation no supported adapter, Reflex ok, PCL ok`, and sl.dlss_g unloads itself ("Disabling DLSS-G since it is not supported on current hardware").
- DLSS Super Resolution goes through sl.dlss while Streamline is loaded (`SlDlssBackend`), so NGX has one owner. Streamline's open issue 95 reports NGX used directly next to Streamline's frame generation failing on Vulkan with "multiple active devices". The mode follows the quality, and the model, auto exposure and pre-exposure follow the direct NGX path (`DlssPresetHint`, `DlssAutoExposure`). The inputs are tagged for the evaluation: colour, output, depth, motion vectors and exposure. The reactive mask is left out: Streamline's Vulkan format table (`sl.chi` `getFormat`) has no `R8_UNORM` and logged "Cannot have undefined format" four times a frame for it, and the DLSS 4 models do not read the bias mask. At the `render_check` poses (960x540, `PT_STREAMLINE=1`) quality, native AA and performance are bit-identical to the direct NGX path (mean difference 0.000, largest 0). That checks the jitter sign, the motion vector scale (UV units: `mvecScale` 1) and the exposure through Streamline.
- Frame and constants: `Renderer::BeginFrame` takes a new frame token, calls the Reflex sleep (`slReflexSleep`) and sets the PCL simulation and render submit markers. `EndFrame` sets the submit end and present markers around `vkQueuePresentKHR`. The common constants are set once per rendered frame before the upscaler (`SceneRenderer::RecordUpscale`). They hold the unjittered projection, the clip-to-previous-clip matrix from the previous unjittered view projection (computed in double precision), the jitter in pixels, `mvecScale` 1, the camera axes, near 0.05 with far 100000 (the projection has no far plane), reverse depth, and camera motion included. Streamline's matrices are row major for row vectors, the same memory as glm's. The Reflex sleep sits at the start of the rendered frame, not before the simulation, so Reflex's latency gain is smaller than it could be.
- Frame generation (`DlssFrameGeneration`): after the upscaler the depth and motion vectors are tagged (`eOnlyValidNow`: Streamline copies them). Before present the HUD-less copy is tagged (one target per swapchain image, drawn by the renderer as for FSR 3) and the mode is set: on with one generated frame, off in menus and the paused game (Streamline's guide 6.4) with `eRetainResourcesWhenOff`. `slDLSSGGetState` is read every frame; a status other than OK turns frame generation off for the session. Switching the option off or on in play recreates the swapchain with sl.dlss_g unloaded or loaded (guide 18.0).
- Pacing: on Vulkan Streamline's frame generation cannot present with v-sync (`eVSyncOffRequired`; v-sync with frame generation is D3D12 only, guide 22.5). While it is on, the swapchain is created without v-sync (`vk::Context::force_vsync_off`) and the V-sync row says so (`pc_note_vsync_dlssg`). In play the fps limit goes to Reflex's frame limiter for the rendered frames (`frameLimitUs` = 2 / limit), and the port's own sleep is skipped. In menus frame generation is off, and the 60 fps menu cap and the fps limit work as without it.
- Not covered: the UI alpha tag (the HUD-less copy alone separates the UI), the bidirectional distortion field (the lens distortion is applied to the HUD-less copy and the frame, but the depth and motion vectors are undistorted), PCL ping messages, and multi frame generation (RTX 50).
- Test, on an RTX 40 or 50 with hardware-accelerated GPU scheduling on: set `frame_generation = dlss` with DLSS or FSR in `pt.ini`, start the game in a window, check `pt.log` for `streamline: DLSS ok, DLSS Frame Generation ok`, `frame generation: DLSS Frame Generation mode on` and the summary `presented N frames for M rendered` at exit (N about 2 M), and check that the menus turn it off. With `PT_FRAME_STATS=1` the frame pacing line gives the rendered rate. The Streamline log is `streamline/sl.log` next to `pt.ini`.
- Developer test override: `PT_DLSSG_ALLOW_UNSUPPORTED=1` skips the port's GPU and driver checks; Streamline's own check still decides. The third-party `dlssg_for_sm86` unlock (RTX 20/30) is D3D12 only by its author's statement and is never shipped or looked for.

Licences (`licenses/` next to `pt.exe`): Streamline's `license.txt` (MIT for Streamline; the NSight Perf part, `sl.nvperf`, is not shipped), `nvngx_dlss.license.txt` (the NVIDIA RTX SDKs licence, which covers `nvngx_dlssg.dll`; redistribution in object code inside an application) and `reflex.license.txt`. Only the signed production DLLs of `bin/x64` ship (`sl.interposer`, `sl.common`, `sl.dlss`, `sl.dlss_g`, `sl.reflex`, `sl.pcl`, `nvngx_dlssg.dll`). `NvLowLatencyVk.dll` is left out: sl.reflex uses `VK_NV_low_latency2` where the driver has it, and the Reflex supplement names no such DLL as distributable. The NVIDIA RTX SDKs licence asks for the NVIDIA marks in the credits of a game that ships DLSS, and for a notice to NVIDIA before a commercial release (the port is not commercial).

## DLSS 5 neural rendering: not available natively

DLSS 5 "3D-guided neural rendering" (NVIDIA, 1 September 2026, NBA 2K27, GeForce RTX 50 only) is not in the port. Checked 2026-10-06:

- NVIDIA's public SDKs have no way to it. Streamline 2.14.1's changelog announces a plugin `sl.dlss_nr`, and `sl_consts.h` declares `kFeatureDLSS_NR = 1004` with the "uplift" buffer types (`kBufferTypeUpliftInputColor` 70, `UpliftOutputColor` 71, `UpliftControlMask` 72). But the release zip and GitHub main ship no `sl.dlss_nr.dll`, no `sl_dlss_nr.h` (its options API), no guide and no NR runtime. The DLSS SDK (310.9.1) does not mention it, and developer.nvidia.com lists DLSS 4.5 only.
- The community route (rtx-unlock's "DLSS 5 Autopilot": ReShade, the RenoDX DLSS 5 add-on and an `nvngx_dlssnr.dll` 310.8.0 taken from a game) is D3D12 only and drives NGX feature 18 (`NVSDK_NGX_Feature_Reserved18`). Its parameters (PEQHUB/RenoDX-DLSS5-Generic `dlssnr.hpp`, MIT) are `DLSSNR.Color` (an LDR proxy the add-on encodes from the game's colour with a paper-white gain), `DLSSNR.Output`, `DLSSNR.MVec`, `DLSSNR.Depth`, the subrects, `MVecScaleX/Y`, `DepthInverted`, `Reset`, `Intensity`, `LocalToneStrength`, `LocalStructureStrength`, `SkinStructureStrength`, `UseAutoMask`, `Style` and `UICorrection`. It calls the DLL's exports directly instead of the NGX core, and patches the DLL's `GetModuleFileNameW` import so the DLL takes its caller for `nvngx.dll`.
- Probe through the port's own NGX Vulkan path, on the RTX 3090 with driver 617.14 (a developer build, removed again). The DLL has the Vulkan exports. `NVSDK_NGX_VULKAN_GetFeatureRequirements(18)` returned 0xBAD00012 (NotImplemented) and `NVSDK_NGX_VULKAN_CreateFeature(18)` 0xBAD0000B (UnableToInitializeFeature). The NGX log shows the feature is not denied for the port ("Feature dlssnr, denial value 0"). The core refuses the DLL instead: "nvLoadSignedLibraryW() failed ... The digital signature of the object did not verify". Every copy on the machine is modified or unsigned: the one that runs on RTX 20/30 (310.8.0, Authenticode hash mismatch) and the "310.8.SF.0" builds (not signed). An unmodified signed runtime would meet NGX's architecture gate, which the same log shows refusing snippets built for newer GPUs ("snippet is using newer GPU arch 190 > 170" for DLSS Frame Generation).
- Using those copies would take what the add-on does: bypassing NVIDIA's signature and caller checks. The NVIDIA RTX SDKs licence forbids circumventing its technical limitations, so the port does not do it, and it ships no NR runtime.
- OpenDLSS-NR (maanHimself, MIT) reimplements the 310.8.0 network in Vulkan compute, bit-exact, with weights the user supplies. It needs FP8 cooperative matrices (`VK_NV_cooperative_matrix2`, `VK_EXT_shader_float8`, `VK_NV_cuda_kernel_launch`), so Ada or newer, and it ships no weights.

What a native option would need: NVIDIA publishing `sl.dlss_nr` (header, guide and signed plugin) or the NGX feature with a signed redistributable runtime, with Vulkan support, and an RTX 50 to test on. The port has the inputs such an option takes: the tonemapped frame before the UI, depth, motion vectors and the G-buffer (albedo, normals).

## Measurements

RTX 3090, driver 617.14, headless runs at fixed poses (`scamera`). Quality numbers are taken with the post effects that depend on the resolution off (FXAA, DOF, bloom, grain, distortion, motion blur).

Still poses (`--shot-warmup 48`, the corridor and lobby poses of f020, f090 and f160): PSNR in dB against a 3840x2160 render box-filtered to 1920x1080, then against the native 1920x1080 frame.

| mode, 1920x1080 output | render | f020 corridor | f020 lobby | f090 corridor | f090 lobby | f160 corridor | f160 lobby |
| --- | --- | --- | --- | --- | --- | --- | --- |
| native | 1920x1080 | 37.9 | 45.2 | 41.6 | 42.6 | 44.8 | 43.4 |
| FSR native AA | 1920x1080 | 37.4 / 39.5 | 45.7 / 45.5 | 40.9 / 42.4 | 44.6 / 45.7 | 45.9 / 47.6 | 45.0 / 47.4 |
| FSR quality | 1280x720 | 36.4 / 38.1 | 45.4 / 44.4 | 39.9 / 41.2 | 43.4 / 43.4 | 45.6 / 45.9 | 44.3 / 45.0 |
| FSR balanced | 1129x635 | 36.0 / 37.5 | 44.7 / 43.9 | 39.5 / 40.6 | 42.9 / 42.6 | 45.3 / 45.3 | 43.7 / 44.2 |
| FSR performance | 960x540 | 35.5 / 36.6 | 44.0 / 43.0 | 38.8 / 39.7 | 41.9 / 41.7 | 45.0 / 44.5 | 42.8 / 43.0 |
| FSR ultra performance | 640x360 | 33.9 / 34.6 | 42.3 / 41.5 | 37.3 / 37.7 | 39.6 / 39.4 | 43.9 / 43.0 | 40.4 / 40.4 |
| DLAA | 1920x1080 | 38.0 / 40.7 | 46.8 / 46.6 | 41.4 / 43.4 | 45.0 / 46.3 | 46.6 / 49.0 | 45.3 / 48.4 |
| DLSS quality | 1280x720 | 37.7 / 39.9 | 46.6 / 46.0 | 40.8 / 42.5 | 44.6 / 45.5 | 46.5 / 48.2 | 45.1 / 47.7 |
| DLSS balanced | 1129x635 | 37.6 / 39.7 | 46.5 / 45.9 | 40.8 / 42.4 | 44.6 / 45.3 | 46.5 / 48.1 | 45.1 / 47.6 |
| DLSS performance | 960x540 | 37.4 / 39.7 | 46.0 / 45.5 | 40.4 / 42.1 | 43.8 / 44.6 | 45.4 / 47.1 | 44.4 / 46.5 |
| DLSS ultra performance | 640x360 | 36.4 / 38.1 | 45.2 / 44.6 | 39.7 / 40.9 | 42.7 / 43.0 | 45.1 / 45.9 | 43.8 / 45.0 |
| XeSS native AA | 1920x1080 | 37.2 / 39.8 | 46.7 / 46.4 | 40.8 / 42.9 | 44.4 / 45.3 | 46.1 / 48.6 | 44.7 / 47.2 |
| XeSS quality | 1280x720 | 37.1 / 39.3 | 46.5 / 45.5 | 40.6 / 42.6 | 44.0 / 44.4 | 46.2 / 47.5 | 44.6 / 46.5 |
| XeSS balanced | 1129x635 | 37.0 / 39.2 | 46.4 / 45.6 | 40.5 / 42.3 | 43.8 / 44.2 | 45.9 / 47.4 | 44.5 / 46.3 |
| XeSS performance | 960x540 | 37.0 / 39.1 | 46.3 / 45.7 | 40.3 / 42.0 | 43.6 / 44.3 | 46.0 / 47.0 | 44.4 / 46.4 |
| XeSS ultra performance | 640x360 | 36.7 / 38.6 | 46.2 / 45.4 | 39.9 / 41.3 | 43.1 / 43.3 | 45.7 / 46.9 | 43.9 / 45.3 |

The native frame samples the texture mips of a 1920x1080 frame like the original and the supersampled reference one mip finer, so the native AA modes can score below native against the reference on textured walls (f020 corridor).

Flashlight trail (step 7): the f070 wall facing the first leg, the view circling 0.08 rad around its rest once a second with the mouse (the light follows through its eased target), `--render-all` at 60 Hz, the user's settings with grain off, 8 frames after two circles; mean |upscaled - native| (8-bit levels) over the frame while it moves and, in brackets, for the same pose held still. Without step 7: DLSS K DLAA 1.88 (0.27), with auto-exposure 1.14; M DLAA 0.52 (0.33); M quality, balanced, performance 0.96, 1.04, 1.14 (0.48 to 0.60), a dark crescent behind the spot in performance; FSR native AA 0.30, XeSS native AA 0.13. With step 7 (1920x1080): K DLAA 0.28 (0.19); M DLAA, quality, balanced, performance 0.20, 0.34, 0.34, 0.37 (0.31, 0.47, 0.51, 0.55); FSR 0.12; XeSS 0.13; at 2560x1440 M 0.22, 0.33, 0.36, 0.34. Moving is at or below still in every mode and no echo ring or crescent shows in the frames. The same at 2560x1440 with `--display-rate 165` (the user's DLAA): K 1.82 before. With the camera held still and only the light stick circling (all motion vectors zero) K trailed as much (1.33), so the vectors were not the cause. 4e2ee38 had the same trail (1.84). Data in `dump/phase2/flashlight`.

Motion vectors: a 90 frame pan with strafe in the corridor (`--render-all`, every frame captured); the previous frame warped by the motion vectors matches the next native frame at 42.9 dB (median; 30.2 dB unwarped).

Thin geometry in motion: a slow pan (0.02 degrees, about 0.6 pixels per frame) across the far window and the picture frames of the f020 corridor, 90 frames after 30 frames of history, against the 4x supersampled render of each frame, around the window. The second column is the mean change of the per-pixel error between consecutive frames in 8-bit levels (the reference itself changes by 1.25 per frame); lower means less crawling and flicker. In a time slice across the window mullions the native edges step pixel by pixel and FXAA flickers on the thinnest lines, while every upscaler mode follows the smooth slope of the reference.

| mode | PSNR dB | error change per frame |
| --- | --- | --- |
| native, FXAA off | 34.8 | 1.44 |
| native with FXAA (original) | 35.8 | 1.32 |
| FSR native AA | 36.8 | 1.16 |
| FSR quality | 35.7 | 1.25 |
| FSR performance | 35.2 | 1.28 |
| DLAA | 37.9 | 1.08 |
| DLSS quality | 38.0 | 1.10 |
| DLSS performance | 37.1 | 1.18 |
| XeSS native AA | 37.0 | 1.15 |
| XeSS quality | 36.3 | 1.23 |
| XeSS performance | 35.9 | 1.26 |

Ending (`walkthrough.py ending --shots`, the 31 shots with content, `--shot-warmup 24`, film grain on), against native: median PSNR FSR quality 47.3 dB, DLSS quality 48.1 dB, DLAA 50.7 dB, DLSS performance 45.9 dB, XeSS quality 48.7 dB, XeSS native AA 49.5 dB, lowest 31 to 34 dB on the street shots. There the eye adaptation is still moving after 24 frames of the same state (the native mean of ending_07 falls from 59 to 43 levels between 24 and 200 frames) and the upscalers trail it by a few levels (ending_07: DLSS quality 3.7 darker, FSR 1.4); after 200 frames both are within 1.3 levels of native, except FSR under the flashlight on the wet asphalt (ending_06 and ending_09, 3.2 and 3.9 levels darker: its accumulation loses some of the sparkle; DLSS 0.5 darker and 1.2 brighter). A 120 frame sequence of the walking legs in the street (`--render-all`, skinned motion vectors, motion blur on) shows no trails or doubled edges on the moving shoes in either upscaler.

GPU frame time (timestamps around the whole frame, all post effects on) at the corridor pose, mean over 900 frames after 600 warm-up frames, average of two runs (the range in parentheses), and the upscaler with its resolve pass:

| mode | 1920x1080 ms | upscaler ms | 3840x2160 ms | upscaler ms | render at 3840x2160 |
| --- | --- | --- | --- | --- | --- |
| off (native with FXAA) | 3.10 (3.08-3.12) | | 10.34 (10.20-10.49) | | 3840x2160 |
| FSR native AA | 3.89 (3.83-3.94) | 0.56 | 13.36 (12.79-13.93) | 2.11 | 3840x2160 |
| FSR quality | 2.72 (2.60-2.85) | 0.43 | 7.67 (7.44-7.89) | 1.52 | 2560x1440 |
| FSR balanced | 2.69 (2.54-2.84) | 0.45 | 6.63 (6.58-6.69) | 1.46 | 2259x1271 |
| FSR performance | 2.22 (2.13-2.31) | 0.42 | 5.70 (5.62-5.78) | 1.30 | 1920x1080 |
| FSR ultra performance | 1.96 (1.84-2.08) | 0.36 | 4.60 (4.29-4.92) | 1.24 | 1280x720 |
| DLAA | 3.95 (3.85-4.05) | 0.71 | 13.54 (13.13-13.94) | 2.29 | 3840x2160 |
| DLSS quality | 2.95 (2.93-2.97) | 0.69 | 8.47 (8.22-8.73) | 2.28 | 2560x1440 |
| DLSS balanced | 2.75 (2.70-2.81) | 0.72 | 7.91 (7.72-8.09) | 2.26 | 2259x1271 |
| DLSS performance | 3.12 (2.88-3.35) | 1.25 | 9.17 (8.95-9.39) | 4.52 | 1920x1080 |
| DLSS ultra performance | 2.56 (2.52-2.59) | 1.00 | 6.84 (6.67-7.02) | 3.40 | 1280x720 |
| XeSS native AA | 4.24 (4.23-4.24) | 0.80 | 13.15 (12.88-13.42) | 2.73 | 3840x2160 |
| XeSS quality | 3.24 (3.16-3.31) | 0.78 | 8.19 (8.19-8.20) | 2.56 | 2560x1440 |
| XeSS balanced | 2.97 (2.86-3.07) | 0.83 | 7.55 (7.47-7.62) | 2.64 | 2259x1271 |
| XeSS performance | 2.81 (2.61-3.01) | 0.89 | 6.66 (6.64-6.68) | 2.62 | 1920x1080 |
| XeSS ultra performance | 2.58 (2.58-2.59) | 0.90 | 5.53 (5.47-5.59) | 2.56 | 1280x720 |

The XeSS rows at 3840x2160 come from a later build with frame generation, where the frame without upscaler measures 9.79 ms (10.34 above) and the FSR and DLSS modes 2 to 9 % less than above. Motion vectors and the reactive mask add 0.02 to 0.09 ms at 1920x1080 and 0.04 to 0.24 ms at 3840x2160. On this GPU the DLSS models M (performance) and L (ultra performance) cost about twice as much as K (the DLSS guide lists the same ratio for RTX 20 and 30; they are tuned for RTX 40 and later), so DLSS performance takes more GPU time than DLSS quality here.

Frame generation, in a window on a 260 Hz display, the corridor pan of the start route (`PT_FRAME_STATS=1`), rendered frames per second as the median frame interval (each rendered frame was followed by one generated frame, e.g. 3603 of 3603):

| 2560x1440, v-sync on | without | with frame generation | shown |
| --- | --- | --- | --- |
| DLAA (the user's setting) | 162 (6.2 ms) | 88 (11.3 ms) | 176 |
| DLSS quality | 211 (4.7 ms) | 119 (8.4 ms) | 239 |

With v-sync each rendered frame and its generated frame take whole refreshes: two (7.7 ms at 260 Hz) when the frame and the interpolation fit, three otherwise. With DLAA at 2560x1440 they do not fit, so frame generation shows 176 frames per second but renders 88 instead of 162; with DLSS quality most frames fit two refreshes. The DLSS quality row was measured while other programs loaded the GPU.

## DLSS Frame Generation on RTX 20 and 30

DLSS Frame Generation needs an RTX 40 or newer card. On an RTX 20 or 30 the row is greyed out, unless RTX30MFG-Unlock
(github.com/mcsoderh/RTX30MFG-Unlock) sits next to pt.exe: `RTX40MFGCore.dll`, `RTX40MFG.asi` and the Ultimate ASI Loader as
`version.dll` with its `global.ini` values in `version.ini`. The game then lets Streamline decide, and Streamline accepts the
card. Tested once on an RTX 3090: it generates frames. It is third-party and experimental; nothing of it ships with the game.
