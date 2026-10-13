# Upstream update review — 11 October 2026

## Decision

The update can be integrated, but it is not a clean or currently validated merge. A trial three-way merge has **23 conflicted files**. Most concern two implementations of MetalFX scaling/frame generation and their renderer interfaces. There is also an upstream microphone lifetime defect and a publishing incompatibility with this fork's annotated tags.

Recommendation: reconcile the renderer as one coherent implementation on an integration branch, apply the two small fixes supplied with this review, then build and regress both Mac architectures before updating the stable branch or publishing a release. Do not resolve the rendering conflicts by choosing one side for every file.

## Preserved work and comparison points

- Spatial-audio research branch: `codex/spatial-audio-research`, commit `cbc256ac6780100f8c4973c2cccdf8e7ef88de37`. It contains the feasibility report, corrected surround documentation, native Mac probe source, analysis script, results and reproduction instructions.
- Released Mac baseline: `codex/macos-arm64-port` / `origin/codex/macos-arm64-port`, `b05adee04ec484bffc6d672ec57a830445817367` (v1.0.6).
- Fork `main` and previous upstream baseline: `edb5a85ca51cb7d6701f862150308b30dff80e3a`.
- Reviewed upstream `main`: [`61139aa3752a4afbd41318015db77d8084efe9bd`](https://github.com/LoreanXavier/pt-pc/commit/61139aa3752a4afbd41318015db77d8084efe9bd).
- Review branch: `codex/upstream-review-20261011`. This branch contains the assessment and proposed patches, not an integrated game build.

Upstream adds 12 commits since the common baseline. Its delta covers 149 files, with 6,386 added and 582 removed text lines, plus binary example assets. Upstream's release version is 1.0.3; the fork's version is independently 1.0.6. Integration must retain the fork's version until its next release is prepared.

## What upstream changed

| Area | Changes relevant to this fork |
| --- | --- |
| Mod support | glTF/GLB model loading, `level.json` edits for hiding/moving/replacing objects and lighting, working examples and expanded mod documentation |
| Rendering | A new native MetalFX implementation, HDR/tone-mapping changes, restored indoor motion blur, reflection/light-list and upscaler edge corrections, glass/blood/VFX fixes |
| Museum and photo mode | Clickable navigation arrows, viewer lighting and camera controls, thumbnail/transcript improvements, photo handwriting and ending-camera fixes |
| Gameplay and progress | Gouge-puzzle replay/respawn fixes, final-loop X-mark state, Lisa/peephole/flashlight corrections, Game+ notices and their saved announcement state |
| Devices | Background microphone opening to avoid long stalls, background Bluetooth DualSense haptic writes, startup timing diagnostics |
| Reports and releases | Opt-in crash/log upload to the upstream developer's Sentry project, a Linux portable archive, and a manual Mac artifact-publishing workflow |
| Older Mac SDKs | Floating-point JSON parsing avoids the newer `from_chars` dependency; frame interpolation is guarded for pre-26 SDKs; the display-HDR worker uses `std::thread` instead of unavailable `std::jthread` |

The microphone, MetalFX and HDR changes deserve targeted regression because they overlap features already tested in this fork. This update does not add spatial headphone output; the spatial-audio work remains separate.

## Findings requiring attention

### 1. Upstream microphone completion can destroy a locked mutex

In [`Microphone::OpenAsync`, lines 57–64](https://github.com/LoreanXavier/pt-pc/blob/61139aa3752a4afbd41318015db77d8084efe9bd/src/engine/voice/microphone.cpp#L57), the completion path locks `pending_->mutex` and then resets `pending_` before the guard leaves scope. If the detached worker has already exited, that member is the last owner of the state. Resetting it destroys the mutex while it is locked, and the guard subsequently attempts to unlock destroyed storage.

This is a defect in the upstream implementation itself. It can affect both successful and failed opens and does not require a merge conflict. It was identified by lifetime analysis, not by observing a crash with a physical microphone.

The proposed fix keeps a local shared owner alive until after the guard unlocks. It is supplied in `upstream-20261011-microphone-lifetime.patch`. Patch applicability against the reviewed upstream file passed. Cancellation, failure, repeated open/close and shutdown behavior still require runtime regression during integration.

### 2. MetalFX implementations cannot be combined piecemeal

The fork submits MetalFX through its existing renderer handoff and keeps frame generation in a separate `metalfx_framegen.mm`. Upstream uses explicit `ExternalSubmit` / `ExternalSync` / `ExternalRun` interfaces and lets the renderer acquire/present an additional generated frame. Its interpolator is in `metalfx_backend.mm`.

Factory names, frame-generation interfaces, image-export declarations and command submission ownership differ. Keeping a mixture of those interfaces would produce compile errors or incorrect GPU ordering. Text conflict resolution alone cannot establish correctness.

Prefer the coherent upstream renderer structure for future alignment, then carry over or adapt the fork's queue/lifetime protections, UI behavior, diagnostics and regression coverage. Preserve SDK/runtime capability guards and the default-off frame-generation setting. The existing v1.0.6 frame-generation results do not validate the upstream implementation.

### 3. The new publishing workflow assumes lightweight tags

[`publish-macos.yml`, lines 30–32](https://github.com/LoreanXavier/pt-pc/blob/61139aa3752a4afbd41318015db77d8084efe9bd/.github/workflows/publish-macos.yml#L30) compares the build's commit SHA with `.object.sha` from the tag-reference endpoint. An annotated tag returns a tag-object SHA there.

Verified against the fork's actual v1.0.6 on GitHub:

| Value | SHA |
| --- | --- |
| Tag reference, type `tag` | `0b50dc291b554cb9f548e09c843af125aaedc1b1` |
| Commit resolved by `commits/v1.0.6` | `b05adee04ec484bffc6d672ec57a830445817367` |

Those values cannot pass the workflow's equality check. The proposed `upstream-20261011-publish-tag.patch` uses the commit-resolution endpoint. Its applicability passed, and the endpoint returned the expected commit. The publishing action itself was not run.

The same helper currently selects four portable ZIP/DMG assets. If adopted for this fork's releases, also validate and publish both Setup ZIPs; the existing fork Mac workflow produces those installers.

### 4. Fork behavior must survive the manual merge

Keep the native PKG/folder installers, selected icon, startup microphone permission request, saved microphone toggle, fullscreen mouse mapping, default-off Fast Walk, existing save-migration handling and profile location.

Preserve the Mac release target's shader/font/MoltenVK dependencies and native Setup target while reconciling CMake. Keep tests' assertions active in release-style builds. Fork-only files are retained by a normal three-way merge; replacing conflicted build/main files wholesale with upstream versions could omit the code that connects those features.

The new crash/log reporting uses the upstream developer's Sentry endpoint. Retain explicit consent and make that recipient clear; it should not be presented as reporting to the fork's own maintainer. No report was uploaded during this review.

The save's finish word now also stores the Game+ announcement tier. Validate existing profile loads and writes using copies. An older binary does not understand the new upper-bit field, so a rollback needs the original save backup.

## Validation performed and its limits

- Fetched upstream and both fork branch references; the fork's remote Mac branch still points to the released baseline above.
- Ran an isolated `merge-tree` analysis without changing the stable branch. The result has 23 conflicted paths; the working checkout has no merge in progress.
- Compiled the exact upstream JSON parser with macOS 15.5 SDK, deployment target 14.0, for arm64 and x86_64. Both targeted parser checks passed; x86_64 ran through Rosetta on this M1 Pro.
- Verified the proposed microphone and tag-resolution patches apply to the reviewed upstream source.
- Verified the annotated-tag counterexample and proposed commit-resolution endpoint using read-only GitHub API calls.

No integrated game build, native Intel gameplay, Windows/Linux CI, graphical regression or physical microphone test was performed. The unresolved renderer merge prevents claiming this update is ready for a release. The parser check establishes a narrow SDK compatibility result, not whole-game compatibility.

## Integration and regression sequence

1. Start from the released Mac baseline and merge the pinned upstream history on an integration branch. Reconcile the complete renderer/MetalFX interface group, preserving the fork's Mac features and packaging.
2. Apply the microphone lifetime fix. Adapt the optional publisher for annotated tags and fork Setup assets. Keep crash reporting opt-in with an explicit recipient.
3. Build Apple silicon with the current SDK and Intel with the older SDK; run settings, saves, input, installer, packaging and queue/lifetime checks. Run native Windows/Linux checks before describing cross-platform compatibility as verified.
4. Test cold boot/settings rendering, startup mic permission and mic On/Off, window/fullscreen mouse alignment, Fast Walk and install/update behavior. Use separate test profiles and copies of existing finished saves.
5. Check MetalFX Off/Quality, reflections and mirror/bathroom/maze scenes, motion blur, pause/menu UI, HDR changes and fallback. Test frame generation Off/On, resolution/window changes and a sustained run on supported Apple silicon; test graceful unavailability on Intel/older macOS.
6. Use the loop browser for the gouge replay/respawn, final-loop X-mark, Lisa and ending changes; check Museum lighting/navigation/transcripts, photo mode and Game+ announcements. Verify the final microphone puzzle while a headset is connected.
7. Update the stable Mac branch and prepare a release after those checks pass. Resume spatial-audio implementation from its preserved branch afterward.
