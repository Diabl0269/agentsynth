# Timeline Safety

`synth::validateTimeline` (`Source/Timeline/TimelineValidator.h/.cpp`) is the untrusted gate for
AI- and tool-supplied **timeline** JSON — tracks, clips, notes and automation lanes in the dialect
`TimelineDoc::toVar` writes. It is a separate function from
[`validatePatch`](patch-safety.md), with its own error enum (`TimelineValidationError`) and its own
name table (`timelineValidationErrorName`, the same idiom as `patchValidationErrorName`).

The operations that go through it are [timeline ops](timeline-ops.md); what the model is allowed to
see of the arrangement is [arrangement context](arrangement-context.md).

## The two-door model

There are two ways timeline data could reach the app, and exactly one of them is open:

| Door | Status | Rule |
| --- | --- | --- |
| The **patch grammar** — a `"timeline"` key inside a patch suggestion | **Closed, permanently** | `validatePatch(trusted=false)` refuses it (`TimelineNotAllowed`, see [reserved keys](patch-format.md#reserved-keys-and-forward-compatibility)). A patch is applied to the graph; a timeline is not part of a graph. |
| The **tools** — the discrete app-side timeline operations (add-track, place-clips, write-lane, place-midi-clip) | **Open, guarded** | Each payload goes through `validateTimeline` before it touches `TimelineDoc`. |

The open door is a separate guarded entrance, *not* a relaxation of the patch path. Pinned by
`TimelineValidatorTest.PatchGrammarStillRefusesTimelineData`, which takes a document this validator
accepts, smuggles it into a patch, and asserts `validatePatch` still refuses it.

## Contract

- **Validates strictly, mutates nothing** — not the doc, not the graph, not the input `var`.
- The caller applies via `TimelineDoc::fromVar` (all-or-nothing) **only** after this passes.
- **A pass means the apply cannot fail.** The last thing the function does is load the document into
  a throwaway `TimelineDoc` to prove it. A var that satisfies every named check and is still refused
  by the loader yields `InternalError` — the validator and `fromVar` have drifted, and the caller
  applies nothing either way. The one reachable case is a malformed `nextTrackId`, `nextClipId`,
  `nextLaneId`, `nextNoteId` or `nextMarkerId` counter, which is the document's own bookkeeping and
  which a tool payload has no reason to carry.
- **Rejects where the trusted paths repair.** `fromVar` clamps a breakpoint's tension into `[-1, 1]`
  and its value into the lane's range snapshot, and repairs a broken sort order. None of that
  happens for untrusted data: a value that would have to be corrected is a value the sender did not
  mean, so it is refused with a message saying which one and why.
- Only the **first** problem is reported, like `validatePatch` — fixing one class can unmask the
  next. Every message names the offending track, clip, note or lane so it can be handed back as a
  correction rather than a complaint.

## The checks

1. **Structural** — the root is an object of the `TimelineDoc` dialect; `version` present, integer,
   and no newer than `TimelineDoc::kFormatVersion`; `tracks` and `markers` (if present) arrays; ids
   present, positive and unique per kind; one lane per `(nodeUuid, paramId)` doc-wide.
   **Unknown top-level keys are refused**, where `PatchDocument` deliberately *preserves* the ones it
   does not understand. That asymmetry is the point: forward compatibility is a property a document
   format needs and an untrusted payload does not, and an ignored key is exactly how a later build
   starts honouring a field today's gate never inspected. The allowlist is `version`, `tracks`,
   `markers` and the five next-id counters — and a key earns its place there **only** by having a
   per-item check written for it (see check 9). Adding one without that check defeats this whole
   paragraph.
2. **Caps** — the per-container limits are `TimelineDoc`'s own constants, referenced and never
   duplicated: `kMaxTracks` (256), `kMaxClipsPerTrack` (4096), `kMaxNotesPerClip` (16384),
   `kMaxLanesPerTrack` (512), `kMaxBreakpointsPerLane` (16384), `kMaxMarkers` (1024),
   `kMaxMarkerTextLength` (128). Two more exist only on this path, because they bound the whole
   payload rather than one container: `kMaxTotalNotesUntrusted` (65536 notes summed across every
   clip) and `kMaxPpqUntrusted` (100000.0, the largest beat position or length in beats — roughly 14
   hours at 120 BPM).
3. **Beats** — every `startBeat`, `lengthBeats`, fade and breakpoint beat must be finite, `>= 0` and
   `<= kMaxPpqUntrusted`; lengths must be `> 0` (`BeatOutOfBounds`).
4. **Notes** — pitch `0..127`, velocity `1..127`, channel `1..16`, **rejected** rather than clamped
   (`NoteOutOfRange`).
5. **Lanes** — every `(nodeUuid, paramId)` must resolve against the **live graph**: a node carrying
   that `uuid` property, holding a `RangedAudioParameter` with that `paramID`. Unresolvable is
   `UnresolvableBinding` — an orphaned binding is a state the app *recovers from* when a node
   disappears under an existing lane, not one untrusted input may author from nothing. The same rule
   applies to a track's non-empty `bindingUuid`; empty is legal, meaning an unbound track. Every
   breakpoint value must sit inside the **resolved parameter's real range**, *not* the lane's own
   `RangeSnapshot`, which is data the sender wrote and therefore cannot be the authority on what the
   parameter accepts. `tension` must be in `[-1, 1]`, `curve` in `0..2`.
6. **Assets** — any clip with a non-empty `assetRef` is refused (`AssetNotAllowed`). Stricter than
   `TimelineDoc::isValidAssetRef`, which only stops a path escaping the bundle: untrusted input may
   not name an asset **at all**, however well-formed.
7. **Record modes** — a lane may only ask for `Read` (1) or `Off` (0). `Touch`, `Latch` and `Write`
   arm the lane to capture the user's own gestures, which is the user's decision
   (`RecordModeNotAllowed`).
8. **Track kinds** — `Midi` (0), `Audio` (1) and `Automation` (2); reserved kinds 3 to 15 are
   refused (`ReservedKindNotAllowed`). An audio *track* is a legal shape; what makes it unauthorable
   in practice is check 6, since an audio track with no asset-bearing clip is just an empty row.
9. **Markers** — the `"markers"` array (`TimelineDoc::Marker`: `id`, `beat`, `text`, `colourArgb`)
   is allow-listed at the top level *because* it is checked here, field by field. A marker carries
   no binding, no asset and nothing executable — it is a beat, a label and a colour — so the rules
   are entirely about bounds and shape:
   - `id` required, positive, unique across markers (`MalformedRoot`), like a note's;
   - `beat` finite, `>= 0` and `<= kMaxPpqUntrusted` (`BeatOutOfBounds`), the same bound every other
     beat in the document gets;
   - `text` a string of at most `kMaxMarkerTextLength` (128) characters, **rejected not truncated**
     (`MarkerTextTooLong`) — the same rule note pitch 128 gets: a label that would have to be
     shortened is not the label the sender meant. An empty label is legal, an unlabelled flag;
   - `colourArgb` an integer in the full 32-bit range `0 .. 4294967295` — inert display data, so
     every value is legal and only the type and the range are checked;
   - array size at most `kMaxMarkers` (`TooManyMarkers`);
   - **unknown keys inside a marker object are refused.** Markers are the one container checked with
     a *closed key set*; tracks, clips and notes reject unknown keys at the top level only. The
     reasoning is check 1's, one level down.

## Trusted-only forever

Audio assets and the clips that reference them, plugin state blobs, a node's `"state"` object
(`ModuleBase::setExtraState`), and lane record arming. Each is a capability that reaches outside the
document — the filesystem, opaque third-party state, or the user's own playing — and none of them
has an untrusted form. Widening `validateTimeline` to admit one is the same class of mistake as
relaxing `validatePatch` to raise the AI pass rate.

Tests: `Tests/Timeline/TimelineValidatorTests.cpp` — table-driven, one deliberate defect per case,
every `TimelineValidationError` value covered.

## The agentic security model

The single statement the sections above and [timeline ops](timeline-ops.md) implement. When
extending the AI's reach into the timeline, this table is the contract to preserve: every row exists
because the mechanism next to it enforces it, not because a prompt asks nicely.

**What AI output may author:**

| Surface | Mechanism | Bound |
|---|---|---|
| MIDI notes | `placeClips` note lists, or `.mid` blobs via `placeMidiClip` | note caps, pitch/velocity/channel ranges REJECTED not clamped; blob at most 256 KiB, PPQ-only SMF parsed by `MidiClipFile`, a format that structurally cannot carry a path, a plugin id, or code |
| Automation lanes | `writeLane` | values validated against the **live** parameter's range intersected with the lane snapshot; `(nodeUuid, paramId)` must resolve against the live graph, so untrusted input can never author an orphan |
| Doc-only tracks | `addTrack`, kinds `midi` and `automation` only | unbound: wiring a Track In or Track Audio node stays a user gesture |

**What AI output may never touch, and why:**

| Never | Why | Enforced by |
|---|---|---|
| Asset references and file paths | an `assetRef` is a file **read**; honouring one from a model turns a chat reply into arbitrary file access | `AssetNotAllowed` in `validateTimeline`; unknown-field rejection makes `assetRef` unreachable by grammar in ops |
| Plugin identifiers and state blobs | a plugin blob is a code-execution surface, not a parameter | node `state` is trusted-path-only (`applyExtraStateToProcessor`); internal-only module types rejected untrusted (`InternalModuleNotAllowed`), which is where hosted-plugin types sit |
| Record arming and record modes | untrusted input must not start capturing the user's audio | `RecordModeNotAllowed`; ops carry no such field by grammar |
| The patch grammar's `timeline` key | timeline data rides its own validated door, never the patch schema — every property in `getPatchSchema` invites the model to emit it on every request | `TimelineNotAllowed`, permanent; pinned in both directions by tests and a harness fixture |

Read-path symmetry: what the model *sees* follows the same rule — arrangement summaries carry bare
file names only, never paths or directories.
