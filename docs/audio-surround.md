# Surround output

## Output device

PC settings > Sound > Output device (pt.ini `[audio] output_device`) picks the playback device of the game's mix by the name SDL
lists; empty is System default, the system's default device (on Windows the console role, which SDL and the spatial path
follow). Both the ordinary output and Windows spatial sound open the chosen device, the spatial path by the Windows endpoint id
matched to that name (`engine/audio/output_device.h`, `playback_endpoints.h`). A change opens the device again at once; a chosen
device that is not connected leaves the system default playing until it is back. When the system default is a controller's own
endpoint (a USB DualSense often becomes the Windows default when it is plugged in), the row shows "System default (controller)"
and pt.log warns. The game never changes the system default. pt.log lists every playback device with its endpoint id and flags,
and the main output with its id at the start, on every reopen and when the system default moves.

Windows spatial sound activates the static 7.1 bed on any endpoint, also one without a spatial format (checked on Windows 11
26200: the full static mask, no dynamic objects, the stream starts). Windows then mixes the bed down to the device's channels.
pt.log says which: "a spatial format is on (n dynamic objects)" or "no spatial format is on for this device".

Surround output is optional. The existing stereo `SoundEngine::Render(out, frames)` path remains the default and keeps its original two-channel master mix. `SoundSystem::Init(vfs, open_device, surround_output)` enables host negotiation when the user setting is on.

The renderer uses Wwise's internal speaker order `FL, FR, FC, BL, BR, SL, SR, LFE`. SDL's 7.1 order is `FL, FR, FC, LFE, BL, BR, SL, SR`; its 5.1 order is `FL, FR, FC, LFE, BL, BR`. The current game speaker renderer has no LFE signal, so the LFE position is silent. Six-channel output folds each back/side pair into the corresponding SDL surround channel at -3 dB per source, preserving both rear fields.

The playback device's preferred channel count selects 7.1 at eight channels, 5.1 at six or seven channels, and stereo below six. If opening the surround stream fails, playback retries in stereo. SDL documents these channel layouts and cross-platform swizzling in [CategoryAudio](https://wiki.libsdl.org/SDL3/CategoryAudio), and `SDL_GetAudioDeviceFormat` reports the opened device format or its preferred format before opening ([API](https://wiki.libsdl.org/SDL3/SDL_GetAudioDeviceFormat)).

The per-speaker render uses the mix already routed by Wwise speaker gains. The master peak limiter shares its existing linked-channel detector and lookahead gain across the speaker channels. Existing effect buses still render as stereo and enter the master speaker field on front left and front right, as they do in the current limiter side-chain model.
