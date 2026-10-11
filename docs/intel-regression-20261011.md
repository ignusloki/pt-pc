# Intel installer accepted — 11 October 2026

The user tested the graphical Intel installer on their **2019 MacBook Pro** and reported that everything seemed to work, although the game was a little slow. They authorized committing the integration and preparing an official GitHub release with Apple silicon and Intel packages.

The tested package was `pt-port-1.0.4-setup-macos-x64.zip`, built from `d397c576daec2f29426585d5386e208af75cd004`. It contains the native x86_64 game, graphical Setup, self-contained extractor and target voice modules. The game and shader sources match the [accepted Apple silicon baseline](apple-silicon-regression-20261011.md).

Before handoff, 19 selected runtime suites passed through Rosetta on the build Mac, with synthetic extraction, packaged voice loading, installer unpacking/rollback, signatures, minimum macOS version and icon checks. The Setup's game-file and destination pickers were inspected. A cold translated voice-load fixture exceeded its 60-second deadline; its completed load, retry and packaged check are retained in the local validation records. Those translated checks are separate from the user's native Intel gameplay test.

The user did not supply the exact Intel GPU, RAM, macOS version, settings or measured frame rate, or individual checklist results. This is an accepted installation/gameplay smoke test, not a performance benchmark or a guarantee for every Intel GPU. The release recommends Windowed 1280 × 720, Low, 30 FPS, Reflections Off and enhanced textures Off as an Intel starting point. MetalFX is optional and must be supported by the GPU.

The release merge retains the tested integration packager and installer, including whole-bundle replacement and legacy `P.T..app` repair. Version 1.0.5 packages are rebuilt from the release commit. Their per-build update endpoint points to this fork; the source's default upstream endpoint is preserved for future integration.

Native Windows/Linux CI and review on their graphics hardware remain gates for the future upstream PR. This Mac release does not publish Windows or Linux installers or claim those platforms newly validated.
