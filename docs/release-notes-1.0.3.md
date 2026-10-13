# P.T. PC Port 1.0.3

Everything since 1.0.2. Run the setup over your existing installation to update: the game files are replaced, your
game archives, settings, saves and mods stay.

## Saying "Jack" without a microphone

The 1.0.2 notes made this sound harder than it is. You don't need to edit `pt.ini`:

1. Open **PC settings > Sound** and turn on **Assign J / L2 + R2 for the microphone trigger**. The row names your
   controller's buttons (L2 + R2 on PlayStation, LT + RT on Xbox, ZL + ZR on Nintendo).
2. When the game wants you to say "Jack", press **J** on the keyboard or **both triggers together** on the controller.

It only does something while the game is listening, so pressing the triggers anywhere else does nothing. The key can
still be changed in `data\pt.ini` (`key` under `[voice]`).

## Mods: your own models and level changes

- **glTF models:** a mod can now put a `.glb` or `.gltf` (Blender's glTF export) in place of a game model, Lisa included
  (her skeleton and animations stay the game's), or add new models. Exporting a game model with `tools/fmdl.py export`,
  editing it in Blender and exporting it again keeps its original materials: unchanged, it looks exactly like the game's.
- **level.json:** hide, move, turn, scale or replace the objects a level places, tint, dim or switch off its lights, and
  add your own models with collision, without touching any game file.
- **F8 inspector:** with a mod installed, F8 writes to `pt.log` the name and position of what you look at and the lights
  near it, which is everything `level.json` needs.
- **Six examples** in `examples/mods` (a new table, a basket swapped for a crate, a red hallway, pictures taken
  down, a texture repaint and a sound swap) and a rewritten, simpler [modding guide](https://github.com/LoreanXavier/pt-pc/blob/v1.0.3/docs/modding-quickstart.md).
- The game now creates the `mods` folder next to `pt.exe` on its first start, with a short `README.txt` inside.

## macOS

- **Apple MetalFX** upscaling on the Mac build, and experimental MetalFX frame generation on Apple silicon with macOS 26
  or newer.

## Museum

- **Navigation:** browse models and audio with clickable arrows. Button prompts follow your keyboard or controller.
- **Model viewer:** the same movement and look controls as photo mode, including moving closer, moving vertically and
  faster movement.
- **Lighting:** model previews have balanced lighting; the radio opens facing forward, with lighting that follows the
  inspection camera.
- **Audio:** speaker icons identify the recordings, and their complete transcripts stay at a consistent brightness.
  Descriptions and controls no longer overlap the thumbnail strip.

## Fixes

- **Photo pieces:** the handwriting stays sharp with motion blur enabled.
- **Explore outside:** the player body appears in third person, free camera and photo mode; city film grain no longer
  carries into the pause menu.
- **Photo mode in the ending:** starts at the current cutscene camera instead of jumping to the gameplay player.
- **Loop browser:** Gouge it out starts with the eye intact and can be replayed. Dying before solving it no longer leaves
  it already gouged on respawn; every later loop keeps the gouged picture.

- **Lisa in the last loop ran at you the moment she appeared**, even with your back turned or 8 m away: her attack check
  used the spot she had been hidden at (often right by the phone table), not the one she appeared at. She now only attacks
  as on PS4.
- **The f070 window glass** scattered the wrong way and piled up behind the window frame in the hallway copies that face
  another direction. It now falls and lies as on PS4.
- **The fridge's blood splash** on the view is now blurred by the depth of field as on PS4, instead of sharp.
- **White outlines and over-sharp edges with DLSS/FSR** along wall corners, plants, hair and Lisa: the flashlight is now
  removed from the upscaler's input in a way that no depth edge can show.
- **DualSense over Bluetooth:** the haptics no longer hold up frames.
- **Freezes of up to 20 seconds** when entering the last loop and every time the pause menu was closed there: on some
  systems, opening the microphone takes that long. It now opens in the background and the game keeps running.
- **Completed photo in the Museum:** it is now unlocked once all five pieces and the option frame have been found, even
  across different runs or before the entry existed.
- **Peephole:** the look range is centred on the hole, as on PS4. Before, it reached too far right and not far enough left.
- **Lisa's slight mouse movement kill** behaves as in the original again.
- **Museum:** names no longer show through behind the pictures.
- **Reset progress:** the description now says your language, PC settings, the loop browser and the Museum stay unlocked.
- **Photo mode and free camera in the ending:** both open there and now also move, look and take input (the ending had
  blocked their controls).
- **Photo mode:** "Player body: Hidden" works with third person on, and the help line at the bottom fits on the screen in
  every language.
- **Fake crash:** after the bug screen the picture now cuts straight to black, as on PS4, instead of showing the hallway for
  a moment.
- **HDR:** the HDR option takes effect again (it had stayed SDR on every PC). If the display or an overlay refuses HDR, the
  game falls back to SDR instead of failing. FSR frame generation stays off while HDR is on.
- **HDR:** the brightness page and the menus show their dark tones correctly, lamp bulbs are no longer cyan, and dark rooms
  are no longer greyed.
- **Lens distortion:** the colour fringe no longer breaks the wet street's fine grain into rainbow ellipses around the
  centre of the screen (it is now taken from a softened picture; lamps and edges keep it), and the lens resamples with a
  sharper filter that leaves no bands sliding over the floor.
- **Ray traced reflections:** the street lamps' reflections on the wet road no longer go out and come back while you
  turn the view: the lamps lighting what the reflections show off screen now stay in the light list.
- **HDR:** the game now uses SDR when Windows HDR is off for your display (a grainy, harsh picture before), and the film
  grain no longer greys dark areas with HDR on.
- **HDR switches without a restart:** the in-game HDR option and turning Windows HDR on or off for the display now take
  effect while the game runs. As in other PC games, the **HDR** row (formerly "Native HDR") can only be turned on while
  Windows HDR is on for your display; otherwise it is greyed out and says where to turn it on.
- **Ultrawide screens:** the options menu darkens the sides of the screen outside its page, and the fake crash's bug screen
  fills them with its colour, instead of showing the game there.
- **The final loop's X photo** showed without its X after quitting and resuming the game; floors past a puzzle now always
  take it as solved.
- **Options menu:** PC Settings, Back and Quit show the selection bar while the mouse is over them, so they read as buttons.
- **Incomplete game files** (core packages missing) now show an error message instead of closing silently. (#58)
- **Upscalers:**
  - The tearing view effect keeps its anti-aliasing instead of looking like FXAA.
  - Plants and Lisa no longer get a white outline against walls with the flashlight on.
  - An upscaler that fails to start is retried instead of staying off for the session.
- **Frame generation:** a failure to turn it off (low video memory) is retried instead of closing the game.
- **DLSS Frame Generation with an fps limit:** a 60 fps limit now shows 60 frames (30 rendered); it had shown 30 from 15.
- **Free camera:** no longer limited to 15 m from where it started (the photo mode keeps its range). (#64)
- **Third person:** near walls, the arm holding the flashlight no longer twists through itself and the light no longer goes
  into the head.
- **Third person, photo mode and free camera:** the flashlight sits in the hand's grip instead of passing through the
  fingers.
- **Picking up the flashlight:** once it lights the wall you can move the light with the stick while the scene still holds
  the view, as on PS4.
- **Photo pieces' handwriting** is drawn as on PS4: a faint light trace added to the picture, not an opaque grey layer.
- **Start screen:** the title picture now shows first and alone. The studio logo and the wake-up of a resumed game come after
  it instead of playing under it. The title picture also matches the PS4 brightness.
- **Wall decals** (the cracks) no longer draw over a painting in front of them up close at low resolutions.
- **Ending:** photo mode no longer shows your character's body there.
- **Game+ (second finish):** the Lisa standing at the end of the corridor shows the Game+ look too. Her shoulder no longer has a hole in the Game+ look.

## VR (experimental)

- **Room scale:** walking around in your room now moves your character with you (with collision), so doors, triggers and the
  body in the mirror stay where you are. Before, the view could drift up to half a metre from the character. (#63)
- **One eye only:** with ray traced contact shadows, ambient occlusion or reflections on, the right eye showed parts of an
  older frame (Lisa still in the floor's reflection, insects and lights in one eye). Both eyes now trace the current frame. (#62)

## Street walk

- **Flashlight:** press **F** during the street walk to switch the game's flashlight on and off (not in the original).
- The street now keeps the lighting of the ending's first street shots (dark, with the orange fog at its far end)
  instead of the hazy grey of the final shot. (#53)

## Game+ and reports

- **Game+ notice:** after a finish, the first time you go through the start room's door into a loop a quiet line at the top
  says which Game+ tier is on ("New Game+ 2/4"). Each tier is announced once; Reset progress resets it with Game+.
- **Send pt.log to the developer:** a new row on the PC settings page (under Progress) sends pt.log, and the latest crash
  dump if there is one, in one press. Nothing else is sent.
- **Steam Deck and Steam Controller:** the face buttons in the prompts are round, as on the hardware (they looked like
  keyboard keys).

## Diagnostics

- **F9** writes the camera position to `pt.log` as a reference capture line. If you report something that differs from
  PS4, press F9 where you see it and include `pt.log`: it lets the PS4 version be checked from exactly your view.
- **Crash reports:** after a crash, the next start asks whether to send the crash dump and `pt.log` to the developer
  (Send, Always send, Don't send). Nothing is sent without your answer.
- `pt.log` now records how long each system takes at start (graphics, controller, audio) and how long the microphone
  takes to open, to find slow starts on some PCs.
