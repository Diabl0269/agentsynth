# File-Size Cap

`scripts/check-file-sizes.sh` enforces a hard **1000-line cap** (`FILE_SIZE_CAP`) on every
git-tracked source, test, docs, script and config file. It runs in
[the Lint job](ci-pipeline.md#jobs)'s "Check file sizes" step, in the "Docs" job, directly from
[`local-ci.md`](local-ci.md)'s local reproduction, and in the pre-commit hook.
`scripts/tests/check-file-sizes.test.sh` covers the checker itself against fixtures.

One cap for everything: a 6,000-line test file is exactly as unreviewable as a 6,000-line source
file, and a per-directory cap would only move the goalposts.

**Why.** The editor's single graph-editor source file crossed 9,000 lines — it is now the
per-concern units under `Source/UI/Graph/GraphEditor/` — and several test files passed 6,000, before
this guard existed. A file that size turns every change into a scroll through unrelated concerns,
inflates review diffs with untouched context lines, and makes merge conflicts far likelier between
two people editing different features that happen to share a file.

## Strict ratchet baseline

`scripts/file-size-baseline.txt` grandfathers files already over the cap at their EXACT current line
count, so the cap does not force a freeze-and-split-everything-today migration. It only ever
tightens:

- A baselined file may never grow past its entry — do that and the check fails, naming the exact
  growth (`grew from N to M lines`) and pointing at a `<Class><Concern>.cpp` split instead.
- Shrink one and its entry must tighten to match: run `bash scripts/check-file-sizes.sh --update`,
  which rewrites the baseline from the current tree and prints what changed. A too-loose entry fails
  the check on its own, naming `--update` as the fix.
- Get a file back under the cap and its entry must be removed entirely — `--update` does that too.
- **No file may join the baseline as new.** A file crossing the cap for the first time fails
  outright, naming the split it needs.
- `--update` never raises an entry or adds a new one either — it refuses (exit 1, baseline left
  untouched) rather than launder a grown or newly-added file through, and a same-size `git mv` is
  recognized and let through automatically. `--allow-growth` is the deliberate, reviewed exception,
  printing a `::warning::` per raised or added entry.

`bash scripts/check-file-sizes.sh --list [N]` prints the N largest scanned files (default 25),
largest first, regardless of cap or baseline — for picking what to split next. The baseline file
itself is the current list of everything over the cap.

## How to split an over-cap file

The full rules live in the root `CLAUDE.md`'s "Code structure" section. In short: a class that
outgrows one file gets its own directory named after the class, holding the header and every unit
(`Source/UI/Graph/GraphEditor/GraphEditor.h` plus `GraphEditor<Concern>.cpp` units, never `_Part1`,
plus shared private helpers in `GraphEditorInternal.h`) — never flat siblings dropped next to
unrelated files. Tests mirror it
(`Tests/UI/Graph/GraphEditor/GraphEditor<Topic>Tests.cpp`, with shared fixtures in
`GraphEditorTestFixture.h`).

A directory itself gets split by area once it passes roughly 30 files. `Source/UI/` holds only area
directories (`Graph/`, `Timeline/`, `PianoRoll/`, `Library/`, `Macros/`, `ModuleViews/`,
`Settings/`, `Assistant/`, `Chrome/`, `Layout/`, `Theme/`), each class directory nested in its area.
`Tests/` mirrors the code by area — `Modules/`, `FX/`, `Engine/`, `Plugin/`, `Timeline/`, `Mixer/`,
`Macros/`, `AI/`, `Account/`, `Project/`, `App/` (MainComponent-level and end-to-end), and
`UI/<area>/` matching `Source/UI/`; only shared helpers (`TestMain.cpp`, `TestAudioHelpers.h`,
`FakeAudioIODevice.h`, `StubPluginInstance.h`) and the `fixtures/`/`reference/` data stay at the
root. Data next to the test tree is resolved from the `TESTS_ROOT_DIR` compile definition, never
from a test file's own `__FILE__` depth.

## Scope and exclusions

All deliberate; the script's own header comment has the full reasoning. `assets/`, `mockups/`, any
local `build*` directory, `.claude/`, and recorded JSON fixture corpora (`Tests/fixtures/`,
`Tools/TimelineOpsHarness/Fixtures/`) never count toward the cap — their size reflects recorded
data, not hand-authored structure. The guard's own baseline file is excluded from itself.

**Hook-environment gotcha.** A git hook process inherits `GIT_DIR` (and sometimes
`GIT_WORK_TREE`/`GIT_INDEX_FILE`) from git itself, which broke `ROOT` resolution and made the whole
guard pass vacuously — every file read as 0 lines, "0 over the cap" — instead of scanning anything.
The script now `unset`s those variables up front, fails loudly if `git rev-parse --show-toplevel`
still cannot resolve `ROOT`, and carries an awk-level fail-safe that refuses to report a clean pass
if every scanned file comes back as 0 lines. Every harness that builds fixture repos does the same,
and so does `ci-local.sh`, which the pre-push hook runs: a hook-run harness once staged every real
file as deleted.

A docs-only PR is gated by this guard too, through the "Docs" job rather than Lint — see
[`ci-pipeline.md`](ci-pipeline.md#docs-only-pull-requests).

Two sibling guards check other shapes of the same problem:
[`function-size-guard.md`](function-size-guard.md) caps a single function, and
[`header-comment-guard.md`](header-comment-guard.md) enforces comment placement in headers.
