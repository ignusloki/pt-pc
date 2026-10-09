# Local integration source refresh

On 9 October 2026, `codex/upstream-macos-integration` merged upstream
`fa6fabbf24102e43e1de054f472e690b335a1e25` from the prior local commit `1b320a4`.
The branch remains local at the user's request. Only the stable
`codex/macos-arm64-port` branch is authorized for a push during this sync.

The source retains the unified portable/native installer packaging, startup
microphone consent, supplied icons, official Microsoft runtime selection and
macOS 14 dependency audits. MetalFX and optional Fast Walk remain follow-up
integration items; they were preserved separately on the stable Mac branch.

The four merge conflicts were reconciled in CMake and the shared installer and
extractor code. The installer transaction regression target was retained, along
with whole-app repair/rollback, legacy `P.T..app` migration, upstream writable-data
protections and the unreadable inner-PKG diagnostic.

Checks repeated after this source refresh on the M1 Pro:

- Fresh ARM64 configuration and native release game/Cocoa installer compilation passed.
- Core, voice matching, settings roundtrip and installer transaction regressions passed.
- All 16 Python packaging regressions passed.
- The fork-specific diff passes the whitespace check against the upstream snapshot.

Evidence: `/Users/suporte/PT-Mac/build/upstream-sync-20261009/`, including
`integration-build.log`, `integration-regression-results.json` and
`integration-python-packaging.log`.

The existing ARM64 and Intel preview packages were not overwritten or rebuilt
for this refresh. Their earlier acceptance results are historical, not results
for `fa6fabb`. Native Intel/Windows/Linux workflow execution and live microphone
consent/input remain pending. The stable branch's release assessment is recorded
in its `docs/upstream-sync-20261009.md`.
