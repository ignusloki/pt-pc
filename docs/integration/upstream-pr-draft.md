# Local upstream PR draft

This is preparation for a future PR to `LoreanXavier/pt-pc:main`. No PR has been opened and the integration branch remains local. Update the validation paragraph after native Intel and Windows/Linux results are available.

## Proposed title

Add native macOS PKG installers and reconcile tested Mac rendering/input features

## Proposed description

Mac users can select their own complete P.T. fake PKG or decrypted game folder and an installation location through a native Setup app. Separate Apple silicon and Intel packages reuse the existing portable app packer, preserve upstream's app name/version/updater conventions, and replace signed bundles as a whole during update or repair with rollback. No original game archives are distributed; the self-contained extractor includes its matching source and notices.

The game requests microphone permission during normal Mac startup, offers a persistent microphone-input toggle in PC Settings, and uses the supplied icon in both apps. The contribution also adapts the tested MetalFX temporal scaler, Vulkan/Metal queue ordering, fullscreen mouse mapping and bounded upload/texture-loading work to current upstream. The optional Fast Walk setting is off by default and preserves interactions and game timing. MetalFX frame generation from fork PR #3 is excluded.

Apple frameworks and Objective-C++ remain guarded by the Mac build path. Windows/Linux release scripts, strict Windows upscaler requirements, updater discovery and current voice/controller/audio implementations are retained. Native validation workflows build the game and installer on both Mac architectures, Windows and Linux. A small migration fix preserves warnings for linked-only legacy data and permits retry after a directory-read error rather than marking the migration complete.

Validation completed locally: ARM64 and x86_64 packages; 19 regression suites per architecture (Intel execution through Rosetta); 16 Python packaging tests; synthetic extraction; packaged voice and installer rollback checks; GPU reflection/queue tests; a complete MetalFX route (27/27), bathroom (8/8), photo mode (5/5), microphone controls (21 expectations), Fast Walk/persistence (28+4), and startup focus (9). Existing player-profile hashes were preserved. The Intel build uses the macOS 15.5 SDK with Apple Clang 21. Native Intel gameplay/microphone, native Windows/Linux CI and physical-controller behavior remain pending, so this description does not claim those platforms fully validated.

Credits: preserve ahm3texe's upstream Mac contribution and ginnfx/brajan zalta's MetalFX contributions and synchronization work. The exact local evidence and remaining gates are in [the integration report](upstream-integration-20261011.md).

## Before opening

- [ ] Record the 2019 Intel MacBook Pro gameplay/microphone results and required GPU features.
- [ ] Authorize publishing the integration branch, then run native Mac and desktop workflows and resolve any failures.
- [ ] Review shared renderer behavior on Windows/Linux graphics hardware.
- [ ] Agree with the maintainer whether default-off Fast Walk belongs in this PR or a separate follow-up.
- [ ] Refresh this draft around the final scope and validation results; keep historical local evidence out of the reviewer-facing description when it is no longer needed.
