# Timeline Operations

`synth::TimelineOps` (`Source/Timeline/TimelineOps.h/.cpp`) is the **write** half of the timeline
seam: discrete, validated, previewable operations a model may ask for, applied to `TimelineDoc` as
one undo step. [Timeline safety](timeline-safety.md) is the gate they go through;
[arrangement context](arrangement-context.md) is the read-only context that lets a model know what
to ask for in the first place.

## The envelope

```json
{ "timelineOps": [
  { "op": "addTrack",   "kind": "midi", "name": "Bass" },
  { "op": "placeClips", "track": "Bass",
    "clips": [ { "startBeat": 0, "lengthBeats": 4, "name": "A",
                 "notes": [ { "startBeat": 0, "lengthBeats": 1,
                              "pitch": 36, "velocity": 100, "channel": 1 } ] } ] },
  { "op": "writeLane",  "nodeUuid": "...", "paramId": "cutoff",
    "points": [ { "beat": 0, "value": 800, "tension": 0, "curve": 1 } ] },
  { "op": "placeMidiClip", "track": "Bass", "startBeat": 0,
    "midBase64": "<base64-encoded Standard MIDI File>" }
] }
```

This is the **client** half of the capability. The private backend repo owns the **server** half —
the capability schema the model emits against, the counterpart of `getPatchSchema` for patches.
Nothing here trusts that schema: an envelope is re-validated locally whatever produced it.

| Op | What it does | What it deliberately does not do |
| --- | --- | --- |
| `addTrack` | Creates the **doc** track. `kind` is `"midi"` or `"automation"`. | No graph node, no Track In wiring — binding a track to a module is a routing decision about the user's own patch, so it stays a user gesture. The new track is unbound and the preview says so. `"audio"` is not offered: an audio track needs an asset, and assets are trusted-only. |
| `placeClips` | Places clips, and their clip-relative notes, on a MIDI track targeted by exact name or `{"index": N}`. | A name matching no track, or more than one, rejects the whole batch rather than guessing. |
| `writeLane` | Find-or-creates the lane for `(nodeUuid, paramId)` on the document's Automation track, creating that track if there is none, exactly as `MainComponent::automateParameter` does, then REPLACES every point in the written span (min to max beat of the payload, inclusive) in one `editBreakpoints` call. | Never sets a record mode; never widens a range. |
| `placeMidiClip` | Decodes `midBase64` and parses it with `MidiClipFile::importFromStream`, placing one clip per non-empty imported SMF track on the target MIDI track at `startBeat`. Clip length is `ceil` of its last note's end, floored at 1 beat, reusing `MidiClipFile::importIntoTrack`. | No paths, no plugin ids, no code — a `.mid` blob can only ever decode to notes, which is why this is the one op that accepts an opaque binary payload at all. |

## The local path

The local (Ollama) path can author this envelope too, behind
`AIIntegrationService::setTimelineToolsEnabled`, which `MainComponent` sets on unconditionally, and
additionally gated on a timeline context being installed (`hasTimelineContext()`). While active,
three things change and nothing else:

- the system prompt gains a `TIMELINE & AUTOMATION OPERATIONS` section teaching the four ops,
  swapped into the existing history **in place**, so a mid-conversation toggle never clears the chat
  and off means byte-identical to the pre-timeline prompt;
- the structured-output `format` becomes `AIStateMapper::getPatchSchemaWithTimelineOps()` —
  `getPatchSchema()` plus an OPTIONAL `timelineOps` array whose item schema is deliberately
  permissive (one object shape, only `"op"` required). It is a grammar that lets the model express
  the ops, not a validator: `TimelineOps::validate` remains the gate, and the
  [reserved-fields rule](patch-format.md#reserved-keys-and-forward-compatibility) is untouched;
- the outgoing request grows an `## Automation targets` section
  (`buildAutomationTargetsSection()`): one line per uuid-bearing node listing its float parameter
  ids and RAW ranges, the addressing channel `writeLane` needs. Node uuids appear there **on
  purpose**, despite [arrangement context](arrangement-context.md)'s no-uuid rule: that rule keeps
  identifiers out of the human-readable summary, this section is what makes the grammar usable at
  all, uuids are random per-node identity and never a path or plugin id, and `validate()` only
  accepts pairs that resolve against the live graph anyway. Bounded to roughly 2000 characters,
  whole lines, with a truncation marker.

Extraction, validation, preview and Apply are unchanged and provider-agnostic either way: they act
on what a response actually carries, and the user's Apply click stays the write gate. Pinned by
`AIIntegrationServiceTest.TimelineToolsToggle*` and `AutomationTargetsSection*`.

## `placeMidiClip` and the `.mid` blob

Every other op in this grammar is closed field by field. `placeMidiClip` is the one exception that
accepts an opaque, base64-encoded blob, and it is safe to accept specifically *because*
`MidiClipFile::importFromStream` (`Source/Timeline/MidiClipFile.h`) is the safest surface available:
a Standard MIDI File can only ever decode to notes — pitch, velocity, channel and timing. There is
no way to encode a file path, a plugin identifier or code inside one, unlike almost any other blob a
model could hand back. `placeMidiClip` reuses that exact importer, the same strict parser a user's
own MIDI-file import goes through, rather than a looser variant for AI input.

Bounds, in the order they are checked:

- **`midBase64` size**, against `TimelineOps::kMaxMidBlobBytes` (262144), checked on the
  STILL-ENCODED string *before* any decode is attempted, so an oversized blob is rejected as cheaply
  as any other length check rather than by allocating a decode buffer for it first.
- **Decodability** — invalid base64 is rejected outright.
- **`MidiClipFile::importFromStream`'s own checks** — not a readable SMF, SMPTE time format (PPQ
  only), or any one imported track's note count over `TimelineDoc::kMaxNotesPerClip` all reject the
  op. An import failure never means "import what parsed and drop the rest".
- **An empty result** — a blob with no notes in it is refused; there is nothing to place.
- **The batch's own note and clip caps** — every note the blob contains counts toward
  `kMaxTotalNotesUntrusted` exactly like a `placeClips` note does, and the target track's clip count
  is checked against `TimelineDoc::kMaxClipsPerTrack` before anything is placed.

Any failure at any of those steps rejects the WHOLE batch, the same all-or-nothing contract every
other op has.

## Sibling, never nested

`timelineOps` sits **beside** a patch, never inside one. `validatePatch(trusted=false)` refuses a
`"timeline"` key in patch JSON and always will (see
[the two-door model](timeline-safety.md#the-two-door-model)). `"timelineOps"` is a different key, so
a single structured response may legitimately carry a patch and an ops envelope on the same object,
and each is validated and applied by its own gate with its own Apply button. Pinned by
`TimelineOpsTest.PatchGrammarStillClosed`: the document dialect smuggled in under `"timeline"` is
refused, while the same intent as a sibling `timelineOps` key is accepted by both halves.

## Trust posture

`validate()`, then preview, then the user clicks Apply, then `apply()`. Nothing is applied because a
model asked for it; a person agrees to it first, having read a summary of what it does. This is
identical to the patch card's posture.

- `validate()` mutates nothing and returns `previewText`, a deterministic sentence:
  `Adds midi track "Bass" (unbound - bind it in the timeline panel); places 1 clip (8 notes) at 0-4
  on "Bass"; writes 12 points to Filter cutoff over beats 0-11`. A bound module is named by its
  **display name**, never its uuid, on the write path as well as the read path.
- The per-op checks are [`validateTimeline`](timeline-safety.md)'s, **reused rather than
  re-stated**: the same caps (`TimelineDoc::kMax*`, `kMaxTotalNotesUntrusted`, `kMaxPpqUntrusted`),
  the same bounds, and the same rule that untrusted input is **rejected where a trusted path would
  clamp** — pitch 200 is refused, not rewritten to 127; a breakpoint outside the **live**
  parameter's range is refused, not pulled inside it. Two more caps bound the batch itself:
  `kMaxOps` (64) and `kMaxNameChars` (128); a third, `kMaxMidBlobBytes` (262144), bounds only
  `placeMidiClip`'s `midBase64`.
- **Capabilities are absent from the grammar, not refused field by field.** An op has no
  `assetRef`, no `recordMode`, no `bindingUuid`, and no kind beyond `midi` and `automation`. A
  `.mid` blob is not an exception to this, because it can only ever decode to notes. Unknown fields
  *inside* an op are **rejected**, so a future field cannot be smuggled past a gate that never
  inspected it — the same reasoning as `validateTimeline`'s unknown-top-level-key refusal. Unknown
  keys at the *envelope root* are ignored, because that is where the sibling patch's own `nodes`,
  `connections` and `mode` live.
- **All-or-nothing, and the preview cannot lie.** `validate()` runs the batch against a throwaway
  copy of the document (`fromVar(doc.toVar())`, replaying the app's own serialisation, which is
  trusted by definition) and `apply()` runs the *same code* against the real one, so every op sees
  the effect of the ones before it and no preview can describe an apply that then fails. A rejection
  means the live doc was never touched at all.
- **One undo step.** The whole batch runs inside a single `AppUndoManager::recordTimelineChange`, so
  however many tracks, clips, notes and breakpoints it touches, one `Cmd+Z` reverts all of it — the
  contract `MidiRecorder::stopAndCommit` already relies on for a take's clip plus its notes.

## The chat seam

`AIIntegrationService`, once `setTimelineContext()` has wired the live timeline in, exposes:

- `extractTimelineOps()` — the same extraction `applyPatch` performs, returning the parsed root when
  it carries a `timelineOps` key. It keys on **presence**, not well-formedness, so a malformed
  envelope is surfaced as a visible rejection instead of being silently dropped.
- `previewTimelineOps()` — the validate step.
- `applyTimelineOps()` — routes through a `TimelineOpsApplyCallback` that
  `MainComponent::initialiseCommon` installs. The service holds the doc only as a `const` pointer
  and owns no undo manager for it, so the **host** supplies the write path, which is what puts an
  AI-applied batch on the same shared undo stack as the user's own edits.

`AIChatComponent` renders `TimelineCard` beside `PatchCard`, to the same conventions, with an
"Apply timeline changes" button. A response carrying both gets both cards; a rejected envelope gets
the card with the reason and **no button**, because a suggestion that cannot be applied must still
say why but must not look clickable.

Tests: `Tests/Timeline/TimelineOpsTests.cpp` — per-op apply, one-step undo, all-or-nothing with the
failing op named by index, caps and bounds, the ungrammatical capabilities, pinned preview strings,
the patch-grammar pin, and the service seam end to end.

## Measuring validity

`Tools/TimelineOpsHarness` is the timeline counterpart of `Tools/AIPatchHarness`, adapted to a seam
that has no live model to replay against: a `timelineOps` envelope's validity is a deterministic
function of `TimelineOps::validate` and a fixed graph, so what it measures is a fixed set of
**recorded fixtures** (`Tools/TimelineOpsHarness/Fixtures/*.json`) rather than prompts sent to
Ollama. The scenario set spans a valid three-op envelope; a valid `placeMidiClip` carrying a real
base64 `.mid`; notes over `TimelineDoc::kMaxNotesPerClip`; a `writeLane` value outside the live
parameter's range; an unknown op field; a SMPTE-format `.mid` blob; a `midBase64` over
`kMaxMidBlobBytes`; and the two-door pin — a timelineOps-shaped payload smuggled under a patch's
`"timeline"` key, checked through `AIStateMapper::validatePatch` instead and expected to come back
`TimelineNotAllowed`.

Each fixture pins its expected valid/invalid outcome plus a message — or, for the patch-smuggle
fixture, a `PatchValidationError` name — that the actual result must contain, and the harness prints
a per-fixture expected-versus-actual table and a summary match rate. It is gated behind
`-DENABLE_AI_HARNESS=ON` like its siblings, though, having no live model in the loop, it needs none
to build or run. `Tests/Timeline/TimelineOpsFixtureTests.cpp` asserts the identical fixture files as
fast gtest cases, which is what CI gates on.
