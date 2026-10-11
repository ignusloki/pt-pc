# P.T. for Mac 1.0.5 — Apple silicon and Intel

The first stable Mac release provides separate graphical installers for **Apple silicon** and **Intel** Macs running **macOS 14 or newer**. Players select their own complete P.T. fake PKG or decrypted game folder and the installation location. No Terminal, Homebrew, Vulkan SDK or .NET installation is required.

## Downloads

| Your Mac | Graphical installer | Optional portable app |
| --- | --- | --- |
| Apple silicon (M1 or newer) | `P.T.PC.Port.Setup-macOS-arm64.zip` | `P.T.PC.Port-macOS-arm64.zip` |
| Intel | `P.T.PC.Port.Setup-macOS-x64.zip` | `P.T.PC.Port-macOS-x64.zip` |

## Changes since preview 5

- Adds a native Intel graphical installer and matching game, extractor and voice modules.
- Incorporates the latest tested upstream rendering, audio, controller and installer updates through `edb5a85`.
- Reconciles Mac packaging so both CPUs use the same app layout, dependency audits and installer repair/rollback.
- Uses **P.T. PC Port.app** and **P.T. PC Port Setup.app**, with the supplied forest/P.T. icon. Existing `P.T..app` installations are recognized during updates.
- Retains startup microphone authorization, the persistent Sound → Microphone input toggle, corrected fullscreen mouse alignment, optional MetalFX upscaling and default-off Fast Walk.
- Uses this fork's release endpoint in the published Mac builds so update notices follow the Mac releases here.

MetalFX and Fast Walk remain Off by default. PR #3's MetalFX frame generation is not included.

## Install or update

1. Download and unzip the **Setup** package for your Mac.
2. Open **P.T. PC Port Setup.app**.
3. Select your own complete P.T. fake PKG or decrypted game folder.
4. Choose a writable parent folder. To update an existing installation, choose the parent of its **P.T. PC Port** folder.
5. Open **P.T. PC Port.app** inside that folder. Keep it beside **CUSA01127**.

Updates preserve player saves, settings, mods and usable game archives outside the replaced app bundle. The profile remains at `~/Library/Application Support/pt-port/pt/`. A different installation folder uses the same account's profile; it does not reset progress.

The optional portable ZIP contains the game runtime. Place your own extracted **CUSA01127** folder beside its app, or select the folder when prompted. Original game archives are excluded from every download.

## Intel performance and compatibility

The user confirmed installation and gameplay on a **2019 MacBook Pro**, with slower performance. Start at **Windowed 1280 × 720, Low, 30 FPS, Reflections Off and Enhanced textures Off**. Keep Upscaling Off for an initial comparison; try MetalFX only when available on your GPU. No measured frame rate or exact GPU was supplied, so this release does not promise a particular Intel frame rate.

The renderer requires GPU features that some Intel integrated graphics and older AMD GPUs lack. A model year alone does not establish compatibility; lowering settings cannot provide a missing required GPU feature. Ray tracing, VR and Windows-only upscalers remain unavailable on Mac.

## Microphone and macOS approval

When microphone input is On and macOS has no decision recorded, the game requests permission at startup. Granting permission does not start recording. **PC Settings (F10) → Sound → Microphone input** controls capture and its test, and persists across launches. An existing macOS decision skips the prompt; denied access can be changed in **System Settings → Privacy & Security → Microphone**.

The apps are ad-hoc signed and **not Apple-notarized**. If macOS blocks a trusted downloaded copy, attempt to open it and use **System Settings → Privacy & Security → Open Anyway** if offered. See [Apple's instructions](https://support.apple.com/en-us/102445).

## Validation and credits

Apple silicon regression and native Intel installation/gameplay were accepted by the user. Release packages are checked for target architecture, macOS minimum, signatures, included dependencies, voice loading, installer unpacking and rollback. **BUILD-INFO.json** records the exact source and checks; **SHA256SUMS.txt** records the download hashes. Native Windows/Linux CI remains separate work for a future upstream PR.

Thanks to LoreanXavier and upstream contributors, ahm3texe for the upstream Mac work, ginnfx/brajan zalta for MetalFX and performance contributions, and rfpm for the icon.
