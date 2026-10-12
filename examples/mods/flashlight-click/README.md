# Flashlight Click

Replaces one sound: the flashlight's switch-on click (the event `Play_sfx_light_on_01`, Wwise media `1058735461` in
`sfx_common.bnk`) becomes a short beep. A 16-bit PCM `.wav` named `<media id>.wav` under `Assets/sh/sound/wem/` takes the
place of the media with that id; the volume, pitch and position still come from the game's bank.

Hear it with the street walk's flashlight (F, from Extras > Street walk), or when the flashlight comes on after the pickup.
To find other sounds' ids, `python tools/wwise_bank.py extract <bank> --out <dir>` writes every embedded sound as `<id>.wem`.
