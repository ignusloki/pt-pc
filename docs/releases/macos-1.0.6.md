# P.T. for Mac 1.0.6 — Apple silicon and Intel

This release includes the rendering updates from [PR #3](https://github.com/ignusloki/pt-pc/pull/3) and the native HDR fix from [PR #4](https://github.com/ignusloki/pt-pc/pull/4). Separate graphical installers are available for **Apple silicon** and **Intel** Macs running **macOS 14 or newer**. Players select their own complete P.T. fake PKG or decrypted game folder and the installation location. No Terminal, Homebrew, Vulkan SDK or .NET installation is required.

## Downloads

| Your Mac | Graphical installer | Optional portable app |
| --- | --- | --- |
| Apple silicon (M1 or newer) | `P.T.PC.Port.Setup-macOS-arm64.zip` | `P.T.PC.Port-macOS-arm64.zip` |
| Intel | `P.T.PC.Port.Setup-macOS-x64.zip` | `P.T.PC.Port-macOS-x64.zip` |

## Changes since 1.0.5

- Updates frame pacing and rendering work, including compute bloom, motion-blur buffers and GPU timing. The ordinary rendering path passed the comparison against 1.0.5.
- Adds optional **MetalFX frame generation** to the Apple silicon package. It requires **macOS 26 or newer** and a GPU that Apple's runtime reports as supported. It stays **Off by default** and uses V-sync while active. The Intel package is built with the macOS 15.5 SDK and does not include this feature; ordinary MetalFX upscaling remains available when supported.
- Fixes **Native HDR** being silently blocked by the normal upscaler device hooks. Supported Mac displays can now use the native extended-linear HDR output. Changing Native HDR still requires a restart. HDR stays Off by default, and frame generation stays disabled while native HDR is active.
- Retains startup microphone authorization, the persistent Sound → Microphone input toggle, fullscreen mouse alignment, the forest/P.T. icon, default-off Fast Walk and installer update/rollback support.

Frame generation has its own GPU cost and may reduce performance at high resolutions. Try it at a moderate resolution and compare with it Off; this release makes no frame-rate promise. Upscaling and Fast Walk also remain Off by default.

## Install or update

1. Download and unzip the **Setup** package for your Mac.
2. Open **P.T. PC Port Setup.app**.
3. Select your own complete P.T. fake PKG or decrypted game folder.
4. Choose a writable parent folder. To update an existing installation, choose the parent of its **P.T. PC Port** folder.
5. Open **P.T. PC Port.app** inside that folder. Keep it beside **CUSA01127**.

Updates preserve player saves, settings, mods and usable game archives outside the replaced app bundle. The profile remains at `~/Library/Application Support/pt-port/pt/`. A different installation folder uses the same account's profile; it does not reset progress. Existing `P.T..app` installations are recognized during updates.

The optional portable ZIP contains the game runtime. Place your own extracted **CUSA01127** folder beside its app, or select the folder when prompted. Original game archives are excluded from every download. Published Mac builds check this fork's releases for updates.

## Intel performance and compatibility

The previous 1.0.5 release was accepted on a **2019 MacBook Pro**, with slower performance. Start at **Windowed 1280 × 720, Low, 30 FPS, Reflections Off and Enhanced textures Off**. Keep Upscaling Off for an initial comparison; try MetalFX only when available on your GPU.

The new rendering changes compile for Intel and pass CPU and packaged-runtime checks through Rosetta on Apple silicon. They still need a native Intel gameplay check. The renderer requires GPU features that some Intel integrated graphics and older AMD GPUs lack; model year alone does not establish compatibility. Ray tracing, VR and Windows-only upscalers remain unavailable on Mac.

## Microphone and macOS approval

When microphone input is On and macOS has no decision recorded, the game requests permission at startup. Granting permission does not start recording. **PC Settings (F10) → Sound → Microphone input** controls capture and its test, and persists across launches. An existing macOS decision skips the prompt; denied access can be changed in **System Settings → Privacy & Security → Microphone**.

The apps are ad-hoc signed and **not Apple-notarized**. If macOS blocks a trusted downloaded copy, attempt to open it and use **System Settings → Privacy & Security → Open Anyway** if offered. See [Apple's instructions](https://support.apple.com/en-us/102445).

## Validation and credits

The merge candidates passed Apple silicon rendering comparisons, gameplay walkthrough checks, fullscreen/menu checks, native Metal tests, frame-generation checks and HDR startup checks. Both architectures compile; CPU suites run natively on Apple silicon and through Rosetta for Intel. Release packages are checked for target architecture, macOS minimum, signatures, included dependencies, voice loading and installer unpacking/rollback.

**BUILD-INFO.json** records the source and validation scope; **SHA256SUMS.txt** records the download hashes. Native Intel gameplay for these updates, longer frame-generation sessions, external HDR10 displays, Windows/Linux native CI and Windows Streamline hardware remain outside the completed checks.

Thanks to LoreanXavier and upstream contributors, ahm3texe for the upstream Mac work, ginnfx/brajan zalta for the MetalFX, performance and HDR contributions, and rfpm for the icon.
