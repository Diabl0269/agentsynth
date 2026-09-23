# Patch Format

The JSON dialect the AI and the app exchange to describe a synthesizer patch. `graphToJSON` writes
it, `applyJSONToGraph` reads it, [`validatePatch`](patch-safety.md) gates it when it came from a
model, and presets, snippets and undo snapshots all persist it.

## Shape

```json
{
  "nodes": [
    { "id": 1, "type": "Oscillator", "params": { "waveform": "Saw", "octave": -1 } },
    { "id": 2, "type": "Filter", "params": { "cutoff": 800.0, "resonance": 0.4 } },
    { "id": 3, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 1, "srcPort": 0, "dst": 2, "dstPort": 0 },
    { "src": 2, "srcPort": 0, "dst": 3, "dstPort": 0 }
  ]
}
```

- **`nodes`** — the modules.
  - `id`: a unique integer identifier, meaningful only within one document.
  - `type`: the module's factory type name (`"Oscillator"`, `"Filter"`, `"ADSR"`).
  - `params`: optional key/value pairs for the module's parameters.
- **`connections`** — the signal flow: `src`/`srcPort` to `dst`/`dstPort`, ports by index.

Parameter ids are the exact lowercase `paramID` strings from `getModuleSchema()` (`waveform`, not
`Waveform`), and values are raw and unnormalized within each parameter's declared range — `cutoff`
is Hz (20 to 20000), not a 0 to 1 fraction. Oscillator has no `frequency` parameter: pitch comes
from the incoming MIDI note (or A4/440 Hz if none is connected) offset by `octave`, `coarse` and
`fine`.

## Per-node `state`

An optional object of **non-parameter** module state, round-tripped through
`ModuleBase::getExtraState()` / `setExtraState()`. `graphToJSON` writes it for any module that has
some — `Sampler` stores `{"sampleFile": "<absolute path>"}`.

**Trusted-path only.** `applyJSONToGraph` ignores `state` unless `trusted == true`: it is honoured
for the app's own undo/redo snapshots and presets, and dropped for anything a provider produced. A
module is free to read this as a filename, so accepting it from model output turns a patch
suggestion into an arbitrary file read. `Sampler` nodes stay fully authorable by the model; the
node is simply created with no sample loaded.

Nothing in `getPatchSchema()` advertises `state`, so a constrained decoder is never invited to emit
one.

A `Hosted Plugin` node's `state` also carries the optional key `"cardLayout"` (the per-instance
knob layout, see [`../control/plugin-card-layout.md`](../control/plugin-card-layout.md)): trusted-only
like the rest of it, and doubly unreachable from a provider because the type itself is
non-authorable.

## Per-node `uuid`

A stable per-node identity, generated lazily by `graphToJSON` and persisted back into the graph
node's `properties`, so repeated saves of an unchanged node emit the same string. This — not the
integer `id`, which merge-mode apply renumbers — is what long-lived references key on.

**Trusted-path only**, like `state`: `applyJSONToGraph` adopts an incoming `uuid` when
`trusted == true` and ignores it otherwise, so a provider cannot hand two nodes the same identity
or claim one that something else already points at. Untrusted nodes get a fresh uuid on the next
`graphToJSON`.

That uuid is also what makes undo and redo node-preserving.
`AIStateMapper::applySnapshotPreservingNodes` — the third apply path, used **only** by
`AppUndoManager::SnapshotAction` — diffs one of the app's own `graphToJSON` snapshots against the
live graph and keeps every node whose uuid still matches, instead of replaying it through
`applyJSONToGraph` and re-creating everything. It refuses anything whose identity is ambiguous,
leaving the graph untouched, and the caller then falls back to
`applyJSONToGraph(..., clearExisting=true, trusted=true)`. `applyJSONToGraph` itself is unchanged by
that path; presets, snippets and AI apply all keep their own semantics. See
[`architecture/module-base.md`](../architecture/module-base.md#appundomanager).

## Per-node `displayName`

A node may carry a `"displayName"` string: the user's custom card title, set by double-clicking the
card header (see [`layout/module-card.md`](../layout/module-card.md#custom-card-titles)). It is emitted only when set, so an un-renamed node's
JSON is byte-identical to a document from before the field existed.

Unlike `uuid` and `state`, it is applied on **both** the trusted and untrusted paths, because it is
**display-only**. It is never consulted for module-type resolution (that is `"type"`), for node
identity (`"uuid"`), for parameter values, or for anything else semantic, so the worst an untrusted
patch can do with it is mislabel a card, which the user can see and rename. It IS length-capped at
`synth::kMaxModuleDisplayNameChars` (64) on every path that accepts one — including the one the user
types into, so a title typed in the app and a title loaded from a file can never disagree about what
is storable. The cap is what stops a hostile patch stuffing a megabyte of text into a title and
wedging the canvas paint. Blank or whitespace-only means "no custom title".

**Why a separate field rather than the processor's name.** `ModuleBase::getName()` is the
auto-numbered `"Chorus 2"` that `AudioEngine::updateModuleNames()` recomputes wholesale on every
graph change, so a custom title stored there is clobbered by the next node added.

Pinned by `ModuleTitleRoundTripsThroughGraphJSON` and
`UntrustedPatchDisplayNameIsCappedAndDisplayOnly`.

## Reserved keys and forward compatibility

Parameter values are a flat scalar map, permanently. Time-varying data lives under the reserved
`"timeline"` root key, never in a polymorphic param value. This gives every older and newer build
something clear to do with it: old builds preserve it, new builds honour it, neither corrupts the
other's data.

`"timeline"`, `"macros"` and `"midiRemote"` are reserved root keys and are **refused, not ignored**,
on the untrusted path (`PatchValidationError::TimelineNotAllowed` for the first). The validator lets
unknown keys through, so a future build that starts honouring one of them would otherwise silently
begin executing provider-authored data against patches accepted today. Refusing means that door can
only be opened by a commit that deliberately deletes the check. The trusted path — preset and undo
replay — accepts them.

The `"timeline"` refusal is permanent even though the AI *can* author timeline data: that data
arrives through a separate, separately-gated door — see
[`validateTimeline`](timeline-safety.md#the-two-door-model).

`graphToJSON` writes a root `"schemaVersion": 1` (`AIStateMapper::kSchemaVersion`). **Readers treat
an absent version as 1 and gate no behaviour on it** — the field exists so a genuinely breaking
format change can be detected later. Adding a property is always additive; never bump the version
for one.

None of `schemaVersion`, `uuid` or `"timeline"` appears in `getPatchSchema()` — every property there
is an invitation to emit it, and all three belong to the app. Pinned by
`AIStateMapperTest.SchemaOmitsReservedFields`.

Unknown top-level keys — anything besides `nodes`, `connections`, `modulations`, `mode`, `remove`,
`removeModulations`, `schemaVersion` — are preserved across a save/load round-trip by
`PatchDocument` (`Source/PatchDocument.h`). These preserved keys are inert: never interpreted, never
validated, never fed into any apply path, and present only on the user preset save/load path. Undo,
redo, snippets and AI apply all replay in-session graph state and must not resurrect file-level keys
into that flow.

## Type strings round-trip

`graphToJSON` writes `getFactoryTypeName(processor)` and `applyJSONToGraph` feeds that string
straight back to `createModule`, so a mismatch is silent data loss on every save/load **and** every
structural undo, which replays the same JSON. `AIStateMapperTest.FactoryTypeNamesRoundTrip` walks
every factory key and fails on any mismatch; `AIStateMapperTest.ParamIdsGolden` does the same for
`paramID`s, which presets, undo snapshots and AI patches all address parameters by.
