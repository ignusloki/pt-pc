# Fast walk

Fast walk is optional and off by default. Open PC settings (F10, or PC settings in the pause menu), select Extras,
and turn Fast walk on. The choice is saved and takes effect immediately; no restart is needed.

Hold either Shift key or the controller's bottom face button (Xbox A / PlayStation X) while walking for a 1.5× boost.
Release the button to return to normal speed. On a Nintendo controller this is the physical bottom face button, B.
The controller button still performs its normal interaction action. Keyboard interaction keys and mouse clicks
do not activate the boost.

The boost multiplies the player's locomotion rate, including the original directional speeds, analog stick
strength and red-loop speed. It does not change the game clock, camera controls, puzzle timers, gravity or collision
handling. Idle movement and movement locked by a cutscene stay at their original rates. Free camera and the street
sequence keep their existing controls.

The behavior follows the README in the supplied PT-Fast-Walk-Windows-Linux package. That package contains compiled
Windows/Linux plugins and loader configuration, with no source scripts. This implementation is built into the port;
it does not load or redistribute those plugins and requires no external patcher on macOS, Windows or Linux.

For configuration files:

```ini
[extras]
fast_walk = 1
```

`pt_fast_walk_test` checks keyboard/gamepad input, release behavior, interactions, directional rates, movement locks,
camera controls and original-frame cadence without game files. Pass the path to a decrypted P.T. folder to also
compare actual movement distance using the original animations. `tests/pad/fast_walk.txt` exercises the live setting
and held inputs through the game; it needs a game folder and isolated settings/save paths.
