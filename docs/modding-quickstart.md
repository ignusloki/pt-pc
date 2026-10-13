# Making your first mod

A mod is a folder inside `mods/`, next to `pt.exe` (the game makes the `mods` folder on its first start). Put a mod's
folder there, restart the game, done. Nothing needs converting or packing.

```
pt.exe
mods/
  my-mod/
    mod.json        name and version (optional)
    level.json      changes to the levels (optional)
    init.lua        a script that reacts to the game (optional)
    Assets/         files that replace or add game files (optional)
```

Once a mod is installed, PC settings shows a **Mods** row under Graphics, where each mod can be switched on and off.
Every mod writes what it did to `pt.log` (in the `data` folder); search it for `mods:`.

The examples in [`examples/mods`](../examples/mods) all work as they are: copy one into `mods/` and start the game.

| You want to... | How | Example |
| --- | --- | --- |
| Put a new object in a level | a `.glb` model and one `add` entry in `level.json` | `added-table` |
| Swap an object for your own model | one `edit` entry with `replace` | `replace-basket` |
| Remove objects | one `edit` entry with `hide` | `hide-pictures` |
| Change the lighting | `edit` entries with `light`, `tint`, `brightness`, `off` | `red-hallway` |
| Move, turn or scale an object | `edit` with `move`, `turn`, `scale` | see below |
| Replace a game model (a prop, Lisa) | your `.glb` at the game model's path, under `Assets/` | see below |
| Repaint a texture | a `.png` at the texture's path | `wall-pictures` |
| Replace a sound | a `.wav` named after the sound's id | `flashlight-click` |
| React to the game in a script | `init.lua` | `event-logger` |

## Step 1: find the name and position of things (F8)

With any mod installed, press **F8** in the game. `pt.log` then shows what the camera looks at:

```
mods inspector: you stand in stage .../pt14_hallway.fpk at [0.12, -1.40, 3.95] (stage space, for level.json "at")
mods inspector: looking at model "shsb_bskt001_0000" (2.1 m away) in stage .../pt14_hallway.fpk
mods inspector:   full name pt14_hallway|pt14_hallway_environ|shsb_bskt001_0000, model file /Assets/.../shsb_bskt001.fmdl, at [-9.14, -1.40, 9.67]
mods inspector:   light "7PL_wall0002 (PointLight)" 1.3 m from that point
```

That's everything `level.json` needs: the stage (`pt14_hallway`), the model's name (`shsb_bskt001_0000`), the light names,
and positions. Use the free camera (F6) to fly to anything you want to inspect.

## Step 2: level.json

`level.json` has two lists. `edit` changes what the game places, `add` places your own models. Comments (`//`) are allowed.

```jsonc
{
  "edit": [
    // remove things: a name ending in * matches every name that starts that way
    { "stage": "pt14_hallway", "model": "shsb_hous001_pict*", "hide": true },

    // move 0.5 m along x, turn 90 degrees about the vertical axis, make it 20% bigger
    { "stage": "pt14_hallway", "model": "shsb_radi001_0000", "move": [0.5, 0, 0], "turn": [0, 90, 0], "scale": 1.2 },

    // draw another model in its place (a game model's .fmdl path or a mod's .glb)
    { "stage": "pt14_hallway", "model": "shsb_bskt001_0000", "replace": "/Assets/mods/my_mod/crate.glb" },

    // keep it visible but let the player walk through it
    { "stage": "pt14_hallway", "model": "shsb_asht001_0000", "collision": false },

    // lights: colour multiplier, brightness factor, or off
    { "stage": "pt14_hallway", "light": "*", "tint": [1.0, 0.3, 0.2], "brightness": 0.8 },
    { "stage": "pt14_hallway", "light": "7PL_wall0002", "off": true }
  ],
  "add": [
    // your own model; "collision" is "box" (its bounds, the default), "mesh" (its triangles) or "none"
    { "stage": "pt14_hallway", "model": "/Assets/mods/my_mod/table.glb", "at": [0.45, -1.4, 4.2], "turn": [0, 90, 0], "collision": "box" }
  ]
}
```

- `stage` is part of the level package's name: `pt14_start` (the start room), `pt14_hallway` (the corridor every loop
  uses), `pt14_hallway_maze_A`/`B`/`C` (the peephole loop), `ending` (the street). Leave it out to match every level.
- Positions are metres in the level's own space, the numbers F8 prints. In the hallway the floor is at `y = -1.4`.
- `turn` is degrees about x, y and z. `scale` is one number or `[x, y, z]`.
- If an entry matches nothing, `pt.log` says `0 matched`. Check the name with F8.

## Step 3: your own models (glTF)

The game reads **glTF 2.0** models: `.glb` (one file, textures inside; the easiest) or `.gltf` with its `.bin` and images
next to it. Blender exports both (File > Export > glTF 2.0, Format "glTF Binary", "+Y Up" left on, the default). One
Blender unit is one metre.

- **New objects:** put the `.glb` anywhere under your mod's `Assets/` folder, e.g. `Assets/mods/my_mod/table.glb`, and use
  that path (`/Assets/mods/my_mod/table.glb`) in `level.json`.
- **Materials:** the base colour (an image or a plain colour) and the normal map are used. Double-sided and alpha-cut
  (Blender's "Alpha Clip") materials work. PNG and JPEG images both work.
- The model's origin is the point `at` places. Put the origin at the bottom of the object so it stands on the floor.

### Replacing a game model, Lisa included

Put your `.glb` (or `.gltf`) at the **same path as the game's model**, with its own extension: the game model
`/Assets/sh/chara/och/Scenes/och0_main0_def.fmdl` (Lisa) is replaced by
`mods/my-lisa/Assets/sh/chara/och/Scenes/och0_main0_def.glb`.

The best start is the game's own model, exported to glTF:

```
python tools/fmdl.py export <your extracted files>/Assets/sh/chara/och/Scenes/och0_main0_def.fmdl --out work/lisa
```

Open `work/lisa/och0_main0_def.gltf` in Blender, change it, and export it again as glTF. What the port keeps:

- **Animated characters:** the skeleton is the game's. Your mesh's bones are matched **by name** to the original's, in
  the original's rest pose, so keep the bone names and the pose of the exported model. A bone the game's skeleton doesn't
  have is logged and weighted to the root. Animations, AI and sounds stay the game's.
- **Materials:** the exported model carries each material's game shader and textures (its "extras"). Kept, they draw
  exactly as the game's (a re-export of the unchanged model looks identical). A material you make new in Blender becomes
  the standard opaque material with your images.
- **Collision** stays the original model's.

Textures of a game model are replaced separately, as PNGs at their own paths (`wall-pictures`). The exported model's
material extras list each texture's path.

## If it does not show up

- Start the game, then search `pt.log` for `mods:`. Every mod, every `level.json` entry (with how many things it matched)
  and every model or texture taken from a mod is listed there, and so is the reason when something was refused.
- A mod switched off in PC settings stays off until it's switched on again.
- Mods are read once at start: restart after changing one.

The full reference, including textures, sounds, the Lua script API and the priority between mods, is in
[modding.md](modding.md).
