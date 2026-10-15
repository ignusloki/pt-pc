# Controller feedback

The PC settings page includes a vibration mode. **Original** is the default and keeps the game's standard motor vibration.
**Enhanced** also enables trigger vibration on compatible controllers and, on supported wired USB DualSense controllers
on Windows, adds vibration derived from Lisa's filtered crying audio. Enhanced effects depend on the controller and its
driver; unsupported effects stay unavailable. Turning the main Vibration setting off disables both original and enhanced
vibration.

Controller-speaker playback is a separate optional setting. On supported wired Sony controllers on Windows, it can play
Lisa's authored crying audio through the controller speaker. Its volume has its own control. The normal game mix,
including headphone output, continues unchanged, and speaker volume does not change vibration strength. The speaker
setting and its volume are independent of the vibration mode. The original game plays nothing through the controller speaker
(`gameplay.md` 14.5), so this stays a PC addition and off by default.

Finding the speaker: on Windows the pad's own playback endpoint is looked up through the pad's PnP container; when that finds
none (DS4Windows puts a virtual DualShock 4 in front of the real pad) or on Linux and macOS, the one playback device named as the
pad's USB audio is taken: a DualShock 4's is "Wireless Controller" (Windows: "Headset Earphone (Wireless Controller)", a stereo
pair at 32 kHz), a DualSense's "DualSense Wireless Controller" (4 channels). Two of the same kind by name, or none, leave the
speaker off; the main mix is the same either way. The route is tried again every 2 s and follows the pad last used, so plugging
the pad in or out while playing works. `PT_CONTROLLER_SPEAKER_DEVICE=<part of a playback device name>` makes another device stand
in for the speaker (tests).

These controller features have passed software tests for supported-capability handling, event selection and audio
routing. DualShock 4 speaker playback on hardware has not been verified (whether a DS4 on PC plays its USB audio on the built-in
speaker at a usable volume without a PS4 setting its speaker volume is unknown); trigger vibration and DualSense actuator output
have not been verified on hardware either. Bluetooth and unsupported devices are not claimed to work.

PC settings > Accessibility has two more controller features, both off by default. **Sound captions** give the controller a short
rumble pulse (the small motor, a quarter of a second) with a caption for sounds that have no vibration of their own, when
Vibration is on; they do not change the rumble modes above. **Gyro** (Camera or Flashlight) reads the motion sensor of a
DualSense, DualShock 4 or Switch Pro controller through SDL; L3 recentres the flashlight. Both are described in
`docs/gameplay.md` (14.8 and 14.9). The gyro has passed software tests with virtual controllers only; real motion sensors have
not been tested.

## Start-up

The controllers and the audio devices start on threads of their own in a window on Windows (`src/engine/platform/device_start.h`);
the window, the graphics device and the boot screens never wait for them. A 1.0.2 player's start sat 98 s inside `SDL_Init` before
any window (a wired DualSense Edge, a TV on HDMI among the audio devices) and a 1.0.3 player's 5.4 s in the gamepad subsystem
alone with no pad connected: SDL enumerates every device there (DirectInput, which SDL itself notes can take a minute even with
no controllers; HIDAPI's open of each HID device; the WASAPI endpoints).

- Controllers: their thread stays to receive HIDAPI's device change messages (SDL makes that window on the thread that starts the
  subsystem), so plugging a pad in or out works as before. Until the start is over SDL's own pad update in the event pump is off
  (`SDL_HINT_AUTO_UPDATE_JOYSTICKS`, it would wait on the joystick lock the start holds) and Dear ImGui is not asked for pads;
  then the pads connected are opened in that frame. DirectInput's enumeration is timed first on its own: not over in 2 s, SDL
  starts without DirectInput that session (XInput and HIDAPI pads as usual; SDL would otherwise repeat the slow enumeration on
  the main thread at every device change), and the log says so.
- Audio: the sound waits at most 0.25 s for the audio start when it is set up; still starting, it begins without a device (as on a
  machine with none) and opens the device in the frame the start ends.
- Headless runs, virtual pads (`--virtual-pads`) and Linux and macOS start both on the main thread, timed the same way.

pt.log has a `boot:` line for each phase: the main thread's phases (data folder, crash reporting, game folder with where it was
found, game archives, video, settings, window, mods and graphics settings, graphics device, game data, sound, first frame with
what it shows) each timed from the end of the one before and the first from the process's creation, so they add up to the time
since the start; the background ones (DirectInput enumeration, controllers, audio devices) give their own length. Slow 1.0.3
starts with 5 to 22 s before `game files found at` were first starts with the game in no folder the start searches: the time
was the player's in the folder dialog, which the log now states (the boot line's `a folder picked in the first-run screen
after 11.2 s`). Test switches
`PT_TEST_SLOW_GAMEPAD_MS`, `PT_TEST_SLOW_AUDIO_MS` and `PT_TEST_SLOW_DIRECTINPUT_MS` slow those starts down (the controllers' with
the joystick lock held); `pt_device_start_test` and `tools/boot_timing_check.py` (focus-free windowed runs) use them.
