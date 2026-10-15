# Android Vulkan 1.1 compatibility assessment

Status: **investigation only; Vulkan 1.1 support is not implemented or verified**.
Reviewed on 2026-10-14 against Android beta 2, source commit
`7677a0b29c7664246cffd5ba667b822732a91aec` on `codex/android-beta-fixes`.
[Android beta 2](https://github.com/ignusloki/pt-pc/releases/tag/v1.0.7-android-beta.2)
still requires Vulkan 1.3 and the renderer's additional features and limits.
This document concerns the custom C++/Vulkan/SDL3 Android port.

## Current conclusion

Supporting some Vulkan 1.1 devices may be possible through extensions providing
the newer functionality used by the renderer. We cannot establish whether the
reported Snapdragon 870 phone qualifies without its actual driver capabilities.
Lowering an API version constant or choosing Low graphics would not be sufficient.

If the driver lacks important features, especially descriptor indexing, support
would require a larger renderer fallback. There is no implementation commitment,
schedule or performance estimate yet. The recommended next step is to obtain a
device report and run a small compatibility probe before changing the game.

## Report and available evidence

A tester reported a Snapdragon 870 phone described as supporting Vulkan 1.1.
The error supplied through the user was:

> P.T. could not start
>
> The graphics device could not be set up (Vulkan).
>
> Make sure your graphics driver provides Vulkan 1.3 (Mesa 23 or newer, or the NVIDIA driver) and that P.T. runs inside your desktop session.
>
> Details are in /data/data/org.ignusloki.pt.beta/files/profile/pt.log; please attach that file when you report this.

The exact phone model, Android version/build, GPU driver version, APK version,
exported diagnostic log and Vulkan capability dump have not been provided.
We have not reproduced the failure on that phone.

The message comes from the generic renderer initialization failure in
[src/main.cpp](../src/main.cpp). Its Mesa/NVIDIA/desktop advice is inherited from
the desktop path and is unsuitable for Android. It does not identify the failed
Vulkan operation or a particular missing feature. If the selected physical
device really exposes only Vulkan 1.1, the current version check rejects it;
the message alone does not prove this was the operation that failed.

Our verified reference phone is the Galaxy S22 SM-S901B/DS, Exynos 2200 /
Xclipse 920, Android 16, with a Vulkan 1.3 driver. Its successful beta testing
does not establish compatibility or performance on the Snapdragon phone or
other Android devices. See the [installation guide](android-beta-installation.md)
and [beta 2 devlog](devlog-android-beta-2.md) for current test scope.

## What the current source assumes

Follow-up on 2026-10-15: a separate Huawei ADY-LX9 / Android 12 beta 2 export
confirms failure during GPU selection after successful import, but does not
contain the actual API version or GPU capabilities. The
[diagnostic build guide](android-vulkan-diagnostics.md) describes the new
capability collector and the report requested from Vulkan 1.1/1.2 testers.
This does not change the published game's compatibility requirements.

The following are confirmed source requirements at the reviewed commit:

| Area | Current behavior | Consequence for an older API path |
| --- | --- | --- |
| [Vulkan initialization](../src/engine/render/vk_context.cpp) | Requests instance API 1.3, skips physical devices below 1.3, and uses the Vulkan 1.2/1.3 feature structures. | Negotiate versions, query individual supported features/extensions and construct a compatible device feature chain. |
| [Rendering](../src/engine/render/renderer.cpp) and [barriers](../src/engine/render/vk.cpp) | Use core commands such as `vkCmdBeginRendering`, `vkCmdEndRendering`, `vkCmdPipelineBarrier2`, `vkQueueSubmit2` and timeline semaphore operations. | Enable and dispatch the corresponding extension commands, or provide fallbacks where unavailable. |
| [Texture descriptors](../src/engine/render/texture_manager.cpp) | Allocates an update-after-bind table of 8,192 textures plus 64 cube textures; shaders use non-uniform indexing. | Descriptor indexing feature bits and sufficient descriptor limits are necessary for this allocation model. |
| [Shader build](../CMakeLists.txt) | Compiles shaders with `--target-env=vulkan1.3`. | Build and validate shaders for the selected older API and extensions; existing binaries cannot be assumed compatible. |
| [Memory allocator](../src/engine/render/vk_context.cpp) | Passes Vulkan 1.3 to VMA. | Supply the negotiated API version and appropriate allocator extension support. |
| [APK manifest](../android/Launcher/AndroidManifest.xml) | Declares Vulkan 1.3 (`0x00403000`) as a required device feature. | Revisit device filtering only when a working lower-version path exists. |

The APK's Android 10 / API 29 minimum and `arm64-v8a` ABI are separate from its
Vulkan requirements. Installing the APK does not demonstrate GPU compatibility.

`Context::Init` currently requests these 17 feature flags unconditionally on
Android, in addition to the API version check:

- Vulkan 1.3: `dynamicRendering`, `synchronization2`,
  `shaderDemoteToHelperInvocation`.
- Vulkan 1.2 descriptor features: `descriptorIndexing`, `runtimeDescriptorArray`,
  `shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingPartiallyBound`,
  `descriptorBindingVariableDescriptorCount`,
  `descriptorBindingSampledImageUpdateAfterBind`,
  `descriptorBindingUpdateUnusedWhilePending`,
  `descriptorBindingStorageBufferUpdateAfterBind`.
- Other Vulkan 1.2 features: `timelineSemaphore`, `scalarBlockLayout`.
- Core features: `samplerAnisotropy`, `fillModeNonSolid`, `shaderInt16`,
  `shaderClipDistance`.

This is an inventory of current requests, not proof that every flag is essential
to Android gameplay. Audit their actual use before treating each absent flag as
a permanent hardware blocker. The aggregate `descriptorIndexing` flag in the
Vulkan 1.2 structure also needs to be translated into the relevant individual
extension feature checks on a Vulkan 1.1 path.

Android enables the full `textureCompressionBC` family flag only when supported.
It checks individual texture formats at upload; an unsupported format is logged
and uses the white fallback texture. The S22 supports the tested game's needed
BC formats despite not advertising the entire BC family. An older-device path
must validate actual texture and render/compute target formats; disabling the
family requirement is not a texture conversion implementation.

The existing collector checks four update-after-bind sampler/image limits
against **8,256** entries. This is preliminary screening, not a complete layout,
pool or pipeline validation. Lower surface resolution and the 1,024-pixel mip
limit reduce some rendering/memory costs but do not change this descriptor table
or the initialization feature requests. Disabling ray tracing does not remove
the raster renderer's requirements either.

## Extension route to investigate

These official Khronos extensions provide counterparts for the main newer
features. Their existence in the specification does not establish support in
the reported phone's driver.

| Functionality | Potential Vulkan 1.1 extension | Promoted to core |
| --- | --- | --- |
| Dynamic rendering | [VK_KHR_dynamic_rendering](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_dynamic_rendering.html) | 1.3 |
| Synchronization and submission API 2 | [VK_KHR_synchronization2](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_synchronization2.html) | 1.3 |
| Descriptor indexing and update-after-bind | [VK_EXT_descriptor_indexing](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_descriptor_indexing.html) | 1.2 |
| Timeline semaphores | [VK_KHR_timeline_semaphore](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_timeline_semaphore.html) | 1.2 |
| Scalar block layout | [VK_EXT_scalar_block_layout](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_scalar_block_layout.html) | 1.2 |
| Shader demotion to helper invocation | [VK_EXT_shader_demote_to_helper_invocation](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_shader_demote_to_helper_invocation.html) | 1.3 |

Check extension dependencies as well as their names and feature bits. On Vulkan
1.1, dynamic rendering also needs
[VK_KHR_depth_stencil_resolve](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_depth_stencil_resolve.html),
which in turn needs
[VK_KHR_create_renderpass2](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_create_renderpass2.html).
The exact required chain must be evaluated against the driver's API version.

Extension commands must be enabled and loaded using the appropriate entry
points. New headers and aliased type names alone do not make newer core commands
available on an older device. Shaders must also use a supported SPIR-V version
and capabilities, with their buffer layouts verified. The
[Khronos version guide](https://docs.vulkan.org/guide/latest/versions.html)
explains these distinctions, including instance versus device versions and
extension dispatch.

## Possible implementation scope

These are engineering possibilities, not findings about the reported driver:

| Device evidence | Possible work | Assessment |
| --- | --- | --- |
| Equivalent extensions, feature bits, limits and formats are available. | Version negotiation, extension feature chains/dispatch, shader targeting, allocator configuration, diagnostics, manifest and tooling changes. | A compatibility path may preserve most of the renderer. Still more than changing the version check, and requires device testing. |
| Descriptor indexing works but table limits are smaller. | Measure actual live resource counts and evaluate a smaller table with explicit capacity handling. | Could be a narrower adaptation if the game fits. Blindly reducing constants risks missing textures or exhausting resources. |
| Dynamic rendering or synchronization features are missing. | Traditional render passes/framebuffers, compatible barriers/submissions, and alternative synchronization where needed. | Wider renderer work with correctness and maintenance costs. |
| Non-uniform descriptor indexing/update-after-bind or required layouts/formats are missing. | Different material/resource binding, batching and shader variants; layout or texture conversion paths as applicable. | Potentially substantial rework. Needs a separate design based on the actual missing capabilities. |

Audit unused feature requests first. Keep the working Vulkan 1.3 path and desktop
builds compatible; isolate Android behavior or select paths from validated
capabilities. There is currently no basis for promising a frame rate, RAM usage,
thermal behavior or support for all Snapdragon 870 phones.

## Evidence needed before implementation

1. Obtain the exact phone model, Android/OS build, APK version and GPU driver
   identification. Record the loader's instance version and the physical
   device's `apiVersion` separately.
2. Reproduce the startup failure and use the launcher's **Save diagnostic log**
   action after returning to the launcher. The private `pt.log` path in the
   generic error is not a convenient export instruction for public builds.
3. Capture Vulkan extensions, individual feature bits, descriptor and other
   relevant limits, queue/presentation support and required format capabilities.
   Missing fields in a partial report mean unknown, not unsupported.
4. Run an asset-free probe that can start on Vulkan 1.1, request only supported
   feature structures, and test device creation, the chosen descriptor layout,
   image allocations and representative shaders before a full game build.

For a USB-connected reporter device, maintainers can use the existing read-only
[device collector](../tools/android/collect_device.py) from the Android worktree:

```sh
python3 tools/android/collect_device.py \
  --adb /path/to/sdk/platform-tools/adb \
  --serial DEVICE_SERIAL \
  --expected-model EXACT_MODEL \
  --out build/android-validation/vulkan11-device-NEW_CAPTURE
```

Replace the placeholders and choose a new output directory for each capture;
the model defaults to the S22 unless overridden. The collector saves Android's
`cmd gpu vkjson` output when available, but its analyzer assesses the **current
Vulkan 1.3 requirements**. A failed version check does not assess the feasibility
of a future extension path. It does not test all extensions or formats needed
for such a path.

The existing [native GPU probe](../tools/android/gpu_probe/main.cpp) also requests
Vulkan 1.3 and uses the 1.2/1.3 feature structures. It must be adapted for the
proposed lower API before using it to assess Vulkan 1.1 compatibility. Neither
current tool is evidence that the reported phone can run a future fallback.

## Validation before claiming support

A successful capability query or compilation would only justify further testing.
Verify the actual signed Release APK on the target phone: startup, first-boot
settings, original texture rendering, gameplay, menus/subtitles, movement/look
controls, settings persistence, aspect-preserving resolution and frame cap,
microphone/Whisper recognition, and repeated background/foreground returns.
Measure memory, sustained performance and thermal behavior, and repeat relevant
checks on the verified S22 and desktop builds affected by shared renderer edits.

Keep the beta's existing signing identity and both apps' profiles. Install test
updates without uninstalling or clearing storage. Preserve
`AndroidEnableMarshalMethods=false` and `AndroidIgnoreAllJniPreload=true` unless
a replacement is verified. Game archives, private signing material and raw
device/profile data do not belong in this assessment or a compatibility APK.

Until device evidence and these tests establish a working path, the published
Android requirement remains Vulkan 1.3 with the current additional capabilities.
