# CLAUDE.md

Desktop app half of a product whose backend/website and marketing assets live in sibling repos
under the workspace root; see [`../CLAUDE.md`](../CLAUDE.md) for the cross-repo map. This file stays
scoped to `agentsynth/` only.

Keep this file **lean** (always loaded): commands, conventions and the area map. Rules live in
per-directory `CLAUDE.md` files (auto-loaded under that directory); mechanism lives in `docs/` (map:
[`docs/README.md`](docs/README.md); a new doc gets a line there, not here). When you change behavior, update the relevant doc, not this
file, as part of the change itself.

## Project

Modular synthesizer (JUCE, C++20): targets `Core`, `AppUI`, `AgentSynth`, `AgentSynthPlugin` (see
[`docs/architecture/architecture.md`](docs/architecture/architecture.md)).

## Commands

```bash
cmake -S . -B build && cmake --build build                  # ENABLE_PLUGIN defaults ON
cmake -S . -B build -DENABLE_TESTS=ON && cmake --build build --target Tests && ./build/Tests/Tests   # ENABLE_TESTS defaults OFF
bash scripts/ci-local.sh                                    # reproduce CI locally (lint + every target + full tests)
bash scripts/install-hooks.sh                               # once per clone, NOT auto-installed
```

Coverage (85% threshold), build flags, the guards, the CI script tests and the pinned clang-format:
[`docs/development/testing.md`](docs/development/testing.md), [`docs/development/local-ci.md`](docs/development/local-ci.md).

## Planning Rules

Every implementation plan **must** include:

1. A **Tests** section: new test cases, the test file, and what each verifies.
2. A **Docs Updates** section: which docs under `docs/` change, and the area `CLAUDE.md` only for a new invariant.
3. A **Structure** check: no file over 1,000 lines after the change; a change that would grow a file in `scripts/file-size-baseline.txt` puts the new code in a new per-concern unit.
4. An **Accessibility** section for any UI change: each new or changed control's keyboard path, focus ring, screen-reader name and tooltip ([docs/development/accessibility.md](docs/development/accessibility.md)).

## Code structure

- Every file is capped at 1,000 lines (`scripts/check-file-sizes.sh`, strict ratchet). → [`file-size-guard.md`](docs/development/file-size-guard.md)
- A class that outgrows one file gets its own directory named after it: `<Class>/<Class>.h` + `<Class><Concern>.cpp` units (never `_Part1`) + shared private helpers in `<Class>Internal.h`. Tests mirror it: `Tests/<Area>/<Class>/<Class><Topic>Tests.cpp`, fixtures in `<Class>TestFixture.h`/`<Class>TestHelpers.h`. Each unit opens with a comment naming its concern.
- Comment placement: the shared header carries declarations, the class comment, section banners and, per member, only what a **caller** can violate (nullability, thread affinity, call ordering, units, ownership), one line each; **maintainer** rationale goes in a doc comment beside the out-of-line definition (one-line forwarders too). Enforced by `scripts/check-header-comments.sh`. → [`header-comment-guard.md`](docs/development/header-comment-guard.md)
- Comments in `Source/` and `Tests/` state behaviour, invariants and reasons, never provenance (ticket ids, dates, review rounds); that is `git log`/`git blame`. Only exception: a test pinning a past bug, as `// Regression test for FRO97: <what broke>` above the test (Tests/ only, no date). Enforced by `scripts/check-comment-provenance.sh`. → [`comment-provenance-guard.md`](docs/development/comment-provenance-guard.md)
- A directory past roughly 30 files is split by area; a new UI file goes into its area directory (list in `Source/UI/CLAUDE.md`). Include moved-or-shared headers Source-rooted (`"UI/Layout/LayoutUtil.h"`). → [`file-size-guard.md`](docs/development/file-size-guard.md)
- Docs: one topic per doc, split at section boundaries; keep the Docs map (`docs/README.md`) current.
- Functions do one thing: extract when one needs section comments or exceeds ~60-80 lines; a new function is never 200+ lines (`scripts/check-function-sizes.sh`). Prefer a real collaborator class when the concern has its own state. → [`function-size-guard.md`](docs/development/function-size-guard.md)

## Critical invariants

- **Bypass/mute contract**: in every `processBlock` (modules override `processModuleBlock`; `ModuleBase::processBlock` is final) use **two separate branches**: `isBypassed()` → dry pass-through, return early WITHOUT touching audio channels (clear only CV channels ≥2); `isMuted()` → `buffer.clear()` and return. Never `if (isBypassed() || isMuted()) buffer.clear()`. **Exception:** modules with no dry audio path (pure sources, audio-in/CV-out taps) clear on bypass, still as two branches. → [`module-base.md#bypassmute-contract`](docs/architecture/module-base.md#bypassmute-contract)
- **Never relax `validatePatch` to raise the AI pass rate**: it is the security boundary for untrusted model output. Fix validity on the *generation* side, most upstream first (schema → bounded retry → narrow repair → prompt), measured with `Tools/AIPatchHarness`. A node's `"state"` object (`ModuleBase::setExtraState`) applies on the **trusted path only**. → [`patch-safety.md`](docs/ai/patch-safety.md)
- **`trusted=true` on `applyJSONToGraph` is about parameter fidelity, not skipping checks**: the untrusted path rescales in-`[0,1]` values against wider ranges. Replaying our own `graphToJSON` output applies trusted; anything that came off disk runs `validatePatch(..., trusted=false)` first (pairing: `SnippetManager::insertSnippet` / `ProjectBundle::load`). → [`snippets-clipboard.md`](docs/layout/snippets-clipboard.md)

Everything else lives in the area `CLAUDE.md`, which loads only once you read under that directory:
**open the area file before editing cross-area code**. A new invariant goes in the area file (+ its
doc); this map changes only when a new area or topic appears.

- `Source/CLAUDE.md`: audio thread, host modes, `EpochExchange`, MIDI Remote threading, timeline ownership, solo/sends, unsaved-changes guard, autosave, ASCII literals, NaN/Inf scrub
- `Source/Modules/CLAUDE.md`: stereo/Dual I/O legs, fixed channel counts, Wavetable warp aliasing, gain staging
- `Source/Timeline/CLAUDE.md`: audio-clip streaming, automation-lane binding
- `Source/AI/CLAUDE.md`: `applyJSONToGraph` auto-connect, patch-format reserved fields, conversation-history entitlement, refresh-token rotation, AI model discovery ordering
- `Source/UI/CLAUDE.md`, `Source/Plugin/CLAUDE.md`: repaint/animation budget, cable identity, theming, plugin editor look-and-feel, logging limits, `GraphEditor` collaborator ownership, mixer-column/parameter unbinding, MIDI Learn registry
- `.github/CLAUDE.md`: CI cache correctness, PR title convention, required status checks

