# CLAUDE.md

This repo is the desktop app half of a product whose backend/website (`synth-platform/`)
and marketing assets (`marketing/`) live in sibling repos under the workspace root — see
[`../CLAUDE.md`](../CLAUDE.md) for the cross-repo map when a task touches more than this
codebase. This file stays scoped to `agentsynth/` only.

Guidance for Claude Code (claude.ai/code) in this repository. Keep this file **lean** — it is always loaded, so it holds only commands, conventions, and a map of which area `CLAUDE.md` guards which topics. The rules themselves live in per-directory `CLAUDE.md` files (`Source/`, `Source/Modules/`, `Source/Timeline/`, `Source/AI/`, `Source/UI/`, `Source/Plugin/`, `.github/`), auto-loaded when you work under that directory; the mechanism and history live in `docs/` (map: [`docs/README.md`](docs/README.md)). When you change behavior, update the relevant doc, not this file — docs must never go stale, so treat updating them as part of the change itself, not a follow-up.

## Project

Modular synthesizer (JUCE, C++20) with a node-based graph editor for sound design — connect audio/CV modules in a visual patching environment. Four CMake targets:

- **Core** — the library: all audio-processing modules + core logic. Headless-testable (no audio device, no GUI).
- **AppUI** — the editor UI (MainComponent, GraphEditor, ModuleComponent, chrome), shared by the app and the plugin.
- **AgentSynth** — the JUCE GUI application (`Main.cpp` on top of AppUI).
- **AgentSynthPlugin** — VST3 (+ AU on macOS) plugin wrapping the same AppUI/Core in a `juce::AudioProcessor`.

## Commands

```bash
# Build (ENABLE_PLUGIN defaults ON, so this also builds the VST3/AU plugin — pass -DENABLE_PLUGIN=OFF for an app-only loop)
# For a Ninja build matching CI's speed, see docs/development/testing.md#build-flags
cmake -S . -B build && cmake --build build

# Test  (ENABLE_TESTS defaults OFF — must opt in)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests
./build/Tests/Tests --gtest_filter="E2EWorkflow*"   # E2E only

# Coverage  (threshold 85%)
bash scripts/coverage.sh

# CI script tests (all run in the Lint job; no compiler, no network, ~1s each)
bash scripts/tests/ci-cache-check.test.sh          # cache health check (also runs after every CI build)
bash scripts/tests/ci-install-linux-deps.test.sh   # Linux apt install + mirror failover
bash scripts/tests/check-nonascii-literals.test.sh # no raw/escaped non-ASCII in string literals (fromUTF8/CharPointer_UTF8 exempt)
bash scripts/tests/utf8-literal-check.test.sh      # non-ASCII \x escape wrapping (also runs directly in the Lint job)
bash scripts/check-file-sizes.sh            # 1000-line cap, strict ratchet baseline (--list / --update)
bash scripts/check-function-sizes.sh        # 200-line-per-function cap, strict ratchet baseline (--list / --update)
bash scripts/check-docs.sh                  # docs integrity: naming/links/refs/map, ratcheted naming only (--list / --update)

# Reproduce CI locally (lint + build every CMake target CI builds + full test suite; prints the
# built app bundle path on success). Single source of truth for "what CI will check" — also what
# the pre-push hook runs.
bash scripts/ci-local.sh          # add --open to launch the built app on macOS afterward

# Git hooks  (run once per clone — NOT auto-installed)
bash scripts/install-hooks.sh   # pre-commit: clang-format lint;  pre-push: scripts/ci-local.sh
# clang-format is pinned (.clang-format-version) — match CI locally:
pip install "clang-format==$(cat .clang-format-version)"
```

See [`docs/development/testing.md`](docs/development/testing.md) for the full build/test reference,
and [`docs/README.md`](docs/README.md) for the map of everything else under `docs/`.

## Planning Rules

Every implementation plan **must** include:

1. A **Tests** section — list new test cases, the test file, and what each verifies.
2. A **Docs Updates** section — list which docs (`docs/development/testing.md`, `CLAUDE.md`, etc.) need updating.
3. A **Structure** check — no file over 1,000 lines after the change; a change that would grow a file listed in `scripts/file-size-baseline.txt` moves the new code into a new per-concern unit instead.

## Code structure

- Every file is capped at 1,000 lines, enforced by `scripts/check-file-sizes.sh` (ratchet baseline `scripts/file-size-baseline.txt`) — mechanism in [`docs/development/file-size-guard.md`](docs/development/file-size-guard.md). `--update` never raises an entry; `--allow-growth` is a reviewed exception.
- A class that outgrows one file gets its own directory named after the class, never flat siblings dropped next to dozens of others: `<Class>/<Class>.h` + `<Class><Concern>.cpp` units (never `_Part1`) + shared private helpers in `<Class>Internal.h` — e.g. `Source/UI/Graph/GraphEditor/`, `Source/MainComponent/`. Tests mirror it: `Tests/<Area>/<Class>/<Class><Topic>Tests.cpp` with shared fixtures in `<Class>TestFixture.h`/`<Class>TestHelpers.h`. Each unit opens with a comment naming its concern. A comment goes where the person who could get it wrong will be looking. The shared header carries the declarations, the class-level comment, the section banners, and — per member — only the constraints a **caller** can violate from outside, one line each: nullability, thread affinity ("message thread only"), call-ordering preconditions, units, ownership. The **maintainer**-facing rationale — why it is implemented this way, the threading or ordering argument, the edge case that forced the design, the invariants the body must preserve — lives as a doc comment beside the out-of-line definition in the owning unit, because only a comment sitting inside the diff hunk of the edit that invalidates it reliably stays true, and a header comment recompiles every including translation unit. A one-line forwarder into a collaborator is defined out-of-line too rather than inline in the header (`PianoRollComponent.h`, `GraphEditor.h`). Enforced by `scripts/check-header-comments.sh` (ratchet baseline `scripts/header-comment-baseline.txt`) — mechanism in [`docs/development/header-comment-guard.md`](docs/development/header-comment-guard.md).
- A directory past roughly 30 files gets split by area too. `Source/UI/` holds only area directories (`Graph/`, `Timeline/`, `PianoRoll/`, `Library/`, `Macros/`, `ModuleViews/`, `Settings/`, `Assistant/`, `Chrome/`, `Layout/`, `Theme/`, `MidiRemote/`) with class directories nested inside them — a new UI file goes into its area, never back into `Source/UI/` itself. Include moved-or-shared headers Source-rooted (`"UI/Layout/LayoutUtil.h"`), so a file's depth never matters.
- Docs: one topic per doc, split at section boundaries; keep the Docs map (`docs/README.md`) current.
- Functions do one thing — extract when a function needs section comments or exceeds roughly a screen (~60–80 lines); a new function must never be 200+ lines, enforced by `scripts/check-function-sizes.sh` (ratchet baseline `scripts/function-size-baseline.txt`) — mechanism in [`docs/development/function-size-guard.md`](docs/development/function-size-guard.md). Prefer extracting a real collaborator class over a per-concern unit when the concern has its own state.

## Critical invariants (break these and you ship bugs)

Three rules stated in full because they're cheap to follow and catastrophic to miss:

- **Bypass/mute contract** — in every signal-processing `processBlock`, use **two separate branches**: `isBypassed()` → dry pass-through (return early WITHOUT touching audio channels; clear only CV channels ≥2 so mod CV doesn't leak as audio); `isMuted()` → `buffer.clear()` then return. Never `if (isBypassed() || isMuted()) buffer.clear()` — that mutes on bypass. **Exception:** modules with no dry audio path (pure sources like Oscillator / Poly MIDI; audio-in/CV-out taps like Envelope Follower / Comparator) clear on bypass, still as two branches. → [`docs/architecture/module-base.md#bypassmute-contract`](docs/architecture/module-base.md#bypassmute-contract)
- **Never relax `validatePatch` to raise the AI pass rate** — it is the security boundary for untrusted model output. Fix validity on the *generation* side, most upstream first: schema → bounded retry → narrow repair → prompt; measure with `Tools/AIPatchHarness` first. A node's `"state"` object (`ModuleBase::setExtraState`) is applied on the **trusted path only** — honouring it for provider output makes a patch suggestion an arbitrary file read. → [`docs/ai/patch-safety.md`](docs/ai/patch-safety.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not skipping checks** — the untrusted path rescales in-`[0,1]` values against wider ranges (a heuristic for models), which corrupts app-authored values like a 0.5 Hz LFO rate. Replaying our own `graphToJSON` output applies trusted; if it came off disk, run `validatePatch(..., trusted=false)` as a separate gate first (`SnippetManager::insertSnippet` / `ProjectBundle::load` are the reference pairing). → [`docs/layout/snippets-clipboard.md`](docs/layout/snippets-clipboard.md)

Everything else lives in the named area `CLAUDE.md` — full rule, mechanism, and doc links — auto-loaded when you read a file under that directory. **Open the area file before editing cross-area code**, since it does not load until something under it does. A new invariant goes in the area file (+ its doc); the map below only changes when a new area or topic appears.

- `Source/CLAUDE.md` — audio thread, host modes, `EpochExchange`, MIDI Remote threading, timeline ownership, solo/sends, unsaved-changes guard, autosave, ASCII literals, NaN/Inf scrub
- `Source/Modules/CLAUDE.md` — stereo/Dual I/O legs, fixed channel counts, Wavetable warp aliasing, gain staging
- `Source/Timeline/CLAUDE.md` — audio-clip streaming, automation-lane binding
- `Source/AI/CLAUDE.md` — `applyJSONToGraph` auto-connect, patch-format reserved fields, conversation-history entitlement, refresh-token rotation, AI model discovery ordering
- `Source/UI/CLAUDE.md`, `Source/Plugin/CLAUDE.md` — repaint/animation budget, cable identity, theming, plugin editor look-and-feel, logging limits, `GraphEditor` collaborator ownership, mixer-column/parameter unbinding, MIDI Learn registry
- `.github/CLAUDE.md` — CI cache correctness, PR title convention, required status checks

## Docs map

The map of every doc under `docs/` (one line each, grouped by area) lives in [`docs/README.md`](docs/README.md). Read a doc before touching its area; a new doc gets a line there, not here.
