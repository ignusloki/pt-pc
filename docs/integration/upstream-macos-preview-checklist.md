# Mac integration preview checklist

Use the Apple silicon installer in `/Users/suporte/PT-Mac/dist/macos/upstream-integration-preview/P.T. PC Port Setup.app`. The separate Intel package is under `upstream-integration-intel-preview`; its compilation/signing are checked, but Intel gameplay needs an Intel Mac with a compatible GPU.

This preview follows upstream's game and newer voice runtime. MetalFX and optional Fast Walk are later integration work; compare those features using the existing tested fork release.

## Installation and icons

- [ ] Finder, the Dock and installer dialogs show the supplied forest/P.T. icon.
- [ ] Select your complete P.T. fake PKG, choose a new writable test location, and confirm that installation finishes.
- [ ] Open `P.T. PC Port.app` inside the installed `P.T. PC Port` folder. It finds the sibling `CUSA01127` archives.
- [ ] Optionally install from a decrypted folder at another location, including spaces/accents in the path.
- [ ] Run Setup again on the same test installation. Confirm saves, settings and mods survive the repair and the game launches afterward.

## Microphone permission at startup

- [ ] On a launch with no existing macOS microphone decision for this app, the OS request appears during startup, before the voice puzzle or microphone test screen.
- [ ] Choose **Allow** yourself. Confirm the first settings screen/game continues normally. Permission alone should not start recording.
- [ ] Quit and reopen. An existing grant is respected without another request.
- [ ] In the game's voice/microphone settings, choose the Mac's actual microphone, test live input and confirm that speech reaches the existing recognition system. Use the same mic/threshold settings as the tested release for comparison.
- [ ] If testing denial, choose **Don't Allow** on a fresh consent test. Confirm game startup still works, with the voice puzzle requiring access to be enabled later in System Settings > Privacy & Security > Microphone.
- [ ] After enabling access, restart the app and retest live input.

macOS remembers its decision. A reused app/profile may therefore skip the dialog correctly. A separate macOS test account is one way to test a genuinely fresh decision. No authorization reset or permission grant has been performed automatically.

## First screen, graphics and controls

Use 1280×720, the **Original** graphics preset, ray tracing/VR off, and enhanced textures off for the first comparison. Keep microphone input enabled for its test. Then repeat with your usual graphics settings.

- [ ] On a fresh game settings profile, the brightness/subtitles/camera settings screen is visible immediately.
- [ ] Confirm the settings with Esc or the controller button and reach the first room.
- [ ] Open the pause menu and PC settings; check that text and buttons render correctly.
- [ ] Switch away from the app and back during startup, including after the microphone dialog. Confirm there is no unwanted initial pause or black settings screen.
- [ ] Check movement, camera, controller buttons, sound and normal progression into the hallway.
- [ ] Repeat one launch using your existing settings/profile and confirm that existing progress is retained.

Report the Mac model, macOS version, whether microphone permission was already recorded, the chosen Allow/Deny response, any screenshot, and `pt.log` from `~/Library/Application Support/pt-port/pt/` if a check fails.
