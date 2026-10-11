# Build and test on the Intel Mac

The Apple silicon upstream-update baseline was accepted on 11 October 2026. This native Intel test uses `codex/upstream-macos-integration`, whose complete game and shader source matches that accepted build (`b687754`). The branch adds separate CPU presets, matching voice modules and unified game/Setup packaging. The Mac minimum is **macOS 14**. Native Intel graphics, live microphone input and performance remain to be verified on the 2019 MacBook Pro.

The branch is local on the build Mac. Use the source bundle in `intel-native-handoff-20261011` to transfer its committed state; cloning the fork's remote Mac branch will not include these Intel packaging changes.

## 1. Prepare the Intel Mac

Record the macOS version, Mac model identifier, RAM and GPU. Connect power. Install current Xcode command line tools and Homebrew if absent, then:

```sh
xcode-select --install
brew install python@3.13 cmake ninja shaderc vulkan-headers vulkan-loader
```

If the command line tools are already installed, their install command may report that. These are development prerequisites; the resulting installer contains its own runtime for players. Homebrew provides [Python 3.13](https://formulae.brew.sh/formula/python@3.13), [shaderc/glslc](https://formulae.brew.sh/formula/shaderc) and [the Vulkan loader](https://formulae.brew.sh/formula/vulkan-loader).

Install the **.NET 10 SDK for macOS x64** using [Microsoft's macOS installation instructions](https://learn.microsoft.com/en-us/dotnet/core/install/macos). Choose the SDK; building the extractor requires it. Open a new terminal after installation.

```sh
uname -m
python3.13 --version
dotnet --info
```

Expect `x86_64`, Python 3.13 and an x64 .NET SDK version 10 or newer.

## 2. Copy and open the source bundle

Copy the complete handoff folder, or its ZIP, to the Intel Mac. Extract the ZIP if used and open Terminal in that folder. Verify the transfer, then create a new checkout:

```sh
shasum -a 256 -c SHA256.txt
git clone --branch codex/upstream-macos-integration pt-pc-intel-source.bundle pt-pc
cd pt-pc
git log -1 --oneline
```

The latest commit must match the handoff's `HANDOFF-INFO.json`. The bundle contains committed source and history, with no local builds or original game package. The clone's origin is this local bundle. Future updates can be transferred with another bundle; do not expect it to fetch new commits from GitHub.

## 3. Build the native Intel installer

From the new checkout:

```sh
set -o pipefail
python3.13 -u tools/macos/build.py --arch x64 --version 1.0.4 --build build/intel-native --out dist/macos/intel-native --jobs 4 2>&1 | tee intel-build.log
```

This compiles the release game, Intel voice CPU modules, self-contained x64 extractor and Setup; runs the selected regressions, extractor fixtures, packaged voice check and Setup self-test; then signs and packages the result. Keep the tests enabled for this native build. Initial setup downloads dependencies and voice models. Version 1.0.4 is an explicit local validation override; it does not create a tag or release or alter upstream's default version.

Results are under `dist/macos/intel-native/`:

- `P.T. PC Port Setup.app`
- `pt-port-1.0.4-setup-macos-x64.zip`
- `P.T. PC Port.app` and its portable ZIP

The output directory must be new. For another attempt after packaging has started, choose a fresh output directory such as `--out dist/macos/intel-native-retry`; the build directory can be reused. If a stage fails, retain `intel-build.log` and the complete error.

## 4. Install and test

Open Setup, select the complete P.T. `.pkg` or decrypted folder, and choose a writable test destination. Original game assets are selected on the Intel Mac and are excluded from the source bundle and built installer. The detailed [Intel checklist](intel-mac-test-checklist.md) covers first boot, microphone, graphics, controls and persistence.

Start Windowed at **1280 × 720**, **Low**, **Upscaling Off**, **Reflections Off**, **V-sync On**, **30 FPS limit**, **Microphone On**, **Fast Walk Off**. After the baseline works, try Original, Reflections On and MetalFX / Quality if the GPU supports it. Check sustained play for at least ten minutes.

For a fresh game profile, quit and rename `~/Library/Application Support/pt-port/pt/` to `pt-backup` if it already exists. Relaunch creates a fresh profile. Restore the original folder only while the game is closed, moving the test profile aside first. This resets game state/settings; the macOS microphone decision is separate.

For targeted scene checks after first boot, you can transfer **both** `PT_Save_Data0` and `PT_Save_Data1` from the accepted Apple silicon profile to the same profile folder on the Intel Mac while the game is closed. Back up any existing Intel saves first. Keep the Intel `pt.ini` so its conservative graphics settings remain in use. The completed saves provide Loop Browser unlocks without requiring another full replay.

Return the machine details, settings used, failed checklist item, `intel-build.log` and the game's `~/Library/Application Support/pt-port/pt/pt.log`. A successful compile or a Rosetta run on Apple silicon does not confirm the Intel GPU. The release and eventual upstream PR still depend on native Intel results and the remaining integration checks.
