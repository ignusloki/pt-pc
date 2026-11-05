# Motion blur modes

Graphics > Motion blur lists Camera + object, Object only, then Off.
Camera + object is the default for new settings. Existing
settings with motion_blur=1 continue to use Camera + object; motion_blur=0
remains Off. The separate camera_motion_blur boolean defaults to true when
missing. Object only saves motion_blur=1 and camera_motion_blur=0. Original
and Ultra presets retain full blur, while Low and High retain Off.

The scene's authored motion-blur enable and shutter duration remain effective.
Both enabled modes use the same HDR-compatible linear-light reconstruction,
shutter timing, velocity bounds, depth rejection and coverage composition.
Motion blur stays before creative grading and HDR output mapping.

## Velocity construction

For current/previous world-space vertex positions X and Xprev, object-only
velocity is the projected difference Pcurrent(X) - Pcurrent(Xprev). Full mode
uses Pcurrent(X) - Pprevious(Xprev). Perspective division precedes conversion
to the engine's velocity units; bounds are applied afterwards. Both rigid
model transforms and previous skeletal poses participate. Camera translation,
rotation and projection changes therefore cannot generate object-only blur.
Static background velocity is explicitly zero in that mode.

Rasterization keeps temporal jitter to match the scene depth buffer, but
motion positions have jitter removed. The depth-derived camera velocity also
excludes jitter. Camera cuts invalidate object history as well as camera
history. Newly visible objects without a previous pose do not invent motion.
The separate upscaler/frame-generation motion shaders keep their full vectors.

This follows the camera-control construction in section 4.1 of
[McGuire et al., A Reconstruction Filter for Plausible Motion Blur](https://casual-effects.com/research/McGuire2012Blur/McGuire12Blur.pdf).
The fork's existing reconstruction filter is retained. Its object-velocity
pass covers opaque deferred meshes with rigid or skeletal history; it does
not introduce separate velocities for transparent particles, moving shadows
or reflected objects. Those remain limitations of this screen-space method.

## Validation

- Production velocity vertex/fragment shaders on Vulkan and D3D12: static,
  translating, rotating and skinned objects under camera translation,
  rotation/projection changes and temporal jitter (64 cases per backend).
- Production camera/object merge shader: zero static-background vectors in
  Object only; camera and covered object vectors in Camera + object.
- Existing GPU tests retain frame-rate-independent blur footprint, zero-motion
  identity and HDR light conservation checks.
- Settings roundtrip/legacy migration, preset classification, camera-cut and
  camera-smoothing tests, and the native menu script in
  tests/pad/motion_blur_modes.txt.

The local Windows x64 Release build is intended for further gameplay testing;
these checks do not certify every animation or transparent effect in the game.
