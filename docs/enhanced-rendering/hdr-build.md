# Rebuilding and publishing the native HDR fork

## Source and prerequisites

Clone `https://github.com/hartapfel/pt-pc-hdr.git` and check out `v1.0.8.2-hdr` (or branch `hdr` for current source). GitHub's source ZIP/tar.gz and the explicit release source ZIP contain the same tracked tree. No RenoDX checkout is required. The baseline is official `v1.0.8`, commit `3b1392abe5215773d4c353f5bc1ecb85233be68a`.

Use Windows x64, Visual Studio 2022 Build Tools with the C++ desktop workload/Windows SDK, LLVM/Clang, CMake 3.28 or newer, Ninja, Git, Python 3 and .NET SDK 10. Install a Vulkan SDK providing Vulkan headers/loader, `glslc` and `spirv-opt`; set `VULKAN_SDK` to its directory. The upstream batch wrapper expects VS 2022 BuildTools and LLVM/CMake in their usual Program Files locations. Adjust it locally for a different VS installation. `PT_BUILD_JOBS` controls parallelism.

CMake fetches the dependency versions and hashes declared in `cmake/Dependencies.cmake`, `cmake/D3D12Backend.cmake` and the upscaler CMake files; keep those files unchanged when reproducing this release. Internet access is needed for dependencies, upscaler SDKs and the speech model. The HDR release preset uses Clang x64 **Release**, Game+ enabled, required upscalers enabled, and **OpenXR disabled**. This is the tested desktop configuration.

Build the game with the original entry point:

```bat
tools\build_pt.bat release
```

For a release-locked executable, use `tools\build_pt.bat release release pt_release`. Outputs are under `build/release`. Build/test source includes both Vulkan SPIR-V and D3D12 DXIL shader pipelines.

## Installer and portable package

Fetch the pinned extraction-library source outside the checkout (or in an ignored directory):

```bat
git clone https://github.com/maxton/LibOrbisPkg.git ..\LibOrbisPkg
git -C ..\LibOrbisPkg checkout 643477263b2644e0803e0f58b8726ea4e3f3b7d4
```

The original `tools/ci/release.py` copies this source to the build directory, applies the upstream reader fixes, retargets it to .NET 10, publishes the self-contained extraction helper, and bundles its exact corresponding source and LGPL license. Nothing is downloaded from a previous HDR build.

Official fork releases use a separate Ed25519 update key. It is **not** an Authenticode certificate. The public key is tracked in `src/engine/platform/release_key.h`; the private seed must never be committed or included in a source/package archive. Maintainers use their existing key. Independent builders can build unsigned game/portable packages with `tools/package.py`, or generate their own key **before building** their own installer:

```bat
python tools\ci\release_signing.py keygen --keys ..\my-private-hdr-release-keys
python tools\ci\release.py --version 1.0.8.2-hdr --dest dist --name 1.0.8.2-hdr --out release-report --liborbis ..\LibOrbisPkg --signing-keys ..\my-private-hdr-release-keys
```

Generating a key changes the public-key source header; keep that change in your own build/fork. To reproduce the public source without changing its key, build the game and payload using `tools/package.py` and `tools/prepare_native_installer.py`, build `pt_setup`, then run `tools/ci/stamp_integrity.py`. Only the maintainer can produce an update trailer accepted by official fork installations. Private signing material is not needed to rebuild the engine, shaders or extractor.

Use a new `--name` for each packaging attempt; the packager refuses an existing portable output. Without `--game`, and with no `game/CUSA01127` directory, the package contains no scene previews. Never put original game archives into a public source/release artifact. Publish one Windows installer, **P.T.PC.Port.1.0.8-HDR.Setup.exe**, with the GitHub asset label **Windows installer - version 1.0.8.2-hdr**. The historical filename is required by existing updaters; its embedded version is current. The portable asset is **P.T.PC.Port.1.0.8.2-HDR-portable-windows.zip**. The tooling creates a versioned installer copy for local use only; do not upload that duplicate.

The fork polls its own GitHub Releases endpoint and trusts its own signing key. Numeric tags optionally ending in `-hdr` are accepted; drafts/prereleases are not offered as automatic updates. Use `<upstream version>.<fork revision>-hdr`, starting at `1.0.8.1-hdr`; initial `1.0.8-hdr` is revision zero. Increment the numeric fourth component for further fork releases. Set the same version in CMake, the release command, tag and notes. **Do not change the legacy canonical installer name** in `update_check.cpp`, `release_signing.py` or release tooling: old clients require it inside the signed manifest too. The versioned local copy is generated automatically after signing and excluded from upload candidates; use the current-version GitHub label on the canonical asset. Keep the existing private signing key. Publish only after all assets are uploaded and verified, as a non-prerelease marked Latest.

## Validation

Relevant targets include `pt_platform_test`, `pt_settings_roundtrip_test`, `pt_graphics_preset_test`, `pt_exposure_test`, `pt_hdr_output_test`, `pt_hdr_pipeline_test`, `pt_d3d12_backend_test`, `pt_composite_output_test`, `pt_d3d12_hdr_framegen_test` and `pt_pc_settings_refresh_test`. Build them with CMake and run from the build directory. GPU tests need suitable local Vulkan/D3D12 hardware; see the test sources for command-line options. For the installer, use `--verify-integrity <new-output.txt>` and `--self-test <new-output.txt>`, then verify the signed trailer with `tools/ci/release_signing.py verify <setup>`.

Internal HDR9 validation covered production GPU shaders on Vulkan/D3D12, HDR output combinations, optics/motion tests, real-scene readbacks, and synthetic fresh/upgrade installation. Full playthrough, physical-display measurement and live generated-frame gameplay have not been certified. Technical HDR1–HDR9 documents are a chronological audit trail; later revisions supersede earlier implementation details.

## Published build toolchain

The initial release uses LLVM 22.1.3, CMake 4.3.1, .NET SDK 10.0.113, and shaderc v2026.1. Its Vulkan tools were built from source rather than installed as an SDK:

- shaderc: `301b4ede53d59b68bf55f95bb26412d9233c8187` (with its pinned `utils/git-sync-deps` dependencies).
- Vulkan-Headers: `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77`.
- Vulkan-Loader: `a9e72c66d5cb79911eb9a9063bf4016dd0a3a123`.

Configure shaderc with Clang/Ninja Release, `SHADERC_SKIP_TESTS=ON`, `SHADERC_SKIP_EXAMPLES=ON`, `SHADERC_SKIP_COPYRIGHT_CHECK=ON`, `SPIRV_SKIP_TESTS=ON`, `SPIRV_SKIP_EXECUTABLES=OFF`, `ENABLE_GLSLANG_BINARIES=OFF`, `SHADERC_ENABLE_WGSL_OUTPUT=OFF`; build `glslc` and `spirv-opt`. Configure the loader against the pinned headers. Pass your resulting paths via `PT_CMAKE_ARGS` using `Vulkan_INCLUDE_DIR`, `Vulkan_LIBRARY`, `Vulkan_GLSLC_EXECUTABLE` and `PT_SPIRV_OPT`, or use a compatible installed Vulkan SDK. No machine-specific SDK paths are committed.

This reproduces the source/build procedure, not a guarantee of byte-identical PE/ZIP output across timestamps, dependency servers, toolchain versions and signing keys. Release SHA-256 checksums identify the distributed artifacts; build provenance records the source commit and toolchain.


The published upstream release script referenced two omitted helpers. This fork includes `tools/ci/portable_list.py` and `tools/ci/payload_index.py` to reproduce its portable manifest and signed range-update protocol. Run `python tests/test_release_packaging.py`; add `--setup <installer>` to verify every compressed payload range and SHA-256 against a real installer. The extractor restore uses the repository-scoped `installer/NuGet.Config`, also included in its corresponding-source archive.
