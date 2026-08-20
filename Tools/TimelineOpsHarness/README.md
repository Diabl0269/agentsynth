# TimelineOpsHarness

Measures how a `timelineOps` envelope (TL8-4/TL8-5) fares against `TimelineOps::validate` — and, for
the one fixture pinning the two-door model, how a patch carrying a smuggled `"timeline"` key fares
against `AIStateMapper::validatePatch` — across a fixed set of **recorded fixtures**.

Unlike `Tools/AIPatchHarness` and `Tools/AIEvalHarness`, this is not a live-model measurement: a
timeline-ops envelope's validity is a deterministic function of the envelope and a fixed graph, so
there is no model to replay prompts against. What is replayed instead is a small set of fixture
JSON files under `Fixtures/`, each pinning an envelope (or patch) and the outcome it must produce.
`Tests/TimelineOpsFixtureTests.cpp` asserts the exact same fixtures as fast gtest cases and is what
CI actually gates on; this tool exists to print the same kind of per-case table and summary rate its
siblings do, so a change to `TimelineOps`/`TimelineValidator`/`MidiClipFile` can be eyeballed against
every fixture at once.

## Running

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target TimelineOpsHarness
./build/Tools/TimelineOpsHarness/TimelineOpsHarness
```

| Flag | Default | Meaning |
| :--- | :--- | :--- |
| `--fixtures` | this tool's own `Fixtures/` directory | Directory of `*.json` fixture files to replay. |
| `--json` | *(none)* | Write per-fixture records to this file for later analysis. |

## Fixture format

```json
{
  "name": "out-of-range-lane-value",
  "description": "...",
  "kind": "timelineOps",
  "expectedValid": false,
  "expectedMessageContains": "outside the range",
  "expectedPreviewContains": "",
  "payload": { "timelineOps": [ ... ] }
}
```

- `kind` is `"timelineOps"` (payload is an envelope, checked via `TimelineOps::validate`) or
  `"patchSmuggle"` (payload is a patch, checked via `AIStateMapper::validatePatch(trusted=false)` —
  used by the one fixture pinning that a timelineOps-shaped payload placed under a patch's reserved
  `"timeline"` key is refused, never inspected for a legitimate-looking shape inside it).
- `expectedValid` is the pass/fail this fixture must produce.
- `expectedMessageContains` (rejections) is a substring the rejection message must contain — this
  harness has no per-timeline-op error enum to match against (unlike `PatchValidationError`/
  `TimelineValidationError`), so the message text is the "error name" being pinned.
- `expectedPreviewContains` (acceptances) is a substring the deterministic `previewText` must
  contain.
- `expectedErrorName`, only on `patchSmuggle` fixtures, is the `PatchValidationError` name (via
  `patchValidationErrorName`) the rejection must carry.

The `02-valid-place-midi-clip.json` and `06-smpte-midi-blob.json` fixtures embed real base64
Standard MIDI File bytes, generated once with `MidiClipFile::exportClip` (PPQ case) and a
hand-built `juce::MidiFile` (SMPTE case) — see the TL8-5 PR description for the generator.

## Reading the output

Every row is expected-vs-actual: `valid`/`invalid` from the fixture against what
`TimelineOps::validate`/`validatePatch` actually returned, plus whether the rejection message (or
patch error name) matched what the fixture pins. The summary rate is the fraction of fixtures whose
actual outcome matched every expectation the fixture makes — with a fixed, deterministic input set
this should always read 100%; anything less means `TimelineOps`, `TimelineValidator` or
`MidiClipFile` moved out from under one of these fixtures, which `Tests/TimelineOpsFixtureTests.cpp`
would also catch (that file, not this histogram, is what CI gates on).
