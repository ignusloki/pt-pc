# Intel Mac test checklist

Target: the user's **2019 MacBook Pro**. This is the first native Intel gameplay test for the refreshed integration preview. It requires **macOS 14 or newer** and a GPU that supplies the renderer's required features.

Copy `pt-port-1.0.2-setup-macos-x64.zip` from `/Users/suporte/PT-Mac/dist/macos/upstream-integration-20261010-x64/` to the Intel Mac. It contains the installer; the original game assets must be selected separately on that Mac. Use the x64 package for Intel.

For a clean boot without touching existing progress, use a separate macOS user account. Two app folders under the same account share the profile at `~/Library/Application Support/pt-port/pt/`.

## Record the machine

- [ ] Record the MacBook Pro screen size/model identifier, macOS version, RAM and graphics adapter from About This Mac/System Information.
- [ ] Connect power for the initial performance check.

## Install and first launch

- [ ] Unzip and open `P.T. PC Port Setup.app`. If macOS blocks this ad-hoc-signed preview, allow the trusted copy through System Settings → Privacy & Security → Open Anyway.
- [ ] Select the complete P.T. fake PKG, then choose a writable test location, for example a new folder under your home directory.
- [ ] Confirm installation finishes and the game/builder show the selected forest/P.T. icon.
- [ ] Open the installed `P.T. PC Port.app`. It should locate the adjacent `CUSA01127` folder automatically.
- [ ] With no previous microphone decision, confirm the permission request appears during startup, before gameplay. Choose Allow yourself. A previously recorded decision correctly skips this dialog.
- [ ] Confirm the initial brightness/subtitles/camera screen is visible and Esc or the controller confirmation starts the game.

If the game stops with a missing required Vulkan-feature message, keep `pt.log`. Lower graphics settings cannot supply a missing required hardware feature.

## Initial settings

Open PC Settings with F10 or through the pause menu. Start with these values:

| Setting | Initial value |
| --- | --- |
| Display mode and resolution | Windowed, 1280 × 720 |
| Graphics preset | Low |
| Upscaler | Off |
| Reflections | Off |
| Enhanced textures | Off |
| HDR, frame generation and VR | Off |
| V-sync | On |
| FPS limit | 30 |
| Microphone input | On, system-default or built-in microphone |
| Fast Walk | Off |

## Graphics, controls and sound

- [ ] Play through the first room and hallway; check textures, lighting, audio and camera movement.
- [ ] Switch Windowed → Borderless → Fullscreen → Windowed. Mouse hover and clicks must stay aligned with each setting row. Repeat after changing resolution.
- [ ] Check controller movement, interactions, pause/confirm and available vibration.
- [ ] Play for at least ten minutes and note stutter, visual corruption, crashes and approximate frame rate.
- [ ] Turn Reflections On and inspect the bathroom/mirror and later maze.
- [ ] If MetalFX is available in the menu, enable it at Quality and repeat those scenes. If unavailable, its disabled choice should explain why and native rendering should continue.
- [ ] After the Low test passes, try Original at 1280 × 720. Increase the FPS limit to 60 only to compare performance; there is no Intel performance guarantee yet.
- [ ] Enable Fast Walk in Extras. Hold Shift or the controller's bottom face button: walking gets faster while interaction still works. Release to return to normal speed. Turn the option Off and confirm the original speed returns.

## Microphone and persistence

- [ ] In PC Settings → Sound, open the microphone test and confirm the input meter responds to your voice. Say “Jack” and check recognition.
- [ ] Set Microphone input Off. Capture should stop and the microphone test should be disabled. Quit/reopen and verify Off persists.
- [ ] Re-enable input and verify the test works. If macOS access was denied, enable it in System Settings → Privacy & Security → Microphone, then reopen the game.
- [ ] Complete the final voice puzzle with the real microphone if possible. Scripted/silence checks performed on the build Mac do not verify live Intel speech.
- [ ] Quit/reopen after a completed loop and check saved progress and settings.
- [ ] Rerun Setup against this test installation and repair it. Progress, settings and usable game archives must remain available.

Return the machine details, settings used, failed checklist item and `pt.log` from `~/Library/Application Support/pt-port/pt/`. For a graphics issue, include a screenshot and whether it occurs with both Upscaler Off and MetalFX. Intel confirmation and native Windows/Linux CI are still required before a public release/upstream PR.
