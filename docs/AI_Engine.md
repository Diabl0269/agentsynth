# Agent Synth AI Engine Documentation

This document provides a comprehensive overview of the AI Engine integrated into Agent Synth, detailing its architecture, components, communication patterns, and key functionalities.

## 1. Overview
The Agent Synth AI Engine serves as an intelligent sound design assistant, enabling users to generate and modify synthesizer patches using natural language commands. Its primary goal is to bridge the gap between intuitive textual instructions and the complex, modular architecture of the Agent Synth synthesizer. This allows for a more accessible and creative sound design workflow.

## 2. Architecture

The AI Engine's architecture is designed for modularity and extensibility, primarily centered around the `AIIntegrationService` which orchestrates interactions between AI models and the core synthesizer.

### Core Components:

-   **`AIProvider`**: An abstract interface defining the contract for any AI backend integration. This allows Agent Synth to support various large language models (LLMs) or AI services (e.g., Ollama, OpenAI) by implementing this interface. It specifies methods for sending prompts, retrieving responses, and managing available models.

-   **`OllamaProvider`**: A concrete implementation of the `AIProvider` interface specifically designed to interact with local Ollama instances. It handles the HTTP communication with the Ollama API, including fetching available models and managing chat completions.

-   **`AIIntegrationService`**: The central orchestrator of the AI Engine. This service manages the overall AI interaction flow. Its responsibilities include:
    *   Maintaining the conversation history with the AI.
    *   Sending user prompts (potentially augmented with current synth context) to the configured `AIProvider`.
    *   Interpreting responses from the `AIProvider`.
    *   Applying AI-generated patch data to the `juce::AudioProcessorGraph`.
    *   Managing the selection and fetching of available AI models.
    *   Notifying listeners of AI-driven changes to the synthesizer state.

-   **`AIStateMapper`**: A utility component responsible for translating between the AI-friendly JSON representation of a synthesizer patch and Agent Synth's internal `juce::AudioProcessorGraph` structure. It handles both `graphToJSON` (for providing context to the AI) and `applyJSONToGraph` (for applying AI suggestions).

### Interaction Flow:

1.  **User Input**: The user provides a natural language prompt via the UI (e.g., "create a warm pad sound with a slow attack").
2.  **Prompt Processing**: The `AIIntegrationService` receives the prompt, adds it to the chat history, and may augment it with the current synthesizer's state (obtained via `AIStateMapper`).
3.  **AI Communication**: The `AIIntegrationService` forwards the processed prompt to the currently selected `AIProvider` (e.g., `OllamaProvider`).
4.  **AI Response**: The `AIProvider` communicates with the external AI model, receives a response, and returns it to the `AIIntegrationService`.
5.  **Response Interpretation**: The `AIIntegrationService` parses the AI's response. If the response contains a JSON patch (identified by a specific format like ````json`), it extracts this data.
6.  **Patch Application**: The extracted JSON patch is then passed to the `AIStateMapper`, which translates it into commands to modify the `juce::AudioProcessorGraph`, effectively updating the synthesizer's patch.
7.  **UI Update**: The UI is updated to reflect the new chat history and the applied synthesizer changes.

## 3. Communication Pattern

The AI communicates with Agent Synth using a simplified JSON schema to describe synthesizer patches. This schema defines nodes (representing modules) and connections between them.

### Example JSON Patch Format:

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

Parameter IDs are the exact lowercase `paramID` strings from `getModuleSchema()` (`waveform`, not
`Waveform`), and values are raw and unnormalized within each parameter's declared range — `cutoff` is
Hz (20–20000), not a 0–1 fraction. Oscillator has no `frequency` parameter: pitch comes from the
incoming MIDI note (or A4/440Hz if none is connected) offset by `octave`/`coarse`/`fine`.

-   **`nodes`**: An array of synthesizer modules.
    -   `id`: A unique integer identifier for the module.
    -   `type`: The string name of the module (e.g., "Oscillator", "Filter", "ADSR").
    -   `params`: An optional object containing key-value pairs for module parameters (e.g., "Frequency", "Cutoff").

    -   `state`: An optional object of **non-parameter** module state, round-tripped through
        `ModuleBase::getExtraState()` / `setExtraState()`. Written by `graphToJSON` for any module
        that has some (today only `Sampler`, which stores `{"sampleFile": "<absolute path>"}`).

        **Trusted-path only.** `applyJSONToGraph` ignores `state` unless `trusted == true` — i.e. it
        is honoured for our own undo/redo snapshots and presets, and dropped for anything a provider
        produced. A module is free to read this as a filename, so accepting it from model output
        would turn a patch suggestion into an arbitrary file read. `Sampler` nodes remain fully
        authorable by the model; the node is simply created with no sample loaded.

        Nothing in `getPatchSchema()` advertises `state`, so a constrained decoder is never invited
        to emit one.

    -   `uuid`: A stable per-node identity, generated lazily by `graphToJSON` and persisted back
        into the graph node's `properties`, so repeated saves of an unchanged node emit the same
        string. This — not the integer `id`, which merge-mode apply renumbers — is what long-lived
        references key on.

        **Trusted-path only**, like `state`: `applyJSONToGraph` adopts an incoming `uuid` when
        `trusted == true` and ignores it otherwise, so a provider cannot hand two nodes the same
        identity or claim one that something else already points at. Untrusted nodes simply get a
        fresh uuid on the next `graphToJSON`.

### Patch format forward-compatibility

Parameter values remain a flat scalar map forever — time-varying data will live under the reserved
`"timeline"` root key, never in a polymorphic param value. This gives every older and newer build
something clear to do with it: old builds preserve it (see `PatchDocument` below), new builds honor
it, neither corrupts the other's data.

`"timeline"` is reserved and **refused, not ignored**, on the untrusted path
(`PatchValidationError::TimelineNotAllowed`). The validator lets unknown keys through, so a future
build that starts honouring timeline data would otherwise silently begin executing provider-authored
automation against patches accepted today. Refusing now means that door can only be opened by a
commit that deliberately deletes the check. The trusted path (preset/undo replay) accepts it. That
refusal is permanent even now that AI *can* author timeline data: it arrives through a separate,
separately-gated door — see [§5c, `validateTimeline`](#5c-untrusted-timeline-data-validatetimeline-tl8-1).

`graphToJSON` writes a root `"schemaVersion": 1` (`AIStateMapper::kSchemaVersion`). **Readers treat
an absent version as 1 and gate no behaviour on it** — the field exists so a genuinely breaking
format change can be detected later. Adding a property is always additive; never bump the version
for one.

Per-node `uuid` is honoured on the trusted path only (`applyJSONToGraph` with `trusted == true`);
untrusted input gets fresh identities regenerated on the next `graphToJSON`. This ensures a model
cannot hand two nodes the same identity or claim one that something else already points at.

That uuid is also what makes undo/redo node-preserving: `AIStateMapper::applySnapshotPreservingNodes`
(the third apply path, used **only** by `AppUndoManager::SnapshotAction`) diffs one of our own
`graphToJSON` snapshots against the live graph and keeps every node whose uuid still matches,
instead of replaying it through `applyJSONToGraph` and re-creating everything. It refuses anything
whose identity is ambiguous — leaving the graph untouched — and the caller then falls back to
`applyJSONToGraph(..., clearExisting=true, trusted=true)`. `applyJSONToGraph` itself is unchanged;
presets, snippets and AI apply all keep their existing semantics. See
[`docs/architecture.md`](architecture.md#appundomanager).

None of `schemaVersion`, `uuid` or `"timeline"` appears in `getPatchSchema()` — every property there
is an invitation to emit it, and all three are ours to write. Pinned by
`AIStateMapperTest.SchemaOmitsReservedFields`.

Unknown top-level keys (anything besides `nodes`, `connections`, `modulations`, `mode`, `remove`,
`removeModulations`, `schemaVersion`) are preserved across a save/load round-trip by `PatchDocument`
(see `Source/PatchDocument.h`). These preserved keys are inert — never interpreted, never validated,
and never fed into any apply path — and live only on the user preset save/load path. Undo/redo,
snippets, and AI apply all replay in-session graph state and must not resurrect file-level keys into
that flow.

Node type strings must round-trip: `graphToJSON` writes `getFactoryTypeName(processor)` and
`applyJSONToGraph` feeds that string straight back to `createModule`, so a mismatch is silent data
loss on every save/load **and** every structural undo (which replays the same JSON).
`ModuleType::PolySequencer` mapped to `"Sequencer"`, which downgraded every saved Poly Sequencer
to a mono one (issue #196). `AIStateMapperTest.FactoryTypeNamesRoundTrip` walks every factory key
and fails on any new mismatch; `AIStateMapperTest.ParamIdsGolden` does the same for `paramID`s,
which presets, undo snapshots and AI patches all address parameters by.

-   **`connections`**: An array detailing the signal flow between modules.
    -   `src`: The `id` of the source module.
    -   `srcPort`: The output port index of the source module.
    -   `dst`: The `id` of the destination module.
    -   `dstPort`: The input port index of the destination module.

## 4. Key Functionality

-   **Natural Language Interaction**: Users can describe desired sounds or modifications in plain English.
-   **Patch Generation & Modification**: The AI can create entirely new patches or intelligently adjust existing ones based on prompts.
-   **Context-Aware Responses**: By providing the AI with the current synthesizer state, it can generate more relevant and informed suggestions.
-   **Model Management**: Users can select from various available AI models (e.g., different Ollama models) via the application's UI.
-   **Extensible Provider System**: The `AIProvider` interface allows for easy integration of new AI backends in the future.
-   **Undoable Patches**: Apply/Merge on a patch card is recorded on the app undo stack, so `Cmd+Z` restores the user's previous patch (see below).
-   **Patch Diff Preview**: A proposed patch's `PatchCard` shows a human-readable preview by
    default — a grouped, colour-coded change list for a merge, or a plain contents summary for a
    replace (see below) — so the user sees what a patch will actually do before clicking
    Apply/Merge.

### Patch Diff Preview (PatchCard)

`AIChatComponent::PatchCard` (`Source/UI/AIChatComponent.cpp`) shows a human-readable preview as
its **default** view, computed once in `attachPatchPreview()` when each message is created (not on
every `updateChatDisplay()` re-render). What it shows depends on the patch's mode:

- **Merge mode** (`isMerge == true`, has stable node identity — see below): a change list —
  "+ Reverb", "Filter: Cutoff 400 -> 800", "+ mod LFO -> Filter Cutoff" — grouped by
  `PatchChange::Kind` (adds, then param changes, then connection changes, then modulation changes;
  see `groupChangesByKind()` below) so adds/removes/changes aren't interleaved, and colour-coded:
  green for `+` (node/connection adds), red/orange for `-` (node/connection removes), amber for
  param changes and modulation adds/removes (a modulation change is reported as a matched
  remove+add pair — see `PatchDiff.h` — so it gets a neutral colour rather than fighting for
  green/red). Rendered into the `TextEditor` line-by-line via `insertTextAtCaret()` with
  `textColourId` set per segment, since `setText()` can't colour per line.
- **Replace mode** (`isMerge == false`): a plain positive summary of the *new* patch's contents —
  "New patch: 12 modules" followed by each node's type name (no `+`/`-` prefix — nothing is being
  "added" relative to something the user cares about, it's just what the patch contains), plus a
  connection count if any. **Never a diff against the old graph** — see "Replace mode" below for
  why that would be technically correct but useless.

Raw JSON stays available behind a secondary "View JSON" toggle for anyone who wants it, pretty-
printed (`juce::JSON::toString(parsed, allOnOneLine=false)`) so it isn't one unbroken line in a
narrow chat column — falling back to the raw string if it fails to parse (shouldn't happen, since
this is a patch that already round-tripped through `extractJSONBlocks`). The card's height is
derived from the preview's line count (capped, with the toggle as the escape hatch for a very long
preview), not a fixed constant; this is a count of *logical* lines, not rendered/wrapped ones, so a
long `ParamChanged` line that wraps in the narrow column can still be short on visible space before
the `TextEditor`'s own scrolling kicks in.

**The diff is computed from two full graph snapshots, never the raw patch JSON.** This is the
whole point, not an implementation detail: diffing the patch JSON against the live graph would
misreport three things the patch itself never states —

- merge mode auto-connects a new audio node with no outgoing wire to Audio Output, and a new
  MIDI-accepting node to an existing MIDI source (`applyJSONToGraph`'s `autoConnectNewNodes`);
- replace mode deletes every node the patch doesn't restate;
- the untrusted-apply path rescales any `[0,1]`-range value against a wider parameter range
  (`AIStateMapper::applyParamsToProcessor`'s normalized-value heuristic) — the value that lands is
  not the value the patch states.

`AIIntegrationService::computePatchPreview(jsonString, mergeMode, before, after)`
(`Source/AI/AIIntegrationService.h/.cpp`) produces the two snapshots without touching the live
graph:

- `before` is `AIStateMapper::graphToJSON(audioGraph)` for replace mode. For merge mode it is
  `graphToJSON` of a scratch graph immediately after `replayLiveGraphTrusted()` — the live graph
  replayed onto scratch trusted, clearExisting=true — rather than a second direct call on
  `audioGraph`. Both must travel the *same* param round-trip (denormalize ->
  `setValueNotifyingHost` -> renormalize, including `snapToLegalValue` on a skewed or interval
  range) or an untouched node on a skewed range (e.g. `LFOModule`'s `rateHz`) can show a phantom
  `ParamChanged` purely from replay rounding. Guarded by
  `PatchDiffIntegrationTest.NoOpPatchWithSkewedParamProducesEmptyDiff`.
- `after` is `graphToJSON` of that same scratch graph once the untrusted candidate patch has
  actually been applied to it (`applyJSONToGraph(json, scratch, clearExisting, trusted=false)`) —
  the exact scratch-graph construction `applyPatch()`'s own `PatchEval` regression check uses
  (`replayLiveGraphTrusted()` is the extracted, shared piece).
- Mirrors `applyPatch()`'s mode-less-patch repair (a replace that only validates as a merge is
  applied as a merge) so the previewed diff matches what Apply/Merge will actually do.
- Never mutates `getLastPatchError()` / `getLastPatchErrorCode()` / `didLastPatchRepairMode()` —
  it runs entirely against a scratch graph and must not clobber the error state from a previous
  real Apply attempt still on screen. Guarded by
  `PatchDiffIntegrationTest.ComputePatchPreviewDoesNotClobberLastPatchError`.
- Returns `false` (with `before`/`after` still populated) if the candidate patch fails validation
  or application on the scratch graph; `PatchCard` shows "Preview unavailable" rather than a diff
  in that case, since `after` would otherwise reflect the unapplied, pre-patch state (misleadingly
  "everything removed" for a failed replace-mode patch, whose scratch starts empty).

`Source/AI/PatchDiff.h`'s `computeDiff(before, after)` is a pure function over two
`graphToJSON`-shaped `juce::var` snapshots, returning `std::vector<PatchChange>`
(`NodeAdded`/`NodeRemoved`/`ParamChanged`/`ConnectionAdded`/`ConnectionRemoved`/
`ModulationAdded`/`ModulationRemoved`), each renderable via `describe()`. Structural notes:

- **Node identity is the `uuid` field, not the integer `id`.** Merge mode's trusted replay
  preserves both `id` and `uuid` for nodes it recreates 1:1 from the live graph
  (`applyJSONToGraph`'s `preservedId` + `adoptUuidIfTrusted`, trusted-path only), and the
  subsequent untrusted patch apply never reassigns a matched node's `uuid`. A brand-new node has
  no `uuid` in `before`, so it never spuriously matches. Connection/modulation endpoints (which
  reference nodes by `id`, meaningful only within one snapshot) are resolved to `uuid` via each
  snapshot's own `id -> uuid` map before comparing.
- **Replace mode has no stable node identity between snapshots.** `applyJSONToGraph` only
  preserves `id`/`uuid` on the trusted path, and a replace-mode apply is always untrusted, so
  `computeDiff` over a replace-mode before/after pair reports the entire prior graph removed and
  the entire new patch added — even where a node is conceptually unchanged. That's technically
  correct (every processor really is destroyed and recreated on replace) but not useful to a user
  reviewing a brand-new patch, which is why `PatchCard` never feeds a replace-mode preview through
  `computeDiff` — it calls `summarizePatch()` instead (below). `computeDiff` itself stays
  mode-agnostic and correct for any snapshot pair; this is a note about how the UI uses it, not a
  limitation of the function.
- **`graphToJSON` already collapses attenuverter chains into a `modulations` array** (scanning
  `AttenuverterModule` nodes and their wires), so `computeDiff` does not need to pattern-match
  attenuverter plumbing itself — it diffs `modulations` directly. It does exclude
  `type == "Attenuverter"` nodes from the node diff and any connection with an Attenuverter
  endpoint from the connection diff, so a modulation change is reported exactly once (as
  `ModulationAdded`/`Removed`), not also as raw node/connection noise. One known omission: an
  attenuverter wired on only one side produces no `modulations` entry in `graphToJSON` at all, so
  it is silently dropped from the diff rather than shown as add/remove noise — deliberate, since a
  half-wired attenuverter only arises from a malformed patch.
- Only a node's `params` object is diffed — never `position`, `state`, `id`, or `uuid` — so a
  merge-mode patch that repositions or re-lists an unrelated existing node doesn't read as
  "moved"/"changed" noise. A parameter's display name comes from a throwaway, never-processed
  instance of its module type (`AIStateMapper::createModule`), falling back to the raw param ID
  when not found; numeric values render at ~3 significant figures (no per-module unit-formatting
  table exists in this codebase, so no units are shown).

`Source/AI/PatchDiff.h` also exposes two smaller pure helpers used only by the UI:

- **`summarizePatch(after)`** returns a `PatchSummary` (node type list, in snapshot node order,
  plus a non-attenuverter connection count) read from a single snapshot. This is what
  replace-mode `PatchCard`s render instead of `computeDiff` output — see above.
- **`groupChangesByKind(changes)`** stable-sorts a `computeDiff` result by `PatchChange::Kind`
  (`Kind`'s declaration order already matches the desired grouping: adds/removes, then param
  changes, then connection adds/removes, then modulation adds/removes), so changes sharing a kind
  keep `computeDiff`'s original relative order. Used only for merge-mode `PatchCard` rendering;
  `computeDiff`'s own output order is untouched and still what its tests assert on.

Tests: `Tests/PatchDiffTests.cpp` — pure `computeDiff` cases, `summarizePatch`/`groupChangesByKind`
coverage, plus two regression tests (`MergeModeAutoWireAppearsInDiff`,
`UntrustedRescaleShowsLandedValueNotRawPatchValue`) proving the snapshot-diff catches what a
raw-patch-vs-live-graph diff would miss.

### Patch Feedback (thumbs up/down, P6-3)

`AIChatComponent::PatchCard` carries a "Good"/"Bad" pair next to the diff preview, plus an
optional single-line comment revealed once a rating is picked. Clicking either commits
immediately — thumbs are meant to be zero-friction, not a form — and reveals the comment field for
anyone who wants to say why; submitting a comment later (Enter or "Save") writes a second record
rather than mutating the first, since the underlying store is an append-only log, not a keyed
table.

`Source/AI/PatchFeedbackStore` appends one JSON object per line to
`<user app data>/Agent Synth/patch_feedback.jsonl` (`{timestamp, rating, comment?, conversationId?,
messageId?, patch}` — `patch` is the parsed patch JSON, falling back to a `patchRaw` string if it
doesn't parse; `conversationId`/`messageId` are the P6-9 additions below, present only when known).
This local log is written **unconditionally** on every rating, regardless of plan or whether a
server sync happens — see "Patch Feedback Sync to Server (P6-9, client side)" below for when a
rating additionally reaches the server.

The rating lives on `MessageData::ratingState`/`ratingComment` for the session (same
"not reconstructed on replay" precedent as `showUpgradeAction`, just above) — the durable copy is
the JSONL log, not the in-memory chat history.

### AI Patch Undo/Redo Contract

`AIIntegrationService::applyPatch()` routes through `AppUndoManager::recordAIPatch()` whenever an
undo manager has been injected (`MainComponent::initialiseCommon()` calls
`aiService.setUndoManager(&undoManager)`). The service holds a **non-owning, nullable** pointer —
constructing an `AIIntegrationService` without one keeps the old direct-apply behaviour, which is
what the standalone service tests do.

The recorded action is the existing snapshot-based `SnapshotAction`, not a fine-grained diff:
`graphToJSON(graph)` is captured before the patch and after it, undo restores the before-snapshot
and redo re-applies the after-snapshot. Merge mode needs no special handling — the after-snapshot
is the whole merged graph. Transactions are named `"AI patch"` / `"AI merge"`.

Two invariants:

1.  **Listener notifications must fire on undo and redo, not just the initial apply.**
    `aiPatchAboutToApply` / `aiPatchApplied` are passed to the action as its `preRestore` /
    `postRestore` hooks. Undo and redo rebuild the graph exactly the way the original apply did,
    so skipping them would leave `GraphEditor` holding stale `ModuleComponent`s pointing at
    `VisualBuffer`s that `applyJSONToGraph` has already freed — a use-after-free, not just a
    stale render. Guarded by `AIUndoTest.ListenersFireOnUndo`.
2.  **A rejected patch pushes nothing.** `applyPatch()` runs `validatePatch()` before touching any
    listener or the undo stack, and `recordAIPatch()` returns without pushing if the mutation
    reports failure, so an invalid patch leaves no no-op entry for `Cmd+Z` to consume. Guarded by
    `AIUndoTest.FailedPatchPushesNothing`.

Because the notifications are dispatched from the undoable action, they are wrapped in a
`juce::WeakReference<AIIntegrationService>` — the action can outlive nothing in practice, but the
weak ref keeps the ordering safe if the service is ever destroyed before the undo manager.

Tests live in `Tests/AIUndoTests.cpp`.

## 5a. Patch Validity: Constrained Decoding, Retry, and Repair

Untrusted model output reaches the graph through one gate — `AIStateMapper::validatePatch(...,
trusted=false)` — which returns a typed `PatchValidationError` plus a message. That gate is a
security boundary and is never relaxed to raise the pass rate; everything below works on the
*generation* side instead.

Each node's `params` keys are checked against that module's real `paramID`s
(`PatchValidationError::UnknownParameterKey`), not merely type/range-checked when present. A key
that matches nothing is rejected rather than silently ignored — `applyParamsToProcessor` only ever
looks a key up *by name* among the real parameters, so an unmatched key (e.g. a corrupted or
typo'd key from a decoding artifact) was previously dropped on the floor, leaving that parameter at
its default while the patch still reported success. Rejecting it surfaces the failure so the
bounded retry/repair loop below actually engages.

One consequence worth calling out: the schema's choice-parameter `properties` are a union across
*all* module types (see `SchemaChoiceParamIdsAreUnambiguous`), so the grammar cannot express "only
this node type's params" and `params` itself stays `additionalProperties: true` so numeric params
remain emittable (see `SchemaStillAllowsNonChoiceParameters`). A key that is real on some other
module — `"waveform"` sent for a Filter node, say — used to be silently ignored and is now a hard
`UnknownParameterKey` rejection of the whole patch. This is an intentional tightening, not measured
against live traffic (`Tools/AIPatchHarness` needs a live Ollama and is excluded from CI), so watch
for `UnknownParameterKey` becoming a new significant rejection class in a harness run.

### Measuring first

`Tools/AIPatchHarness` replays a fixed set of realistic prompts through the exact production path
and tallies rejections by `PatchValidationError`. It needs a live Ollama, so it is opt-in
(`-DENABLE_AI_HARNESS=ON`) and excluded from CI. See its README for flags and caveats.

`Tools/AIEvalHarness` answers a different question: of the patches that *do* pass validation and
apply, are they usable — an output wired to a source, not just schema-legal JSON? It scores 40
golden prompts against `Source/AI/PatchEval.h`'s structural checks (unit-tested in
`Tests/PatchEvalTests.cpp`, model-independently) and is what makes switching to a cheaper or local
model a measured decision instead of a guess. Same opt-in flag, same exclusion from CI — see its
README.

Two facts that measurement established, and that any future change here should be re-checked
against:

- **`format` enforcement is backend-dependent.** Ollama compiles the JSON schema to a GBNF grammar
  for llama.cpp models, so anything the schema *encodes* becomes impossible to emit. MLX-backed
  models (e.g. `gemma4:e4b-mlx`) ignore `format` entirely and fall back to prompt compliance.
  Schema work therefore helps some backends and not others; retries cover the rest.
- **Validation reports only the first error.** Fixing one class of failure does not simply shrink
  the histogram — it *unmasks* whatever was next in the same patch. An error count that rises after
  a fix is expected, not a regression.

### The four layers, most upstream first

1.  **Constrained decoding (`getPatchSchema()`).** The strongest guarantee, because the backend
    can enforce it. Two things matter here:
    - The `node.type` enum is generated from `moduleFactory`, not hand-written. The old literal had
      drifted — `Voice Mixer` was creatable but missing from the schema, so a constrained decoder
      could never produce one. Guarded by `SchemaModuleTypesMatchTheFactory`.

      Because the enum is *derived*, **registering a module makes it model-authorable by default** —
      the right default for an ordinary DSP module, the wrong one for anything that names an
      external resource or carries privileged state (a hosted plugin, a timeline feed). Such a
      module goes into `kNonAuthorableModuleTypes` (AIStateMapper.cpp) when it is registered.
      `AIStateMapperTest.AuthorableModuleTypesGolden` pins the exact resulting list, so either kind
      of addition fails the build until the choice is made deliberately.

      The set today: `Attenuverter` / `Mod Slot` (an implementation detail of the `modulations`
      array), `Track In` (a timeline feed, whose only meaningful state lives outside the patch), and
      **`Rec Tap`** (TL6-3). Rec Tap's reason is the sharpest of the four: it **names a file path on
      disk**, so a model that could author one could aim a recording anywhere the app can write —
      the write-side twin of the `"state"` file-path restriction below.

      **The deny set is enforced by the validator, not just by the schema** (TL7-4's mechanism,
      landed early with TL3-1's Track In node). `validatePatch(..., trusted=false)` rejects any node
      whose type is in `kNonAuthorableModuleTypes` with `PatchValidationError::InternalModuleNotAllowed`.
      Leaving it to the schema enum alone would have made "non-authorable" mean *un-suggested*
      rather than *unreachable*: the schema is only a grammar for backends that compile it, and a
      patch can also arrive from a local model or any future caller that never saw it. The **trusted**
      path is untouched — our own saves must round-trip an Attenuverter or a Track In. Callers that
      gate app-authored data with `trusted=false` before applying it trusted (session state,
      `.agsproj`, snippet files — the docs/layout.md §12.5 pairing) pass
      `allowInternalModuleTypes=true`: they are gating structure, ids, ranges and tampering, not
      authorship, and our own files legitimately contain internal nodes. The parameter defaults to
      `false`, so a new model-facing caller gets the restriction without knowing it exists.
    - `node.params` enumerates every **choice** parameter's real options, so a model physically
      cannot emit `"waveform": "White Noise"`. `additionalProperties` stays `true` so numeric
      parameters remain expressible — a grammar restricted to the listed keys would make `cutoff`
      unreachable, a worse bug than the one being fixed. Guarded by
      `SchemaConstrainsChoiceParametersToTheirOptions` and `SchemaStillAllowsNonChoiceParameters`.

    The schema is an **output** contract: every property in it is something the model is invited to
    emit. Reference data does not belong there, which is why the old `parameterChoices` block was
    removed — the choice lists do real work as enums in `params`, and stay human-readable in the
    system prompt via `getModuleSchema()`.

2.  **Validate-and-retry (`applyPatchWithRetry`).** Cross-references — "connection names an id that
    does not exist" — cannot be expressed in a JSON schema at all, so they are handled after the
    fact. On rejection the *specific* validation message is fed back ("that patch was rejected
    because X; return a corrected patch"). Retries are bounded by `kMaxPatchRetries` (2, i.e. 3
    attempts total), each is announced through the `onRetry` callback so the chat shows what is
    happening, and exhaustion surfaces the error rather than looping.

    Rejection messages name the offending id **and list the valid ones**. This is not cosmetic: a
    bare "unknown node id" gives the model nothing to aim at and the retry tends to repeat itself.

    **Merge-mode id collisions are rejected, not resolved** (`NodeIdTypeMismatch`). A patch node
    whose `id` matches a live node but whose `type` differs used to fall through the
    update-in-place branch and create a *second* node, rebinding `idMap[id]` to it — so every later
    connection/modulation in the same patch that meant the original node silently re-pointed at the
    new one. Ids the same patch also `remove` are exempt: removals run first, so re-using such an id
    names a genuinely new node. The trusted path is unaffected by design.

3.  **Repair — one rule only.** A patch that states no `"mode"` and is rejected as a *replace* is
    re-validated as a *merge*, and applied that way if it passes. This costs no round-trip and
    alters nothing about the patch's content; it only resolves which mode the caller guessed, and
    validation is the arbiter. It is deliberately **one-directional**: reading a patch as a merge
    can only preserve nodes the user already had, whereas quietly turning a merge into a replace
    would wipe their patch. Never fires when the model stated a mode. Guarded by the
    `AIPatchRetryTest.Repair*` tests.

    No other repair is applied. Dropping a connection that names a stale id would silently give the
    user less than they asked for, which is exactly the silent-partial-apply failure the strict
    gate exists to prevent.

4.  **Prompt.** Weakest guarantee, so it carries the least. It documents the module/parameter
    tables and the merge-vs-replace rules; the enforceable parts live in the schema.

### Logging

One line per rejection and one per retry. Retries happen at user-click frequency, so this stays
within the no-high-frequency-logging rule — never log per candidate token or per validation pass.

Tests: `Tests/AIPatchValidationTests.cpp` (table-driven, one malformed patch per
`PatchValidationError`, plus the schema contract) and `Tests/AIPatchRetryTests.cpp` (retry bound,
error feedback, repair scope).

## 5b. Few-Shot Patch Examples

The first `AIEvalHarness` sweep (P2-8: 6 local models, 40 prompts each, single run) found nothing
shippable — best was `gemma4:e4b-mlx` at 71.1% overall pass, everything else in the high 50s/low 60s.
The failures were not syntactic (`validatePatch` already forces well-formed JSON) but structural: a
model produces a patch with no `Audio Output` node, or with nodes that are never actually wired to
one. The prompt's module/parameter tables and syntax snippets tell a model the *rules*; they never show
it a complete, working patch end to end.

`initSystemPrompt()` (`Source/AI/AIIntegrationService.cpp`, ~line 430) now embeds 5 hand-authored
`(prompt → complete, correctly-connected patch)` worked examples, covering the categories the task
called for:

- **From scratch, bass** — Oscillator → Filter → VCA → Audio Output, with a Filter Env and an Amp Env
  each driving a real modulation target (`Cutoff` destPort 1, `CV` destPort 1).
- **From scratch, lead** — the same shape plus an LFO into the Oscillator's `Fine` input (destPort 4) —
  a functional CV target, unlike the Oscillator's `Pitch` target (destPort 0), which mono oscillators
  silently ignore. The prompt calls this trap out explicitly.
- **FX chain** — a source patch chained through Distortion → Chorus → Reverb, in line, into Audio
  Output. Also flags that Delay's and Reverb's advertised CV modulation targets are vestigial (the
  audio engine never reads that CV) — don't route `modulations` into them.
- **Merge mode, adding a node** — stacking a second, detuned Oscillator into an existing filter, only
  emitting the new node and the one new connection per the delta convention.
- **Merge mode, removing a node** — removing a Distortion node and rewiring around the gap, the exact
  failure category (a node removed but the chain left dangling) the task was written to fix.

**Non-overlap with `AIEvalHarness`.** Reusing one of the harness's 40 eval prompts as a few-shot example
would be train/test contamination and invalidate the P2-8 measurement. Each example's prompt text was
checked by hand against `Tools/AIEvalHarness/Main.cpp`'s `scenarios()` for verbatim or near-paraphrase
overlap, and `AIIntegrationServiceTest.WorkedExamplePromptsDoNotOverlapEvalScenarios`
(`Tests/AIIntegrationServiceTests.cpp`) enforces it in CI against a manually-synced copy of the 40
prompts — a guard against future drift, not a substitute for the manual check when new examples are
added.

**Proof.** `AIIntegrationServiceTest.WorkedExamplePatchesAreStructurallyValid` catches a hand-authoring
mistake (a dangling node, an out-of-range parameter) at CI time by running every example through
`AIStateMapper::applyJSONToGraph` and `synth::evaluatePatch`. That is necessary but not sufficient — it
proves the examples themselves are valid, not that they help a model. The actual evidence is a
before/after `Tools/AIEvalHarness` pass-rate delta, measured on the same machine immediately before and
after this change (`--runs 3` except `gemma4:12b-it-qat` at `--runs 1`, per its ~180s/request cost):

| Model | Before (overall pass) | After (overall pass) | Δ |
| :--- | :--- | :--- | :--- |
| `gemma4:e4b-mlx` | 60.0% (72/120 applied) | **100.0%** (120/120 applied) | **+40.0pp** |
| `artifish/llama3.2-uncensored` | 57.7% (64/111 applied) | 64.0% (71/111 applied) | +6.3pp |
| `llama3.2:1b` | 63.5% (54/85 applied) | 61.5% (48/78 applied) | -2.0pp |
| `gemma4:12b-it-qat` | 100% (34/34 applied, n=34 — 6 of 40 requests hit a connection error and were excluded) | 97.5% (39/40 applied, n=40 — clean run) | ~flat |

`gemma4:e4b-mlx` — the strongest model in the original P2-8 sweep and, per §5a, one whose MLX backend
ignores `format` entirely and falls back to prompt compliance — goes from "good but not shippable" to a
clean 100%. That is exactly the model this technique should help most: schema/grammar enforcement can't
reach it, so prompt content is the only lever available, and it was the missing lever. It is now a
credible cheaper/local default candidate.

`artifish/llama3.2-uncensored` improves modestly; its "has Audio Output" rate alone jumped from 64.0% to
91.9%, so the examples clearly taught it to include an output node — "source reaches Audio Output"
(actual connectivity) is now its bottleneck instead.

`llama3.2:1b` is flat-to-slightly-down on pass rate (within sampling noise at this size), but its raw
apply-failure count rose (35 of 120 never applied before, vs. 34 never-applied + 8 provider errors = 42
of 120 after). The larger prompt appears to cost this 1B model more in basic JSON-production reliability
than it gains in structural quality — a real, model-size-dependent downside, not a rounding error.

`gemma4:12b-it-qat` (the app's shipped default) is essentially unchanged and already near-ceiling in
both runs; the "after" number is the more trustworthy of the two since it didn't lose any requests to
connection errors.

**Prompt-token growth.** The system prompt's literal text grew from ~5.8K to ~11.6K characters (~1.4K to
~2.9K tokens at a rough 4-chars/token estimate) — it roughly doubled. On a hosted API that is a real
per-request cost, not a rounding error; it is justified here by `gemma4:e4b-mlx`'s +40pp jump but is a
genuine net negative for `llama3.2:1b`, which should factor into any future decision to widen this
example set further or to gate it per-model.

## 5c. Untrusted Timeline Data: `validateTimeline` (TL8-1)

`synth::validateTimeline` (`Source/Timeline/TimelineValidator.h/.cpp`) is the untrusted gate for
AI/tool-supplied **timeline** JSON — tracks, clips, notes and automation lanes in the dialect
`TimelineDoc::toVar` writes. It is a separate function from `validatePatch`, with its own error
enum (`TimelineValidationError`) and its own name table (`timelineValidationErrorName`, same idiom
as `patchValidationErrorName`).

### The two-door model

There are two ways timeline data could reach the app, and exactly one of them is open:

| Door | Status | Rule |
| --- | --- | --- |
| The **patch grammar** — a `"timeline"` key inside a patch suggestion | **Closed, permanently** | `validatePatch(trusted=false)` refuses it (`TimelineNotAllowed`, §"Patch format forward-compatibility"). A patch is applied to the graph; a timeline is not part of a graph. |
| The **tools** — TL8-4's discrete app-side timeline tools (add-track, place-clips, write-lane) | **Open, guarded** | Each payload goes through `validateTimeline` before it touches `TimelineDoc`. |

TL8-1 is the "deliberate commit that opens the door TL0-4 closed" — through its own guarded
entrance, *not* by relaxing the patch path. Pinned by
`TimelineValidatorTest.PatchGrammarStillRefusesTimelineData`, which takes a document this validator
accepts, smuggles it into a patch, and asserts `validatePatch` still refuses it.

### Contract

-   **Validates strictly, mutates nothing** — not the doc, not the graph, not the input `var`.
-   The caller applies via `TimelineDoc::fromVar` (all-or-nothing) **only** after this passes.
-   **A pass means the apply cannot fail.** The last thing the function does is load the document
    into a throwaway `TimelineDoc` to prove it. A var that satisfies every named check and is still
    refused by the loader yields `InternalError` — the validator and `fromVar` have drifted, and the
    caller applies nothing either way. (The one reachable case today is a malformed
    `nextTrackId`/`nextClipId`/`nextLaneId`/`nextNoteId` counter — the document's own bookkeeping,
    which a tool payload has no reason to carry.)
-   **Rejects where the trusted paths repair.** `fromVar` clamps a breakpoint's tension into
    `[-1, 1]` and its value into the lane's range snapshot, and repairs a broken sort order. None of
    that happens for untrusted data: a value we would have to correct is a value the sender did not
    mean, so it is refused with a message that says which one and why.
-   Only the **first** problem is reported, like `validatePatch` — fixing one class can unmask the
    next. Every message names the offending track/clip/note/lane so it can be handed back as a
    correction rather than a complaint.

### The checks

1.  **Structural** — root is an object of the `TimelineDoc` dialect; `version` present, integer, and
    no newer than `TimelineDoc::kFormatVersion`; `tracks` (if present) an array; ids present,
    positive and unique per kind; one lane per `(nodeUuid, paramId)` doc-wide.
    **Unknown top-level keys are refused**, where `PatchDocument` deliberately *preserves* the ones
    it does not understand. That asymmetry is the point: forward-compatibility is a property a
    document format needs and an untrusted payload does not, and an ignored key is exactly how a
    later build starts honouring a field today's gate never inspected.
2.  **Caps** — the per-container limits are `TimelineDoc`'s own constants, referenced and never
    duplicated: `kMaxTracks` (256), `kMaxClipsPerTrack` (4096), `kMaxNotesPerClip` (16384),
    `kMaxLanesPerTrack` (512), `kMaxBreakpointsPerLane` (16384). Two more exist only on this path,
    because they bound the whole payload rather than one container:
    `kMaxTotalNotesUntrusted = 65536` (notes summed across every clip) and
    `kMaxPpqUntrusted = 100000.0` (the largest beat position or length in beats — ~14 hours at
    120 BPM).
3.  **Beats** — every `startBeat`, `lengthBeats`, fade and breakpoint beat must be finite, `>= 0`
    and `<= kMaxPpqUntrusted`; lengths must be `> 0` (`BeatOutOfBounds`).
4.  **Notes** — pitch `0..127`, velocity `1..127`, channel `1..16`, **rejected** rather than
    clamped (`NoteOutOfRange`).
5.  **Lanes** — every `(nodeUuid, paramId)` must resolve against the **live graph**: a node carrying
    that `uuid` property, holding a `RangedAudioParameter` with that `paramID`. Unresolvable is
    `UnresolvableBinding` — an orphaned binding is a state the app *recovers from* when a node
    disappears under an existing lane (TL2-6), not one untrusted input may author from nothing. The
    same rule applies to a track's non-empty `bindingUuid` (empty is legal: an unbound track).
    Every breakpoint value must sit inside the **resolved parameter's real range** — *not* the
    lane's own `RangeSnapshot`, which is data the sender wrote and therefore cannot be the authority
    on what the parameter accepts. `tension` must be in `[-1, 1]`, `curve` in `0..2`.
6.  **Assets** — any clip with a non-empty `assetRef` is refused (`AssetNotAllowed`). Stricter than
    `TimelineDoc::isValidAssetRef`, which only stops a path escaping the bundle: untrusted input may
    not name an asset **at all**, however well-formed.
7.  **Record modes** — a lane may only ask for `Read` (1) or `Off` (0). `Touch`/`Latch`/`Write` arm
    the lane to capture the user's own gestures, which is the user's decision
    (`RecordModeNotAllowed`).
8.  **Track kinds** — `Midi` (0), `Audio` (1) and `Automation` (2); reserved kinds 3..15 are refused
    (`ReservedKindNotAllowed`). An audio *track* is a legal shape — what makes it unauthorable in
    practice is check 6, since an audio track with no asset-bearing clip is just an empty row.

### Trusted-only forever

Audio assets and the clips that reference them, plugin state blobs, a node's `"state"` object
(`ModuleBase::setExtraState`), and lane record arming. Each is a capability that reaches outside the
document — the filesystem, opaque third-party state, or the user's own playing — and none of them
has an untrusted form. Widening `validateTimeline` to admit one is the same class of mistake as
relaxing `validatePatch` to raise the AI pass rate.

Tests: `Tests/TimelineValidatorTests.cpp` (table-driven, one deliberate defect per case, every
`TimelineValidationError` value covered).

## 5d. Arrangement Context: `ArrangementContext::summarize` (TL8-3)

`synth::ArrangementContext::summarize` (`Source/Timeline/ArrangementContext.h/.cpp`) is the
timeline sibling of the patch-context injection: a compact, token-bounded, read-only text summary
of the arrangement — tracks, clip windows, note counts and automation lanes — folded into the same
outgoing AI request the current patch JSON already rides on.

### Where it's built in

`AIIntegrationService::buildPatchAugmentedContent` (the function `sendMessage` calls to build the
structured-output request) is the one seam patch context reaches the model through today ("Current
patch state:\n\`\`\`json...\`\`\`"). TL8-3 adds a second, independent section right beside it,
under its own "## Arrangement" delimiter, gated `#if SYNTH_ENABLE_TIMELINE` and included only when
`ArrangementContext::summarize` returns non-empty text (an empty/absent timeline adds nothing, the
same "say nothing rather than say empty" rule the patch section already follows). `AIIntegrationService`
does not own a `TimelineDoc`/`TransportService` itself — `MainComponent::initialiseCommon` installs
non-owning pointers to its own (app-lifetime) instances via `setTimelineContext()`, mirroring
`setProvider()`/`setUndoManager()`.

### Security model — read path only

This is a **read-only summary that never round-trips**: nothing it emits can be replayed back into
the timeline, and it inherits the same two boundaries `validateTimeline` (§5c) enforces on the
*write* path, applied here to a text summary instead of a JSON payload:

-   **Never a file path.** An audio clip's `assetRef` is bundle-relative (`Clip::assetRef`); the
    summary emits only the bare file name (everything after the last `/`) and drops the directory
    component outright — never a stored-then-redacted path, a name that was never anything but the
    bare file name to begin with.
-   **Never a plugin/implementation identifier.** A bound track or lane is named by the bound
    node's display name (`juce::AudioProcessor::getName()`, the same string its title bar shows) —
    never a node id, factory type key, or raw uuid. `summarize()` resolves every binding against
    the **live graph** passed in (not the doc's own cached `orphaned` flags, which may be stale);
    an unresolvable binding reports `"MISSING"` rather than leaking the uuid it failed to resolve.

### Format and budget

One line per item, in `TimelineDoc`'s own stable order — a header (`"Arrangement: N tracks, bpm B,
T/S, loop [a, b)"` or `"loop off"`), then per track: kind, name, `armed`/`muted`/`soloed` flags
(shown only when set), and its binding; a compressed clip line for MIDI tracks (`"3 clips @ 0-8,
8-12, 16-20 beats; 42 notes total"`), one line per clip for audio tracks (name + beat window + bare
file name); then one line per automation lane (`"cutoff lane on Filter: 12 points, Read"`).
`maxChars` (default 2000) is enforced at **track granularity only** — a track is included whole or
not at all, so the result is never cut mid-line — and a dropped tail is marked deterministically
with `"… [+K more tracks]"`.

Tests: `Tests/ArrangementContextTests.cpp` (tracks/clips/lanes rendering across bound/unbound/
orphaned states, track-granularity truncation, the empty-doc case, and the file-path-leak pin;
plus a seam-level test that the injected request gains an "## Arrangement" section exactly when
`buildPatchAugmentedContent` should add one).

## 5e. Timeline operations (TL8-4)

`synth::TimelineOps` (`Source/Timeline/TimelineOps.h/.cpp`) is the **write** half of the timeline
seam: discrete, validated, previewable operations a model may ask for, applied to `TimelineDoc` as
one undo step. §5c is the gate they go through; §5d is the read-only context that lets a model know
what to ask for in the first place.

### The envelope

```json
{ "timelineOps": [
  { "op": "addTrack",   "kind": "midi", "name": "Bass" },
  { "op": "placeClips", "track": "Bass",
    "clips": [ { "startBeat": 0, "lengthBeats": 4, "name": "A",
                 "notes": [ { "startBeat": 0, "lengthBeats": 1,
                              "pitch": 36, "velocity": 100, "channel": 1 } ] } ] },
  { "op": "writeLane",  "nodeUuid": "…", "paramId": "cutoff",
    "points": [ { "beat": 0, "value": 800, "tension": 0, "curve": 1 } ] },
  { "op": "placeMidiClip", "track": "Bass", "startBeat": 0,
    "midBase64": "<base64-encoded Standard MIDI File>" }
] }
```

This is the **client** half of the capability. TL8-2 (platform) owns the **server** half — the
capability schema the model actually emits against, the counterpart of `getPatchSchema` for
patches. Nothing here trusts that schema: an envelope is re-validated locally whatever produced it.

**The LOCAL (Ollama) path can author this envelope too**, behind
`AIIntegrationService::setTimelineToolsEnabled` — driven by the runtime "Show timeline
(experimental)" preference via `MainComponent::applyTimelineFeatureEnabled`, and additionally gated
on a timeline context being installed (`hasTimelineContext()`). While active, three things change
and nothing else:

- the system prompt gains a "TIMELINE & AUTOMATION OPERATIONS" section teaching the four ops
  (swapped into the existing history **in place** — a mid-conversation toggle never clears the
  chat, and off means byte-identical to the pre-timeline prompt);
- the structured-output `format` becomes `AIStateMapper::getPatchSchemaWithTimelineOps()` —
  `getPatchSchema()` plus an OPTIONAL `timelineOps` array whose item schema is deliberately
  permissive (one object shape, only `"op"` required): it is a grammar that lets the model express
  the ops, not a validator — `TimelineOps::validate` remains the gate, and the reserved-fields
  rule (`"timeline"`, `"schemaVersion"`, node `"uuid"` absent) is untouched;
- the outgoing request grows an `## Automation targets` section
  (`buildAutomationTargetsSection()`): one line per uuid-bearing node listing its float parameter
  ids and RAW ranges — the addressing channel `writeLane` needs. Node uuids appear there **on
  purpose**, despite `ArrangementContext`'s no-uuid rule: that rule keeps identifiers out of the
  human-readable summary; this section is what makes the grammar usable at all, uuids are random
  per-node identity (never a path or plugin id), and `validate()` only accepts pairs that resolve
  against the live graph anyway. Bounded to ~2000 chars, whole lines, with a truncation marker.

Extraction, validation, preview and Apply are unchanged and provider-agnostic either way — they
act on what a response actually carries, and the user's Apply click stays the write gate.
Pinned by `AIIntegrationServiceTest.TimelineToolsToggle*` / `AutomationTargetsSection*`.

| Op | What it does | What it deliberately does not do |
| --- | --- | --- |
| `addTrack` | Creates the **doc** track. `kind` is `"midi"` or `"automation"`. | No graph node, no Track In wiring — binding a track to a module is a routing decision about the user's own patch, so it stays a user/host gesture. The new track is unbound and the preview says so. `"audio"` is not offered: an audio track needs an asset, and assets are trusted-only. |
| `placeClips` | Places clips (and their clip-relative notes) on a MIDI track, targeted by exact name or `{"index": N}`. | A name matching no track — or more than one — rejects the whole batch rather than guessing. |
| `writeLane` | Find-or-creates the lane for `(nodeUuid, paramId)` on the document's Automation track (creating that track if there is none — the TL5-9 rule, exactly as `MainComponent::automateParameter` does it), then REPLACES every point in the written span (min..max beat of the payload, inclusive) in one `editBreakpoints` call. | Never sets a record mode; never widens a range. |
| `placeMidiClip` | Decodes `midBase64` and parses it with `MidiClipFile::importFromStream` (TL3-4), placing one clip per non-empty imported SMF track on the target MIDI track at `startBeat` (clip length is `ceil` of its last note's end, floored at 1 beat — reusing `MidiClipFile::importIntoTrack`). | No paths, no plugin ids, no code — a `.mid` blob can only ever decode to notes, which is why this is the one op that accepts an opaque binary payload at all. |

### `placeMidiClip` — the `.mid` blob is the safest AI note surface (TL8-5)

Every other op in this grammar is closed field-by-field (§"Capabilities are absent from the
grammar" below). `placeMidiClip` is the one exception that accepts an opaque, base64-encoded blob —
and it is safe to accept specifically *because* `MidiClipFile::importFromStream` (TL3-4,
`Source/Timeline/MidiClipFile.h`) was designed as "the safest future AI patching surface" from the
start: a Standard MIDI File can only ever decode to notes (`pitch`/`velocity`/`channel`/timing).
There is no way to encode a file path, a plugin identifier, or code inside one, unlike almost any
other blob a model could hand back. `placeMidiClip` reuses that exact importer — the same strict
parser a user's own MIDI-file import goes through — rather than a looser variant for AI input.

Bounds, in the order they're checked:

-   **`midBase64` size**, against `TimelineOps::kMaxMidBlobBytes` (262144) — checked on the
    STILL-ENCODED string, *before* any decode is attempted, so an oversized blob is rejected as
    cheaply as any other length check rather than by allocating a decode buffer for it first.
-   **Decodability** — invalid base64 is rejected outright.
-   **`MidiClipFile::importFromStream`'s own checks** — not a readable SMF, SMPTE time format (PPQ
    only), or any one imported track's note count over `TimelineDoc::kMaxNotesPerClip` all reject
    the op (an import failure never means "import what parsed and drop the rest").
-   **An empty result** — a blob with no notes in it is refused; there is nothing to place.
-   **The batch's own note/clip caps** — every note the blob contains still counts toward
    `kMaxTotalNotesUntrusted` exactly like a `placeClips` note does, and the target track's clip
    count is still checked against `TimelineDoc::kMaxClipsPerTrack` before anything is placed.

Any failure at any of those steps rejects the WHOLE batch, the same all-or-nothing contract every
other op has.

### Sibling, never nested

`timelineOps` sits **beside** a patch, never inside one. `validatePatch(trusted=false)` still refuses
a `"timeline"` key in patch JSON and always will (§5c's two-door model) — `"timelineOps"` is a
different key, so a single structured response may legitimately carry a patch and an ops envelope
on the same object, and each is validated and applied by its own gate with its own Apply button.
Pinned by `TimelineOpsTest.PatchGrammarStillClosed`: the document dialect smuggled in under
`"timeline"` is refused, while the same intent as a sibling `timelineOps` key is accepted by both
halves.

### Trust posture — identical to the patch card

`validate()` → preview → the user clicks Apply → `apply()`. Nothing is applied because a model asked
for it; a person agrees to it first, having read a summary of what it does.

-   `validate()` mutates nothing and returns `previewText`, a deterministic sentence —
    `Adds midi track "Bass" (unbound - bind it in the timeline panel); places 1 clip (8 notes) at
    0-4 on "Bass"; writes 12 points to Filter cutoff over beats 0-11`. A bound module is named by
    its **display name**, never its uuid (§5d's rule, on the write path).
-   The per-op checks are `validateTimeline`'s, **reused rather than re-stated**: the same caps
    (`TimelineDoc::kMax*`, `kMaxTotalNotesUntrusted`, `kMaxPpqUntrusted`), the same bounds, and the
    same rule that untrusted input is **rejected where a trusted path would clamp** — pitch 200 is
    refused, not rewritten to 127; a breakpoint outside the **live** parameter's range is refused,
    not pulled inside it. Two more caps bound the batch itself: `kMaxOps` (64) and `kMaxNameChars`
    (128); a third, `kMaxMidBlobBytes` (262144), bounds only `placeMidiClip`'s `midBase64`.
-   **Capabilities are absent from the grammar, not refused field by field.** An op has no
    `assetRef`, no `recordMode`, no `bindingUuid`, and no kind beyond `midi`/`automation`. A `.mid`
    blob is not an exception to this — it can only ever decode to notes, never a path or an id.
    Unknown fields *inside* an op are **rejected**, so a future field cannot be smuggled past a gate
    that never inspected it — the same reasoning as `validateTimeline`'s unknown-top-level-key
    refusal. Unknown keys at the *envelope root* are ignored, because that is where the sibling
    patch's own `nodes`/`connections`/`mode` live.
-   **All-or-nothing, and the preview cannot lie.** `validate()` runs the batch against a throwaway
    copy of the document (`fromVar(doc.toVar())` — replaying our own serialisation is trusted by
    definition) and `apply()` runs the *same code* against the real one, so every op sees the effect
    of the ones before it and no preview can describe an apply that then fails. A rejection means the
    live doc was never touched at all.
-   **One undo step.** The whole batch runs inside a single `AppUndoManager::recordTimelineChange`,
    so however many tracks, clips, notes and breakpoints it touches, one Cmd+Z reverts all of it —
    the contract `MidiRecorder::stopAndCommit` already relies on for a take's clip plus its notes.

### The chat seam

`AIIntegrationService` (gated `#if SYNTH_ENABLE_TIMELINE`, and only once
`setTimelineContext()` has wired the live timeline in) gains:
`extractTimelineOps()` — the same extraction `applyPatch` performs, returning the parsed root when
it carries a `timelineOps` key (**presence**, not well-formedness, so a malformed envelope is
surfaced as a visible rejection instead of being silently dropped); `previewTimelineOps()` — the
validate step; and `applyTimelineOps()`, which routes through a `TimelineOpsApplyCallback` that
`MainComponent::initialiseCommon` installs. The service holds the doc only as a `const` pointer and
owns no undo manager for it, so the **host** supplies the write path — which is what puts an
AI-applied batch on the same shared undo stack as the user's own edits.

`AIChatComponent` renders `TimelineCard` beside `PatchCard`, to the same conventions, with an
"Apply timeline changes" button. A response carrying both gets both cards; a rejected envelope gets
the card with the reason and **no button**, because a suggestion that cannot be applied must still
say why but must not look clickable.

Tests: `Tests/TimelineOpsTests.cpp` (per-op apply, one-step undo, all-or-nothing with the failing
op named by index, caps/bounds, the ungrammatical capabilities, pinned preview strings, the
patch-grammar pin, and the service seam end to end).

### Measuring validity: the timeline-ops eval scenarios (TL8-5)

`Tools/TimelineOpsHarness` is the timeline counterpart of `Tools/AIPatchHarness`, adapted to a
seam that has no live model to replay against: a `timelineOps` envelope's validity is a
deterministic function of `TimelineOps::validate` and a fixed graph, so what it measures is a fixed
set of **recorded fixtures** (`Tools/TimelineOpsHarness/Fixtures/*.json`) rather than prompts sent
to Ollama. The scenario set spans: a valid three-op envelope; a valid `placeMidiClip` carrying a
real base64 `.mid` (generated once with `MidiClipFile::exportClip`); notes over
`TimelineDoc::kMaxNotesPerClip`; a `writeLane` value outside the live parameter's range; an unknown
op field; a SMPTE-format `.mid` blob; a `midBase64` over `kMaxMidBlobBytes`; and the two-door pin —
a timelineOps-shaped payload smuggled under a patch's `"timeline"` key, checked through
`AIStateMapper::validatePatch` instead and expected to come back `TimelineNotAllowed`. Each fixture
pins its expected valid/invalid outcome plus a message (or, for the patch-smuggle fixture, a
`PatchValidationError` name) the actual result must contain, and the harness prints a
per-fixture expected-vs-actual table and a summary match rate — gated behind
`-DENABLE_AI_HARNESS=ON` like its siblings, though (having no live model in the loop) it needs none
to build or run. `Tests/TimelineOpsFixtureTests.cpp` asserts the identical fixture files as fast
gtest cases, which is what CI actually gates on.

## 5f. The Agentic Timeline Security Model (TL8-6)

The single statement the per-feature sections above implement. When extending the AI's reach into
the timeline, this table is the contract to preserve — every row exists because the mechanism next
to it enforces it, not because a prompt asks nicely.

**What AI output may author:**

| Surface | Mechanism | Bound |
|---|---|---|
| MIDI notes | `placeClips` note lists, or `.mid` blobs via `placeMidiClip` (§5e) | note caps, pitch/velocity/channel ranges REJECTED not clamped; blob ≤ 256 KiB, PPQ-only SMF parsed by `MidiClipFile` (a format that structurally cannot carry a path, a plugin id, or code) |
| Automation lanes | `writeLane` (§5e) | values validated against the **live** parameter's range intersected with the lane snapshot; (nodeUuid, paramId) must resolve against the live graph — untrusted input can never author an orphan |
| Doc-only tracks | `addTrack` (kinds `midi`/`automation` only) | unbound — wiring a Track In/Track Audio node stays a user gesture |

**What AI output may never touch, and why:**

| Never | Why | Enforced by |
|---|---|---|
| Asset references / file paths | an assetRef is a file **read**; honoring one from a model turns a chat reply into arbitrary file access | `AssetNotAllowed` in `validateTimeline` (§5c); unknown-field rejection makes `assetRef` unreachable by grammar in ops (§5e) |
| Plugin identifiers / state blobs | a plugin blob is a code-execution surface, not a parameter | node `state` is trusted-path-only (`applyExtraStateToProcessor`); internal-only module types rejected untrusted (`InternalModuleNotAllowed`); hosted-plugin types join that list in TL7-4 |
| Record arming / record modes | untrusted input must not start capturing the user's audio | `RecordModeNotAllowed` (§5c); ops carry no such field by grammar |
| The patch grammar's `timeline` key | timeline data rides its own validated door, never the patch schema — every property in `getPatchSchema` invites the model to emit it on every request | `TimelineNotAllowed` (permanent, §5c); pinned in both directions by tests and a harness fixture |

Read-path symmetry: what the model *sees* (§5d) follows the same rule — arrangement summaries carry
bare file names only, never paths or directories.

## 5. AIChatComponent and Logging

`AIChatComponent` (`Source/UI/AIChatComponent.cpp`) is the chat UI for AI-assisted patching. It wires user prompts to `AIIntegrationService` and displays the conversation history with optional JSON patch previews.

### Response timing marker

Assistant bubbles that end an in-flight wait (successful reply, provider error, cancel, or the
120 s timeout) show a compact elapsed-time label right-aligned on the same role row as `"AI"`
(e.g. `340ms`, `1.2s`, `1m 5s`). The value is wall-clock ms from send until the wait ends, stored
on `MessageData::responseMs`. History-restored turns and patch-retry / apply-failure bubbles leave
`responseMs` at `-1` and omit the marker. Format helper: `AIChatComponent::formatResponseTime`.

While a request is in flight, the `"AI is thinking..."` status line shows the same formatted elapsed
time and refreshes on a 500 ms `juce::Timer` tick (label text only — not a full chat redraw). That
timer also enforces the 120 s timeout.

### Bubble sizing and wrapped-height measurement

Each `MessageBubble` caps at `kBubbleWidthFraction` (0.8) of the message list width, with the
remaining gutter left on the side opposite the sender — user bubbles hug the right edge, assistant
bubbles the left (`MessageBubble::isUserRole()`, read by `AIChatComponent`'s layout loop). Bubble
fill/border/role/timestamp colours resolve through theme tokens (`accent`/`surfaceHi`/`border`/
`textMuted`) via `dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())`, never raw `juce::Colours`.

`AIChatComponent::computeWrappedTextHeight(font, text, width)` is the **required pattern** for any
chat-panel element whose text length varies at runtime — it measures the actual wrapped height via
`juce::GlyphArrangement` rather than estimating from a fixed line count or a fixed single-line
height, which is what let a long wrapped line (e.g. a "Preview unavailable…" status, or a long
`hostedModeNotice`/`downgradeStripLabel`) get clipped. `PatchCard`/`TimelineCard::getRequiredHeight(width)`
take the render width as a parameter for the same reason: the height calculation and the actual
render width must always agree.

### Debug Logger Registration (Debug builds only)

In **Debug builds only**, `AIChatComponent` registers itself as the global `juce::Logger` by calling `juce::Logger::setCurrentLogger(this)` inside the `#else` branch of an `#ifdef NDEBUG` guard in the constructor. The debug console (`TextEditor`) and the "Debug" toggle button are also created and wired there. The destructor unregisters under `#ifndef NDEBUG`:

```cpp
// Constructor
#ifdef NDEBUG
    juce::Logger::writeToLog("AIChatComponent initialized (Release)");
#else
    // debug console setup + addChildComponent(debugConsole) ...
    juce::Logger::setCurrentLogger(this);
#endif

// Destructor
#ifndef NDEBUG
    juce::Logger::setCurrentLogger(nullptr);
#endif
```

This means `juce::Logger::writeToLog(...)` output is piped into the in-UI `TextEditor` debug console **only in Debug builds**. In Release builds no logger registration occurs.

### Logging Rules

Appends to the debug console are coalesced and the console is length-bounded. However:

> **Do NOT add high-frequency `writeToLog` calls** (per-parameter, per-sample, per-frame, per-connection). They run on the UI thread. A per-parameter log on preset load once caused a multi-second UI freeze; this is guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`. Keep logging to errors and rare events only.

### Panel Visibility Persistence

The AI panel visibility persists via the `ApplicationProperties` key `"aiPanelVisible"` (default `false`). It is read in `MainComponent::initialiseCommon()`:

```cpp
isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);
```

Changes are written back to the same key when the panel is toggled.

### Model Discovery Ordering Contract

`AIChatComponent`'s constructor calls `refreshModels()` at construction time, which calls
`AIIntegrationService::fetchAvailableModels()`. **If the service has no provider installed
yet at that point, discovery short-circuits** (`AIIntegrationService::fetchAvailableModels`
immediately invokes the callback with `({}, false)` when `provider == nullptr`) and no model
is ever selected — `AIProvider::currentModel` (e.g. `OllamaProvider::currentModel`) stays
empty for the rest of the session.

This matters because `MainComponent` declares `aiChatComponent` as a member that is
constructed in the member-initialiser list — i.e. **before** the constructor body runs — while
`aiService.setProvider(...)` only happens later, inside `MainComponent::initialiseCommon()`.
So the chat component's own ctor-time `refreshModels()` call is guaranteed to run with no
provider installed and is therefore a no-op (it issues no `/api/tags` request at all).

**Any owner that constructs `AIChatComponent` before installing a provider on the
`AIIntegrationService` it was given MUST call `chatComponent.refreshModels()` again AFTER
`aiService.setProvider(...)`.** `MainComponent::initialiseCommon()` does this immediately
after constructing the provider (either the one injected by a caller/test, or the one built via `synth::AIProviderRegistry` from the persisted provider id — see `Source/AI/AIProviderRegistry.h`). Skipping this step means `currentModel` stays
empty and every subsequent `/api/chat` request is sent with `"model": ""`, which Ollama
rejects with HTTP 400 `"model is required"`.

Regression: this call was mistakenly deleted in commit `f7cba4a` (issue #96) and replaced
with a comment incorrectly claiming the ctor-time call already covered discovery. Locked by
`MainComponentTest.AiProviderGetsModelSelectedOnStartup` and
`AIChatComponentTest.RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction`.

Because `refreshModels()` runs more than once by design (ctor, post-`setProvider()`, and
again whenever `SettingsWindow` triggers a re-fetch after a host/provider change), it must
`modelPicker.clear()` before re-adding the `"Loading models..."` placeholder at item ID 1.
Skipping the clear lets that `addItem(..., 1)` collide with an ID already in the box (a
model from a prior successful fetch, or the placeholder itself) — `juce::ComboBox::addItem()`
jasserts on duplicate IDs, and the picker is left showing stale and fresh entries together
for the duration of the new fetch. Locked by
`AIChatComponentTest.RefreshModelsClearsStaleItemsBeforeSecondFetchResolves`.

### Auth Token Re-Push Contract

`AIIntegrationService::setAuthToken()` is a second instance of the same ordering hazard as the
Model Discovery Ordering Contract above, for the same underlying reason: `AccountService` (and
the `AIChatComponent`/`AccountRow` wiring that observes it) can exist and fire callbacks before
`MainComponent::initialiseCommon()` has installed a real `AIProvider` on the service.

`setAuthToken(token)` stores the value in `currentAuthToken` **regardless of whether a provider
is currently installed**, and forwards it to `provider->setAuthToken(...)` only if one exists.
`setProvider(...)` then re-pushes `currentAuthToken` (when non-empty) onto whatever provider it
just installed, so a token set first is never lost. `AIProvider::setAuthToken()` defaults to a
no-op, so calling it on any provider — including `OllamaProvider`, which has no notion of auth —
is always safe.

Locked by `AIIntegrationServiceTest.SetAuthTokenForwardsToInstalledProvider` and
`AIIntegrationServiceTest.SetAuthTokenBeforeProviderInstalledIsRePushedBySetProvider` in
`Tests/AIIntegrationServiceTests.cpp`.

### Conversation-Id Threading (P6-8, client side)

Server-side conversation history (`packages/conversations` in `synth-platform`) is Pro-plan only:
`RemoteProvider`'s `patch.generate` calls carry an `x-conversation-id` request header when the
client has one from a prior response in the same session, and the server responds with one (new
or same) only when it actually persisted the exchange — a free-plan response carries **no**
header at all, not an empty one.

The re-push shape mirrors the Auth Token contract above almost exactly:
`AIIntegrationService::setConversationId(id)` stores the value in `currentConversationId`
regardless of whether a provider is installed, and `setProvider(...)` re-pushes it to whatever
provider it installs next. `AIProvider::setConversationId()` defaults to a no-op, so `OllamaProvider`
and any local/test provider are unaffected automatically.

The one-way difference from the auth token: nothing external ever calls `setConversationId()` with
a *real* id under normal operation. `AIIntegrationService::sendMessage()`'s success callback (the
same branch that appends the assistant turn to `chatHistory`) captures a non-empty
`AIResponse::conversationId` and calls `setConversationId()` itself, so the **next** call in the
session continues the same server-side thread automatically. `RemoteProvider::processRequest()`
reads the response's `x-conversation-id` header (same `result.headers` lookup used for
`Retry-After`) onto the `AIResponse` it delivers, and sends the stored id as a request header only
when non-empty — it has no notion of plans and never decides on its own whether to send one.

`AIChatComponent` is the one plan-aware call site (`Source/AI/AccountService.h`'s free function
`isProPlan(const AccountSnapshot&)`, also used by `PlanBadge`): right before every
`aiService.sendMessage(...)` call in `sendButtonClicked()`, it clears the conversation id
(`aiService.setConversationId({})`) whenever the attached `AccountService` is absent or its
snapshot isn't Pro. This is defense-in-depth, not the real gate (the server enforces Pro-only
persistence on its own) — its only job is covering a Pro→Free downgrade mid-session, where a
conversation id captured earlier in the session would otherwise still be sitting in
`AIIntegrationService` and get resent to a now-free account for no reason. A brand-new free-plan
session never has an id to clear in the first place, since the server never sent one.

Locked by `RemoteProviderTest.ConversationIdHeaderSentWhenSet` /
`ConversationIdHeaderOnlySentWhenSet` / `ConversationIdHeaderCapturedFromResponseIntoAIResponse` /
`MissingConversationIdHeaderLeavesAIResponseFieldEmpty` in `Tests/RemoteProviderTests.cpp`, and
`AIIntegrationServiceTest.ConversationIdCapturedFromResponseAndRePushedToProvider` /
`EmptyConversationIdOnResponseDoesNotCallSetConversationId` /
`SetConversationIdBeforeProviderInstalledIsRePushedBySetProvider` in
`Tests/AIIntegrationServiceTests.cpp`.

`Source/AI/AuthClient.h/.cpp` additionally exposes cloud-only conversation methods alongside
`fetchEntitlement()` — `listConversations()`, `getConversation(id)`, `deleteConversation(id)`,
`deleteAllConversations()`. Note `listConversations()` is not a side-effect-free read: the server's
`GET /v1/conversations` lazily sets/clears a grace-period deletion date on every call
(`ListConversationsResult::deletionScheduledAt`, empty when null).

### Local History (P6-8, client side)

Local-first, cloud-as-sync: **every** session writes its conversation to a local file regardless of
plan; a Pro session *additionally* syncs to the cloud via the conversation-id threading above. This
gives Pro users offline resilience for free and keeps `AIChatComponent` on one code path instead of
branching storage logic throughout it.

**`Source/AI/LocalHistoryStore.h/.cpp`** — one JSON file per conversation, following
`SnippetManager`'s exact convention (static methods over an explicit directory, a
`getDefaultHistoryDirectory()` for production callers, pure/filesystem-free JSON transforms):

- Location: `<userApplicationDataDirectory>/<kSettingsFolderName>/History/<id>.json`.
- Shape: `{id, title, createdAt, updatedAt, messages: [{role, content, createdAt}]}` — the same
  field names as `AuthClient::ConversationDetailResult`/`ConversationMessage`, so a UI reading
  either backend never has to translate field names. `content` still carries any fenced ` ```json `
  block **unsplit**, exactly like a stored `AIProvider::Message` — `AIChatComponent`'s existing
  extract/clean logic (originally only run once, on `aiService.getHistory()` in the constructor) is
  now `AIChatComponent::replayMessagesFrom()`, shared by that constructor path and by restoring a
  history-panel entry, so the two can never drift.
- No file-locking — same as `SnippetManager`/`ThemeManager`/`DeviceIdStore`. The multi-instance
  concurrent-write hazard is avoided by construction: each app/plugin-instance *session* mints its
  own conversation id lazily, on its first successful exchange (`AIChatComponent::
  saveCurrentConversationLocally()`), so no two writers ever target the same file. A history-panel
  list scanning all files can very rarely race a torn write from another live instance — accepted,
  same as the classes above.
- Retention is **user-configurable, local only**: a day count (30/90/180/365) or "keep forever"
  (`LocalHistoryStore::kRetainForever`), read from `juce::ApplicationProperties`'s
  `"historyRetentionDays"` key (Settings → AI tab, added after the provider/host controls —
  `SettingsWindow.cpp`'s `AISettingsTab`). `save()` prunes by `updatedAt` on every write; an
  out-of-range persisted value (e.g. a hand-edited `0`) falls back to the 180-day default rather
  than pruning everything. Independent of that setting, a hard cap
  (`LocalHistoryStore::kHardCapFiles`, 2000 files) always applies as an engineering backstop.
  **Cloud retention is explicitly NOT per-tier/configurable in this PR** — it stays the server's
  single global default; only the local copy has a user-facing retention control.

**`Source/AI/ConversationHistorySource.h`** — the backend-agnostic interface behind the history
panel: `list()`/`get(id)`/`deleteAll()`, all callback-based (never blocking the message thread).
`LocalHistorySource` wraps `LocalHistoryStore` and answers synchronously (file I/O is fast enough
not to need offloading). `CloudHistorySource` wraps the `AuthClient` conversation methods; each call
launches a **detached** worker thread that copies everything it needs (a copy of the small,
stateless `AuthClient`, the access token, a heap-allocated cancellation flag) before returning, so
the `ConversationHistorySource` object itself never needs to outlive the call — mirrors
`AIProvider::sendPrompt()`'s existing contract that a completion callback may arrive on a background
thread, leaving the caller responsible for hopping back to the message thread
(`Component::SafePointer` + `MessageManager::callAsync`, exactly like
`AIChatComponent::sendButtonClicked()` already does for `aiService.sendMessage()`).

**Backend selection** (`AIChatComponent::historyButtonClicked()`) is plan-driven, but the cloud call
itself is **signed-in**-driven, not plan-driven — that asymmetry is deliberate: `listConversations()`
is the *only* source of a pending grace-period deletion date, and that date only ever matters for a
signed-in account that has lapsed off Pro. So: whenever signed in, a cloud `listConversations()` call
always fires (learning `deletionScheduledAt` either way); when the plan is Pro its result is *also*
the list rendered; when the plan is Free/lapsed, the list rendered instead comes from
`LocalHistoryStore`. Not signed in (or no `AccountService` attached) skips the cloud call entirely.
This call happens **only** on an explicit History-button click — never speculatively, never on a
timer — because of the read-writes-on-read caveat above.

**Restoring** an entry (`AIChatComponent::restoreConversation()`) replays its messages via
`replayMessagesFrom()` and clears `aiService`'s own `chatHistory` (`aiService.clearHistory()`) —
deliberately does **not** attempt to re-seed `AIIntegrationService`'s history with the restored
turns, since no API exists for that (out of scope for this PR; adding one is a candidate for a
follow-up). The model therefore has no memory of the restored conversation until new turns
accumulate in the current session. The restored id is adopted as this session's
`currentLocalConversationId`, so subsequent local (and, for a Pro/cloud restore,
`aiService.setConversationId(id)`-continued cloud) saves keep appending to that same conversation
rather than starting a new one.

**UI chrome** (`Source/UI/AIChatComponent.h/.cpp`):
- A **History** button (top toolbar, next to New Chat) opens a `juce::PopupMenu` — a "Clear my
  history" item, then one row per conversation (title + readable date). No custom list component
  needed; `PopupMenu` was already this codebase's pattern for a "pick one item" affordance
  elsewhere (`GraphEditor`, `ModuleLibraryComponent`, `ModMatrixComponent`).
- **Upsell strip** (`upsellButton`): a single "Upgrade to Pro" button (same `urlOpener`/`kUpgradeUrl`
  mechanism as the Quota-error bubble's button, but a persistent strip, not a per-message one).
  Shown whenever `!isProPlan(snapshot)` — **including with no `AccountService` attached at all**,
  which deliberately diverges from `accountRow`/`planBadge`/`hostedModeNotice`'s "invisible until a
  service says otherwise" convention: those default to invisible because they have nothing true to
  say yet, but "not Pro" is already true before any `AccountService` exists (every caller starts on
  the free tier, signed out). The explanatory copy ("Your history is saved locally only —
  subscribers get automatic cloud backup across devices.") lives on `historyButton`'s tooltip
  instead of its own label, so it doesn't compete for space in the bottom-chrome stack — see
  `updateUpsellStrip()`.
- **Downgrade notice** (`downgradeStripLabel`): "Your subscription has lapsed — your saved history
  will be deleted on {date}." Shown only once signed in, `!isProPlan(snapshot)`, and a
  `deletionScheduledAt` is known from the last History click. Never polled.
- Both strips follow `hostedModeNotice`'s exact construction/visibility pattern
  (`addChildComponent` + `set*Visible()` + `resized()` reserving height only when visible).

Tests: `Tests/LocalHistoryStoreTests.cpp` (save/list/get/delete round trips; pure JSON transforms;
age-based pruning at each offered retention value plus "forever"; the hard cap; out-of-range
retention falling back to the default; an unparseable `updatedAt` being kept, not treated as
infinitely old). `Tests/AIChatComponentTests.cpp` (upsell/downgrade strip visibility across
signed-out/free/pro/lapsed-with-date snapshots; history panel backend selection per plan via
`setHistorySourcesForTesting()`; "Clear my history" wired to the plan-appropriate backend; restoring
a conversation replaying its messages; every successful exchange saved locally regardless of plan).
`Tests/SettingsWindowTests.cpp` (the retention control's default, persisted-value load, round trip,
and out-of-range fallback). `Tests/BrandingTests.cpp` (`resolveApiBaseUrl()`'s Debug-only
`AGENTSYNTH_LOCAL_API_URL` env var override, used to point a local build's auth/entitlement/
cloud-history traffic at a locally-run `synth-platform` server — see `docs/testing.md` "Testing
Cloud-Gated Features Locally").

### Patch Feedback Sync to Server (P6-9, client side)

Wires the local P6-3 thumbs log to a new endpoint,
`POST /v1/conversations/:conversationId/messages/:messageId/feedback` (`{"rating": "up"|"down",
"comment"?: string}`, Bearer auth), for signed-in Pro users only, and only when a server-side
message id is available for the rated turn.

**Where the message id comes from.** A hosted (`RemoteProvider`) response that a Pro account
persisted server-side now also returns an `x-message-id` header alongside the existing
`x-conversation-id` one — `AIProvider::AIResponse::messageId`, populated in
`RemoteProvider::processRequest()` the same way `conversationId` is (read via `result.headers`,
empty when absent). Unlike `conversationId`, this is **not** re-pushed/threaded through
`AIIntegrationService` state — it's per-turn, so it just flows through the response object to
`AIChatComponent`'s callback unchanged, which stashes it onto that turn's
`MessageData::serverMessageId` at the same point `jsonPatch` is set. Only ever populated for a
live, same-session assistant message; **not** reconstructed by the history-replay loop (same
"session-scoped" precedent as `ratingState`/`showUpgradeAction`), so rating a message from a
restored conversation stays local-only — deliberate scope limit, not a gap to fix later.

**The conversation id used for the sync is the SERVER one**, `AIIntegrationService::
getConversationId()` (a new public getter over the existing `currentConversationId` member) — not
`AIChatComponent::currentLocalConversationId`, the unrelated key `LocalHistoryStore` uses. Sending
the wrong one would fail the server's ownership check indistinguishably from a nonexistent id
(404).

**The rating callback** (`AIChatComponent`'s thumbs handler, same lambda that has always called
`patchFeedbackStore.record()`) now also, after the local record:
1. Reads `aiService.getConversationId()`.
2. If the rated message's `serverMessageId` is non-empty AND that conversation id is non-empty AND
   the attached `AccountService` reports signed-in AND Pro AND a non-empty access token — fires the
   sync. Otherwise it's a silent no-op; the local log already has the rating either way.
3. The sync itself is **fire-and-forget**: a detached background thread (same shape as
   `CloudHistorySource`'s calls — copies of a small stateless `AuthClient`, the token, and plain
   strings, never `this` or any UI state) calls the new `AuthClient::submitMessageFeedback(...)`.
   No retry, no queueing, no UI error surface — a failed sync just means that one rating never
   reached the server; nothing blocks or spins on it.

`AuthClient::submitMessageFeedback(accessToken, conversationId, messageId, rating, comment,
cancelled)` mirrors `listConversations()`'s `{ok, transportError}` result-type convention. The
JSON body omits `comment` entirely when empty, the same convention `PatchFeedbackStore::record()`
already used for its own local `comment` field. Server responses this client interprets: 200 ok;
404 (wrong/unowned conversation or message id, indistinguishable from nonexistent) and 400 (bad
`rating`) surface as `!ok` with a `transportError`; 403 (non-Pro) is unreachable from this client
since it gates on Pro before ever calling, but the server independently re-checks it regardless —
same "client only decides what to show, never what to allow" boundary as every other Pro-gated
endpoint in this file.

Test injection: `AIChatComponent::setFeedbackHttpPerformerForTesting(HttpPerformer)` installs a
fake transport for the rating callback's locally-constructed `AuthClient`, mirroring
`setHistorySourcesForTesting()`'s fake-backend idiom but at the `HttpPerformer` layer (this call
doesn't go through `ConversationHistorySource` at all).

Locked by: `Tests/AuthClientTests.cpp` (`SubmitMessageFeedback*` — request shape, comment
omission, 404/403/400/transport-failure mapping). `Tests/RemoteProviderTests.cpp`
(`MessageIdHeaderCapturedFromResponseIntoAIResponse`,
`MissingConversationIdHeaderLeavesAIResponseFieldEmpty` extended to also assert `messageId`).
`Tests/PatchFeedbackStoreTests.cpp` (`IncludesConversationAndMessageIdWhenProvided`,
`OmitsConversationAndMessageIdWhenNotProvided` — old call sites keep the pre-P6-9 line shape
exactly). `Tests/AIChatComponentTests.cpp`
(`RatingWithServerMessageIdAndProAccountFiresExactlyOneFeedbackPost`,
`RatingOnFreePlanAccountDoesNotFireFeedbackPost`,
`RatingWithNoServerMessageIdDoesNotFireFeedbackPostEvenWhenPro`).

### Opt-In Prompt Collection for Product Learning (P6-7, client side)

A single settings checkbox — "Help improve AgentSynth — share my hosted-mode prompts for product
learning" (`Source/UI/SettingsWindow.cpp`'s `AISettingsTab`, next to the provider picker's hosted-
mode disclosure) — lets a signed-in user opt in to the team reviewing their hosted-mode prompt +
resulting patch for **human review** (improving prompts/UX/features), off by default. This is
explicitly **not** used to train or fine-tune AI models — the privacy policy's existing "we do not
use your prompts or patches to train AI models" promise stays intact and untouched by this feature.
Toggling off purges any already-collected samples for that user server-side immediately; the client
has nothing further to do on revoke.

The client half is a thin state-sync layer, mirroring the entitlement fetch almost exactly:
`AccountSnapshot::promptLearningOptIn`/`promptLearningOptInAt` are populated the same way
`plan`/`monthlyRequestLimit` are — `AccountService::refreshPromptLearningOptIn()` (fire-and-forget,
GET `/v1/prompt-learning`) and `setPromptLearningOptIn(bool)` (fire-and-forget, PUT
`/v1/prompt-learning` with `{"opted_in": ...}`) both go through `AuthClient`'s existing
Bearer-token layer, both no-op when signed out, and both merge their result onto the currently
published snapshot rather than replacing it (dropping the result if a sign-out raced the network
call, same guard as `refreshEntitlement()`'s). `AccountService::PendingJob` gained two `Kind`
values for this and a `bool boolArg` field (used only by `setPromptLearningOptIn`, to carry the new
value alongside the access token already occupying `arg`) — the minimal extension rather than a
second `Kind` pair or a second queue slot.

`AISettingsTab` reflects the checkbox's enabled/checked state from `AccountService`'s published
snapshot — disabled with a "Sign in required" tooltip when signed out (same gating precedent as
`AccountRow`/`PlanBadge` reading `AccountService::getSnapshot().state`), and kept live while the
Settings dialog is open by chaining onto `AccountService::onStateChanged`. That callback is a
**single `std::function` slot**, not a multicast delegate — `AIChatComponent` installs it once, for
the app's lifetime, in its `setAccountService()` — so `AISettingsTab` captures whatever was already
installed, wraps it with its own refresh, and restores the original callback verbatim in its own
destructor rather than overwriting the slot outright (which would silently stop
`AIChatComponent`'s `accountRow`/`planBadge` from refreshing for as long as the Settings dialog
stayed open). This is safe only because nothing else touches `onStateChanged` while a
`SettingsWindow` is open in practice; a future second long-lived subscriber to this slot should
make it an actual multicast rather than adding a third link to this chain.

**Distinct from two other things that also touch "prompts," easy to conflate:**
- **P4-5's failure-debug retention** is not an app-owned table at all — it's GCP Cloud Logging's
  unconditional 30-day bucket retention on the backend's error logs, which only ever contain
  `{err, capability, code}`, never a prompt body. It applies to every request regardless of this
  opt-in and has no client-side surface at all.
- **P6-8's conversation history** (`/v1/conversations/*`, see above) is a **Pro-only, user-facing
  convenience** — letting a subscriber list/resume/delete their *own* past exchanges, stored so
  *they* can come back to them. P6-7 is a **different purpose and different storage**: the product
  team reviewing opted-in samples to improve the product, available regardless of plan (consent is
  the gate here, not plan tier), stored separately from conversation rows. Toggling this off purges
  P6-7's samples; it has no effect on P6-8's conversation history, and vice versa.

Tests: `Tests/AuthClientTests.cpp` (`fetchPromptLearningPreference`/`setPromptLearningPreference` —
method/URL/headers/body, the off-by-default null-timestamp shape, unauthorized, transport failure).
`Tests/AccountServiceTests.cpp` (`setPromptLearningOptIn`/`refreshPromptLearningOptIn` go through
the authenticated job/token path and update the snapshot; both are a no-op when signed out, asserted
by a zero-calls check on the fake server — mirroring `RefreshEntitlementIsNoOpWhenSignedOut`).
`Tests/SettingsWindowTests.cpp` (checkbox disabled/unchecked with no `AccountService` or when signed
out with the "Sign in required" tooltip; enabled and reflecting the server's opted-in value once
signed in; toggling it calls into `AccountService::setPromptLearningOptIn()`).

### Account Sign-In Surface (P3-2: AccountRow / SignInDialog)

The AI panel's account UI is `Source/UI/AccountRow.h/.cpp` (a slim status row: "Sign in" /
"Signing in…" / email + "Sign out") and `Source/UI/SignInDialog.h/.cpp` (the modal device-code
flow, launched the same way `SettingsWindow` is — content handed to
`juce::DialogWindow::LaunchOptions::content.setOwned(...)`, not a `DialogWindow` subclass).
`AIChatComponent` owns an `AccountRow` member unconditionally; with no `AccountService` attached
(`setAccountService()` never called) the row is invisible and contributes zero height to
`resized()`, so every existing caller/test sees byte-identical layout.

**Single-owner-per-callback-slot contract.** `AccountService::onStateChanged` and
`onAccessTokenChanged` are single `std::function` members, not a multicast listener list.
`AIChatComponent::setAccountService(AccountService*)` is the **sole** setter of both — it installs
`onStateChanged` to call `accountRow.refresh()` and `onAccessTokenChanged` to call
`aiService.setAuthToken(token)`. Neither `AccountRow` nor `SignInDialog` ever assigns those
callbacks itself:
- `AccountRow::refresh()` re-reads the snapshot for its own UI, then forwards to a currently-open
  `SignInDialog` (tracked via a non-owning `juce::Component::SafePointer<SignInDialog>`, so a
  dialog closed by any path — its own Cancel button, auto-close on success, or the native title
  bar — never leaves a dangling pointer) by calling `dialog->refresh()`.
- `SignInDialog::refresh()` re-reads the snapshot for its own UI (code, status text, the one-time
  auto-open of the verification URL).

If a second call site ever needs `AccountService`'s callbacks, it must go through
`AIChatComponent::setAccountService()`'s fan-out (extend `refresh()`/its callers) rather than
assigning `onStateChanged`/`onAccessTokenChanged` directly — a second direct assignment anywhere
would silently steal the slot from `AIChatComponent`.

Both callback lambdas installed by `setAccountService()` capture a
`juce::Component::SafePointer<AIChatComponent>`, not `this` — required because
`AccountService::publishSnapshot()`/`setAccessTokenFromWorker()` copy the `std::function` out of
the member *before* dispatching it via `MessageManager::callAsync()`, so a callback already queued
at the moment `AIChatComponent` is destroyed still runs; the `SafePointer` is what makes that safe.
`~AIChatComponent()` also clears both slots on its stored `AccountService*` as a second layer of
defense. This is why `MainComponent.h` declares `accountService` **before** `aiChatComponent`:
members are destroyed in reverse declaration order, so `aiChatComponent` (which owns those two
callback slots) is torn down first, while `accountService` is still alive to have them cleared.

`MainComponent::initialiseCommon()` calls `aiChatComponent.setAccountService(&accountService)`
**before** `accountService.attemptSilentSignIn()`, so the wiring is live for any state changes the
silent sign-in attempt produces. `accountService` is default-constructed (production host,
`KeychainTokenStore`) — on a machine with no stored refresh token this is a fast, silent no-op
(see `AccountService::attemptSilentSignIn()`'s own doc comment), which is also why every existing
`MainComponent`-constructing test continues to run at its normal speed. A machine that *does* have
a token from a real sign-in could see different behavior when a different (e.g. unsigned/ad-hoc)
build reads that Keychain item — no test seam for this exists yet; out of scope for P3-2.

### AI Provider Registry

Providers are registered once, in `synth::AIProviderRegistry::createDefault()`
(`Source/AI/AIProviderRegistry.cpp`) — adding a new hosted provider is a single
`registerProvider(...)` call there, not edits scattered across `MainComponent` and
`SettingsWindow`. Each `ProviderDescriptor` carries a stable `id` (persisted to the
`"aiProvider"` setting) separately from its `displayName` (shown in the UI), so renaming
the UI label never breaks a user's saved selection. `AIProviderRegistry::create()` falls
back to the first registered provider when given an unknown or empty id — this covers
both a stale pre-registry saved value and any future case of a provider being removed.

### OllamaProvider: Fail-Fast on Empty Model + HTTP-Status-Aware Errors

`OllamaProvider::processRequest` (used by `sendPrompt`) now guards against the empty-model
case directly: if `currentModel.isEmpty()`, it returns
`"Error: No Ollama model selected. Check that Ollama is running and that a model is available
(ollama list)."` with `success = false` **without touching the network** — this closes the
window where the bug above could still silently POST `{"model": ""}`.

When the HTTP request itself fails, `OllamaProvider` now distinguishes a reachable-but-rejecting
server from an unreachable one using `juce::URL::InputStreamOptions::withStatusCode(&httpStatus)`:
- Non-zero `httpStatus` (server responded, but `createInputStream` returned `nullptr` because the
  status wasn't 2xx): `"Error: Ollama at <host> rejected the request (HTTP <status>)."`
- `httpStatus == 0` (no response at all — connection refused/timeout): the original
  `"Error: Could not connect to Ollama at <host>"`.

Locked by `OllamaProviderTest.SendPromptWithNoModelFailsWithoutHittingNetwork` and
`OllamaProviderTest.SendPromptIncludesSelectedModelInRequestBody` in
`Tests/OllamaProviderTests.cpp`.

### P6-13: Structured-Output Corruption, the Envelope Codegen, and the `params-openness` Tradeoff

Two corrupted local Ollama patch-generation samples with gpt-oss-20b showed a JSON key (meant to
be `"waveform": "Saw"`) replaced with garbage containing leaked model reasoning text.
**Reproduced live** (`eval-results/p6-13-baseline-patch-2026-08-21.json`, `poly-pad` scenario:
`"filterType": "LPF24", "poly": true } },?? Wait. The earlier error shows stray quotes. We must
correct JSON.` — leaked self-correction text inside a JSON key string) at a 17.5% rejection rate
(7/40) against gpt-oss:20b, unpinned sampling, the pre-P6-13 hand-written schema. **This exact
corruption is still open** — neither angle investigated below explains or fixes it; both are real,
separately-useful findings. See `Tools/AIEvalHarness/README.md` for how to reproduce.

**Confirmed: the `{}` open-schema bug.** synth-platform's `packages/inference/src/index.ts`
documents a proven Ollama grammar-compiler defect — an "anything goes" subschema spelled as `{}`
(the empty-object JSON Schema, as opposed to the boolean `true`) gets mangled into a garbage
wrapped object instead of passing the value through unconstrained, confirmed on gpt-oss:20b and
gemma4:12b-it-qat. `AIStateMapper::getPatchSchemaWithTimelineOps()`'s `"track"` field
(`Source/AI/AIStateMapper.cpp`) had exactly that shape; it is now `"type": "string"` (narrowed, not
`oneOf`/`anyOf` — see the doc comment there for why: this header's own note that llama.cpp's
grammar compiler "handles anyOf poorly" rules that out too). `getPatchSchema()`'s own `params`
field never hit this — its `additionalProperties: true` was already the JSON Schema boolean, not
`{}`.

**Reproducibility knobs.** `OllamaProvider::setSamplingOptions()` adds optional `think` /
`temperature` / `seed` fields to the request body (all omitted when unset — no production caller
sets these, so this is opt-in only). `Tools/AIEvalHarness` exposes them as `--think`/
`--temperature`/`--seed`, plus a `--mode timeline` that replays a separate scenario set through
`getPatchSchemaWithTimelineOps()` instead of `getPatchSchema()` — same request path
(`OllamaProvider::processRequest` → `format` field), just the extended schema, so a corruption fix
gets verified against both local structured-output schemas the client actually sends, not just the
plain patch one.

**Tested and refuted: `think: false`.** The leading hypothesis going in — that Ollama routes
reasoning tokens into a `message.content`-adjacent `thinking` field only when `think` is
explicitly set, and that leaving it unset was the leak — was wrong, and expensively so.
`--think false --seed 42 --temperature 0` against the exact same 40 prompts: **0/40 applied,
every single response failed with `"Root is not an object"`** (`eval-results/p6-13-think-false-
2026-08-21.json`), down from the baseline's 33/40. `think: false` does not stop gpt-oss-20b's
"harmony" format from reasoning — it removes the channel a reasoning model needs to route that
reasoning into, and the model appears to emit that reasoning as (or in place of) `content`
instead, unparseable as JSON at all rather than merely corrupted in one key. **Do not set `think:
false` for this model** — this finding exists specifically so a future session doesn't re-attempt
the same fix.

A direct `curl` against `/api/chat` with a small hand-written schema (`{"waveform": {"enum":
["Sine","Saw","Square"]}}`) confirmed `format` genuinely constrains decoding on this setup — a
clean `{"waveform":"Sine"}` back, not prompt-compliance fallback. So the corruption is not "the
grammar isn't binding at all"; it is specific to the larger, real patch schema under longer
generation (many optional properties, real conversation context) in a way a trivial schema doesn't
trigger — genuinely still open, and the next angle worth trying (context length, prompt structure,
or a non-reasoning instruct model as the task's angle (c) suggested) is a new investigation, not
this task's to finish.

**Confirmed clean: the new envelope + the `track` fix, live.** `--mode timeline` (default,
unpinned sampling, current post-rewrite schema) — the only path that exercises both P6-13 changes
together against a real model — passed 8/8 scenarios, `timelineOps` present in all 8 responses,
**0/8 corrupted/rejected** (`eval-results/p6-13-baseline-timeline-2026-08-21.json`). This isolates
two things at once: the new, stricter generated envelope (`additionalProperties: false` on every
nested object, where the hand-written schema had none) does not itself break generation under
normal sampling — which is what makes the `think:false` run's 0/40 attributable to `think:false`
and not to the schema rewrite — and the `track` field fix holds under real model output, not just
the unit test asserting its shape.

**Envelope codegen.** `AIStateMapper::getPatchSchema()` no longer hand-builds the schema field by
field. It parses `synth::generated::kPatchEnvelopeSchemaJson`
(`Source/AI/generated/PatchEnvelopeSchema.g.h`) — a header **vendored from the synth-platform
repo**, generated from `packages/contracts/src/patch.ts`'s `PatchSchema` (the same Zod source
`generate-cpp.ts` already used for the client's typed C++ structs) — then layers on the two things
that source can't know: the `"type"` enum and the per-choice-parameter `enum`s inside `params`,
both read from *this build's* live module registry (`moduleFactory`, built by instantiating every
registered `AudioProcessor` and reading `AudioParameterChoice::choices`). This is a deliberate
split, not an oversight: `packages/contracts/src/patch.ts`'s own doc comment says node `type` stays
`z.string()` because "per-module constraints are layered on by the client, not this envelope" — the
server has no module registry to enumerate against.

Regenerate after editing `patch.ts`: `pnpm --filter @platform/contracts codegen:envelope-schema`
in synth-platform, then copy `packages/contracts/generated/PatchEnvelopeSchema.g.h` into this
repo's `Source/AI/generated/PatchEnvelopeSchema.g.h` verbatim and commit both — there is no
CMake→pnpm/tsx build dependency (this repo's cache/build invariants rule that out), so this is a
manual copy-and-commit step, same discipline as `Patch.g.h`'s cross-repo flow. The generated schema
is rendered **flat/inlined** (`$refStrategy: "none"`, no `$ref`/`definitions`) rather than reusing
`buildPatchJsonSchema()`'s named-definitions rendering used for the hosted Groq/Cerebras path —
`$ref` indirection is untested territory for llama.cpp's grammar compiler, and the hand-written
schema it replaces has never needed anything but a flat shape.

**The `params`-openness tradeoff, stated plainly.** The hosted schema's `params` must stay open
(`additionalProperties: true`) because numeric parameter values can't be fully enumerated — that
requirement is unconditional, not something this fix can trade away, and `true` (not `{}`) already
satisfies it safely. Where a real conflict *would* exist — a field that legitimately needs to be
open-shaped for Ollama specifically but can't be — the resolution is a per-provider variant (open
for hosted providers, a narrower client-only form for Ollama), exactly what happened to the
timeline-ops `"track"` field above. `getPatchSchemaWithTimelineOps()` itself stays a client-side
C++ extension on top of the generated envelope — synth-platform has its own hosted
`buildTimelineOpsJsonSchema()` (`packages/contracts/src/timeline-ops.ts`) for a *different*
capability (`POST /v1/capability/timeline.generate`), but unifying the client's local schema
against it is out of this task's scope and tracked as a separate follow-up.

Locked by `AIStateMapperTest.TimelineOpsTrackFieldIsNotOpenSchema`,
`OllamaProviderTest.SendPromptOmitsSamplingOptionsWhenUnset` and
`OllamaProviderTest.SendPromptIncludesSamplingOptionsWhenSet`.

### OllamaProvider: Worker-Thread Contract — Never Silence

**Every request accepted by `sendPrompt()` eventually gets its callback invoked**, with the
model's answer or with an error string. Nothing is ever dropped quietly, because a dropped
request leaves the chat UI waiting forever with no error to show.

Three rules make that hold:

1. **The worker parks; it does not exit on drain.** `run()` loops until `threadShouldExit()`,
   waiting on the thread's own `juce::WaitableEvent` (`Thread::wait()` / `notify()`) when the
   queue is empty. It previously `break`ed out as soon as the queue emptied — and because
   `juce::Thread` only clears its handle *after* `run()` has returned, `isThreadRunning()`
   still said `true` in that window, so `sendPrompt()`'s `if (!isThreadRunning()) startThread()`
   skipped the restart and the request sat in the queue with nothing to pick it up.
2. **Queue ownership is explicit, not inferred from `isThreadRunning()`.** A `workerState`
   (`idle` / `starting` / `running`) guarded by `queueLock` decides whether a worker will drain
   the queue. It is released in the *same* locked section that empties the queue, so a
   concurrent `sendPrompt()` either hands its request to the retiring worker (which fails it
   with a callback) or sees `idle` and starts a fresh one. `sendPrompt()` also self-heals a
   `running` state whose thread has vanished — `stopThread(0)` force-kills the worker, since
   juce never waits when given a zero timeout, so `run()` never gets to hand the queue back.
   That recovery branch is deliberately untested: forcing it requires `pthread_cancel`, whose
   forced unwind aborts under glibc when it reaches the `catch (...)` in juce's
   `threadEntryPoint` (`FATAL: exception not rethrown`). **Do not call `stopThread(0)`.**
3. **Shutdown fails the queue instead of discarding it.** `~OllamaProvider()` sets
   `isShuttingDown` (late `sendPrompt()` calls then fail immediately rather than resurrecting a
   thread on a dying object), then `stopThread(2000)`. Requests still queued are failed with
   `"Error: Request cancelled - the Ollama provider is shutting down."` **inline**, not via
   `MessageManager::callAsync` — the message loop cannot be trusted to run a deferred callback
   before the provider is gone. So every shutdown callback has already fired by the time the
   destructor returns; none can fire after it.

Locked by `OllamaProviderTest.QueuedRequestDuringThreadShutdownStillCompletes` and
`OllamaProviderTest.PendingRequestsAreFailedOnDestruction`.

### Request Cancellation

`sendPrompt()` returns an `AIProvider::RequestId`; `cancel(id)` abandons that request. This is a
real abort, not a UI dismissal — the Cancel button used to only hide the spinner while the HTTP
request ran to completion, which billed a metered backend for work the user had abandoned and,
because the queue is drained serially, made the *next* message wait behind it.

The contract, which every provider must honour:

1.  **A cancelled request still gets exactly one callback**, with `AIErrorKind::Cancelled`. A caller
    is never left hanging, and never called twice.
2.  **It must not block requests queued behind it.** If it is still in the queue, `cancel()` removes
    it under `queueLock` — the same lock the worker pops with, so exactly one of them ends up owning
    it. If it is already in flight, the read is aborted so the worker is genuinely free rather than
    waiting out the response.
3.  **An unknown, completed or already-cancelled id is a safe no-op.** The UI holds stale handles
    routinely (a response and a Cancel click cross), so this is the common case. The delivery path
    deregisters the request, which is what makes a later `cancel()` inert.

`AIIntegrationService` must **not** append an assistant turn for a cancelled request: it produced no
answer, and `chatHistory` is replayed as context, so an invented turn would be fed back on every
later message. The user's own turn stays.

#### Publish before connect — the part that makes it work

**The provider does not use `juce::URL::createInputStream()`, and must not start.** A chat request
sets `"stream": false`, so Ollama sends *nothing at all* — not even response headers — until the
entire generation is finished. That means `connect()`, not `read()`, is where a request spends
essentially all of its time. `createInputStream()` connects internally and only hands the stream
back afterwards, so a factory built on it cannot expose the stream until the generation it was
supposed to cancel has already completed.

So `InputStreamFactory` takes a third argument, a `StreamPublisher`. The factory constructs the
`WebInputStream`, calls `publish(stream.get())`, and only then calls `connect()`. A factory must
also call `publish(nullptr)` before destroying a stream it is not returning, so a concurrent
`cancel()` can never reach a dead object; every store and load of the pointer is under
`RequestState::streamLock`.

This was measured, not assumed. Against a live Ollama with a long generation in flight: publishing
after connect, `cancel()` could not stop the request at all (still running after 10 s); publishing
before it, the callback fires in **1–8 ms** and the next message is answered in ~100 ms.

How the read is aborted, in order of preference:

-   **`juce::WebInputStream::cancel()`** — the real mechanism, documented to cancel a blocking read
    and prevent subsequent connection attempts, and safe to call from another thread.
-   **`synth::CancellableStream`** — an opt-in mix-in for injected test factories, which are not
    `WebInputStream`s, so tests can unblock a stream by the same route a real socket uses.
-   **The `cancelled` flag alone** — the fallback for any other stream type. The worker finishes the
    read and discards the response. Still correct, just not prompt.

Model discovery (`fetchAvailableModels`) passes a no-op publisher: it is not cancellable and is
bounded by its own 4 s connection timeout.

Locked by `OllamaProviderTest.CancelledRequestInvokesCallbackWithCancelledKind`,
`CancelDoesNotBlockSubsequentRequest`, `CancelOfUnknownRequestIdIsSafeNoOp`,
`CallbackFiresExactlyOnceEvenIfCancelRacesCompletion` (run with `--gtest_repeat=100`),
`CancelAndCompletionEachDeliverExactlyOnceInEitherOrder`,
`AIIntegrationServiceTest.CancelledRequestDoesNotAppendToHistory`, and the `AICancelTest` UI locks.

> The race test sweeps the cancel across the completion by a growing offset rather than just
> spawning two threads. Without the offset "completion wins" is never exercised — `cancel()` sets the
> cancelled flag before releasing the read, so the worker always observes the cancel (measured: 0
> completion-wins in 1000 iterations; with the sweep, ~860 in 1000).

> **Re-verifying by hand** (the unit tests use a mock stream and therefore cannot cover
> `WebInputStream::cancel()`): with `ollama serve` running, send a prompt that generates for a while,
> hit Cancel, and confirm the input frees immediately and the next message is answered without delay.
> A regression here is invisible to the suite — the mock path would still pass.

### RemoteProvider: synth-platform Inference Service (libcurl, default as of P4-6)

`Source/AI/RemoteProvider.{h,cpp}` is the first non-Ollama `AIProvider`: it talks to a local
instance of the `synth-platform` inference service over libcurl instead of `juce::WebInputStream`.
It reuses `OllamaProvider`'s worker-thread/queue/cancellation architecture almost exactly
(`queueLock`-guarded `pendingRequests`, `inFlight` map, `idle`/`starting`/`running` worker state,
`claimDelivery()`/`deliverResult()`/`deliverError()` with the same exactly-once and
`forceSynchronous`-during-shutdown contract) — see "OllamaProvider: Worker-Thread Contract" above
for the rules this inherits unchanged.

**Wire contract:**

- `POST {host}/v1/capability/patch.generate`
- Request: `{"productName": string, "userPrompt": string}` — `currentPatch` and `promptVersion`
  are always omitted (not sent as `null`).
- Success: HTTP 200, `{"data": <Patch JSON object>}`. `data` is re-serialized with
  `juce::JSON::toString()` and delivered as `AIResponse::content` — the same raw-JSON-text shape
  `AIIntegrationService::applyPatch()` already expects from any provider.
- Error: non-2xx, `{"error": {"code": string, "message": string}}`.

**Why `userPrompt` can carry the client's already-wrapped text unmodified.**
`AIIntegrationService::buildPatchAugmentedContent()` renders the current graph into the *last*
conversation message as `"Current patch state:\n\`\`\`json\n<JSON>\n\`\`\`\n\nUser request:
<text>"` whenever the graph is non-empty, and as `"Current patch is empty.\n\nUser request:
<text>"` when it has zero nodes — the model always gets an explicit signal about whether a patch
already exists, rather than silently falling back to the bare request text (which left "is there
already a patch?" unanswered for a fresh-session prompt like "create a bass patch"). The service's own
`buildUserMessage()` (`synth-platform/packages/capabilities/src/patch-generate/capability.ts`)
performs the *exact same* wrapping server-side, from a separate `currentPatch` + `userPrompt`
pair, and only wraps when `currentPatch` is present. Sending the client's already-wrapped text as
`userPrompt` alone, with `currentPatch` omitted, therefore produces byte-identical model input to
the "proper" structured split — without RemoteProvider ever having to parse the wrapper back
apart. **Do not "fix" this into a parser** that splits `userPrompt` into `currentPatch` +
`userPrompt` — it would just reimplement, and risk diverging from, wrapping the service already
does.

**Conversational mode is out of scope.** The service exposes no plain-chat capability (its
capabilities are `patch.generate` and `timeline.generate` — see "Remote capabilities beyond
patch.generate" below). `AIIntegrationService::sendMessage()` calls `sendPrompt()` with a void
`responseSchema` for ordinary chat turns, so `RemoteProvider::sendPrompt()` fails fast — no
network call — with `AIErrorKind::Schema` when `responseSchema.isVoid()`, mirroring
`OllamaProvider`'s "no model selected" fail-fast precedent. It also fails fast the same way for an
empty `conversation` or a blank/whitespace-only last message (mirrors the service's own
`userPrompt: z.string().min(1)`).

**Error-kind mapping** (checked in this order — `cancelled` is checked before any of the below,
same as `OllamaProvider` checks `wasCancelled()` right after the network call returns):

| Condition                                    | `AIErrorKind`                    |
|-----------------------------------------------|-----------------------------------|
| transport failure that wasn't a cancellation  | `Network`                        |
| `timedOut`                                    | `Timeout`                        |
| `cancelled`                                   | `Cancelled`                      |
| HTTP 401 / 403                                | `Auth`                            |
| HTTP 429, `error.code == "QUOTA_EXCEEDED"`    | `Quota` (P4-3's monthly quota — see "Quota UI and the Upgrade Path" below) |
| HTTP 429, any other/no code                   | `RateLimit` (reads `Retry-After`) |
| HTTP 402, `error.code == "TRIAL_EXHAUSTED"`   | `TrialExhausted` (see "Device Id and Anonymous Trial" below) |
| HTTP 402, any other/no code                   | `Quota`                           |
| HTTP 503, `error.code == "SERVICE_CAPACITY_EXCEEDED"` | `ServiceCapacityExceeded` (see below) |
| HTTP 400 / 404                                | `Schema` (client/request-shape problem, not worth retrying as-is) |
| HTTP 500 / 502 / 503 (any other code) / any other unexpected non-2xx | `Server` |
| 2xx with no parseable `data`                  | `Schema`                          |

Whenever the response body parses as JSON with a string `error.message`, it is appended to the
delivered error message (mirrors the "name the specific failure" ethos on
`AIIntegrationService::buildCorrectionPrompt`); otherwise the message just names the HTTP status.
`TrialExhausted`, `ServiceCapacityExceeded`, and `Quota` via the `QUOTA_EXCEEDED` 429 path are the
exceptions to "appended": the server's `error.message` there is a complete, user-facing sentence on
its own (see below and "Quota UI and the Upgrade Path"), so it is delivered verbatim as
`AIError::message` rather than tacked onto a generic prefix. The pre-existing generic-402 `Quota`
mapping (no recognised code) keeps the appended-prefix behaviour — only the `QUOTA_EXCEEDED` 429
path is verbatim.

**Cancellation, simplified vs `OllamaProvider`.** libcurl doesn't need the
`StreamPublisher`/`activeStream`/`streamLock` machinery `OllamaProvider` uses to abort a
`WebInputStream` mid-connect: `CURLOPT_XFERINFOFUNCTION` is invoked periodically by libcurl
*during* the transfer, on the same thread running `curl_easy_perform()`, and returning non-zero
aborts it with `CURLE_ABORTED_BY_CALLBACK`. `RequestState` therefore only needs `cancelled` and
`delivered` atomics. `cancel()` on a still-queued request pulls it out of `pendingRequests` and
delivers `Cancelled` immediately (identical to `OllamaProvider::cancel()`'s queued branch);
`cancel()` on an in-flight request just sets the flag — the worker's own progress callback notices
it inside `curl_easy_perform()` and unwinds on its own. A `CURL*` handle is never touched from any
thread other than the one running `curl_easy_perform()` for it.

### Remote capabilities beyond patch.generate: timeline.generate (arrange mode)

The service exposes more than one capability, and they differ in **input shape**: `patch.generate`
takes a prompt (the `sendPrompt()` path above), while `timeline.generate`
(`synth-platform/packages/capabilities/src/timeline-generate/capability.ts`) takes structured
fields that have no home in a conversation. `AIProvider::sendCapabilityRequest(capability, body,
callback)` is the non-conversational sibling: the **caller** authors the body field by field, the
provider adds what *it* owns (`productName`, the `Authorization`/`X-Device-Id`/`x-conversation-id`
headers) and posts to `POST {host}/v1/capability/<capability>`. The default implementation on
`AIProvider` fails synchronously with a typed `Schema` error, so providers with no capability
endpoint (local Ollama, test doubles) never need to know the method exists. Inside
`RemoteProvider` a capability request rides the SAME queue/cancel/delivery machinery and — the
part that matters — the same one status→`AIErrorKind` mapping above, which is what keeps
quota/trial/capacity enforcement byte-identical across capabilities (locked by
`RemoteProviderTest.CapabilityQuotaExceededMapsToQuotaWithServerMessageIntact` /
`CapabilityTrialExhaustedMapsToDistinctKindWithServerMessageIntact`). The body is serialized to
its final JSON string on the *enqueuing* thread (`Request::capabilityBodyJson`) so no ref-counted
`juce::var` ever crosses to the worker.

**The arrange path, end to end — one intent, two transports.** `AIChatComponent` shows a
Patch/Arrange selector in the model row, visible while the timeline feature preference is on
(`areTimelineToolsEnabled()` + a live `setTimelineContext()`). The gate is deliberately
**provider-agnostic** — the local/remote parity rule: arrange mode works on both transports, so
the provider never gates the UI. Routing is the selector's call **alone — never a keyword
heuristic**; `shouldUseStructuredOutput()` stays a patch-path concern. The selector's gate
re-syncs at `refreshModels()` (a convenient known resync point) and at
`AIChatComponent::refreshModeControls()` called by `MainComponent::applyTimelineFeatureEnabled` /
`initialiseCommon` (the service has no listener for the preference switch, so the owner that
flips it re-syncs the selector). Hiding the selector resets it to Patch — an invisible control
must not keep steering requests.

An Arrange send goes through `AIIntegrationService::sendArrangeMessage()`, which absorbs the
transport difference so it is never a behaviour difference:

- **Hosted provider** → `sendCapabilityRequest("timeline.generate", …)` with the structured
  input body below.
- **Local provider (Ollama)** → `sendPrompt()` with the SAME fields composed into the outgoing
  message (`buildArrangeAugmentedContent()`, section-for-section the way the server's own
  `buildTimelineUserMessage()` composes them: arrangement context when non-empty, then tracks,
  then targets, then the prompt — plus one trailing steering line, standing in for the dedicated
  arrange system prompt the server swaps in and a mid-conversation local request cannot), and
  `AIStateMapper::getTimelineOpsEnvelopeSchema()` as the response contract: an envelope-ONLY
  grammar sharing the ops item schema with `getPatchSchemaWithTimelineOps` (one source, cannot
  drift), with `timelineOps` **required** — an arrange answer with no ops is not an answer. The
  history splice matches `sendMessage()`: chatHistory keeps the raw user text, the composed
  context exists only on the wire.

Either way the answer is the same `{"timelineOps": [...]}` envelope, and everything downstream is
shared. The structured input (`buildArrangeRequestBody()` — public, so tests reproduce the real
request):

- `userPrompt` — the **raw** user text. Deliberately NOT pre-wrapped the way
  `buildPatchAugmentedContent()` wraps the patch path's last message: `timeline.generate`'s own
  `buildTimelineUserMessage()` composes the context sections server-side from the structured
  fields below **unconditionally**, so pre-wrapping would put every section in the model input
  twice. (Contrast with the patch path's wrap-once equivalence argument above — same goal,
  opposite conclusion, because the two capabilities wrap differently server-side.)
- `arrangementContext` — `ArrangementContext::summarize()` (§5d); `""` for an empty doc (the
  schema requires the key but allows it empty).
- `paramTargets` — `{nodeUuid, nodeName, paramId, min, max, default}` per automatable parameter,
  from `enumerateAutomationTargets()`: the SAME enumeration `buildAutomationTargetsSection()`
  renders as text for the local model, so the two surfaces cannot disagree about what is
  automatable. Capped at `kMaxRemoteParamTargets` (64, mirroring the server's
  `MAX_PARAM_TARGETS` — a longer list is a 400 before any model sees it).
- `availableTracks` — `{name, kind, index}` per live `TimelineDoc` track, in doc order.
  `TimelineDoc::kMaxTracks` equals the server's `TIMELINE_OPS_MAX_TRACKS` (256), so no cap is
  needed.

History and conversation-id bookkeeping are shared with `sendMessage()` via
`wrapCompletionForHistory()` — one wrapper, so the two send paths cannot drift.

**The response re-enters the existing seam unchanged.** `timeline.generate` answers
`{"data": {"timelineOps": [...]}}`; `RemoteProvider` re-serializes `data` as `AIResponse::content`
exactly as for a patch, and the §5e flow — `extractTimelineOps()` → `TimelineOps::validate` →
`TimelineCard` preview → the user's Apply — consumes it with **no remote-specific branch**. Arrange
mode adds a second way to *ask*, never a second way to *apply*; both doors' validators are
untouched (the two-door model of §5c stands). A response that fails `TimelineOps::validate` shows
the rejection in the card with no Apply button, and there is **no client retry loop**: the server
already runs its own bounded repair-retry inside the capability (`generateStructured`), so a
rejection here is information for the user, not a trigger for another round trip.

**Why the client never calls `automation.generate`.** It is a strict subset of
`timeline.generate` in both directions: its input schema is what `TimelineGenerateInputSchema`
`.extend()`s (minus `availableTracks`, with `paramTargets` required non-empty), and its output
envelope is `writeLane`-only — anything it can say, `timeline.generate` can say, and the client's
single gate (`TimelineOps::validate`) accepts both. A second client path would mean a second body
builder, a second routing branch and a second test surface for zero user-visible gain; the
narrower capability exists server-side for clients that only automate. If a dedicated
automation-only surface ever becomes worth it client-side, the seam is ready:
`sendCapabilityRequest("automation.generate", …)` with the same body minus `availableTracks`.

Tests: `Tests/RemoteProviderTests.cpp` (capability URL/body/headers, fail-fast validation, the
entitlement-error pass-through, envelope re-serialization), `Tests/AIIntegrationServiceTests.cpp`
(`Arrange*` — request-body shape, the 64-target cap, empty-timeline explicitness, the shared
history/conversation-id contract, the local transport's composed message + envelope-only schema +
raw-text history, typed no-provider and hosted-without-capability failures), and
`Tests/AIChatComponentTests.cpp` (`Arrange*` — provider-agnostic selector gating, explicit
routing on both transports, and the card flow for a validated and a rejected canned envelope).

**HTTP transport seam.** `RemoteProvider::HttpPerformer` (`RemoteProvider.h`) parallels
`OllamaProvider::InputStreamFactory`: a constructor taking just a host installs the real
libcurl-backed performer, and a second constructor injects a fake one for tests (no real sockets —
see `Tests/RemoteProviderTests.cpp`).

**Windows is not supported yet.** `RemoteProvider.cpp`'s libcurl implementation is compiled only
under `#ifndef _WIN32`; the `#else` branch is a stub returning `transportFailed=true` with a clear
message, touching no curl API. The `windows-latest` CI job has no libcurl setup step, and the P2-5
spike already flagged Windows/WinINet as an unverified follow-up risk rather than a P2-7 blocker.
Root `CMakeLists.txt` requires `CURL` (`find_package(CURL REQUIRED)`) under `if(NOT WIN32)` —
covering both Linux and macOS (whose SDK ships a `curl.tbd` stub, so no Homebrew dependency is
needed there) — and deliberately does not require it on `WIN32`.

**Visible as of P4-6.** `ProviderDescriptor::hidden` (`Source/AI/AIProviderRegistry.h`) is a
runtime flag, not a build-time one: `RemoteProvider` is fully registered and constructible via
`AIProviderRegistry::createDefault()` (id `"remote"`, registered after `"ollama"` so the
unknown-id fallback to `descriptors.front()` is unaffected — and stays that way deliberately: an
unrecognised/corrupt persisted id fails safe to the provider that sends no data anywhere, never to
the one that does), and since P4-6 `hidden=false`, so `AISettingsTab`
(`Source/UI/SettingsWindow.cpp`) offers it in the provider combo box alongside `"ollama"`. The
`visibleProviders` member (still present even with nothing currently hidden) is used consistently
by both the population loop and `selectedDescriptor()` — indexing the combo's selected item
against the *unfiltered* `providerRegistry.listAll()` would desync the moment a future hidden
provider sits between two visible ones.

**Default provider (P4-6).** `"remote"` (hosted) is the default for a brand new install;
`"ollama"` (local) remains the default for an install that has already launched before, even if it
has never opened AI settings — see `MainComponent::resolveDefaultProviderId()` and the migration
comment in `MainComponent::initialiseCommon()`. The persisted `"aiProvider"` key is only ever
*written* by `AISettingsTab::updateSettings()`, so its absence alone can't distinguish "brand new
install" from "existing user who never touched AI settings"; `initialiseCommon()` instead checks
whether the settings file already existed on disk before this launch. Each provider persists its
own host under its own key (`"ollamaHost"` / `"remoteHost"`) — sharing one key, the pre-P4-6
behaviour, meant switching providers in Settings silently pointed the new one at whatever host
string the previous provider had left behind. An empty/unset `"remoteHost"` falls back to
`synth::branding::kApiBaseUrl` (`Source/Branding.h`), the production Cloud Run URL — see that
constant's comment for the P4-7 caveat that the service doesn't accept public traffic yet.

**Privacy disclosure (P4-6 acceptance criterion).** A visible line — not just a tooltip, since the
acceptance criterion is explicit that this "should not be discoverable only by reading a policy
page" — appears next to the model picker in `AIChatComponent` whenever the active provider is
hosted: `"Hosted mode sends your prompt and current patch to Agent Synth's servers."`
`AIProvider::isHosted()` (default `false`, overridden `true` in `RemoteProvider`) drives this via
`AIIntegrationService::isCurrentProviderHosted()`; `AIChatComponent::updateHostedModeNotice()` is
called from `refreshModels()`, the same post-`setProvider()` resync point documented above for
model discovery, so the notice's visibility never lags a provider switch. `AISettingsTab`'s
provider combo box also carries the same disclosure as a tooltip, for the toggle itself.

**Model picker in hosted mode.** `RemoteProvider::fetchAvailableModels()` always resolves
`success=true` with an empty list (the service picks its own model server-side — see that method's
doc comment). `AIChatComponent::refreshModels()`'s callback treats `success && models.isEmpty()`
as "nothing to choose from," not a fetch failure: the picker shows a single disabled
`"Model chosen automatically"` entry rather than the misleading `"Error fetching models"` a plain
`success` check would have produced for every hosted-mode user.

**Eval harness parity.** `Tools/AIEvalHarness` (see its README) can replay its golden prompt set
through `RemoteProvider` instead of `OllamaProvider` via `--provider remote`, so a model can be
scored through the exact stack a user hits — client -> service -> Ollama/Groq — instead of an
approximation of it. The service picks its own model server-side; the harness's `--model` is a
report label only in this mode.

### Device Id and Anonymous Trial (P3-3)

`Source/Auth/DeviceIdStore.h/.cpp` generates a stable per-install identifier (`juce::Uuid`,
dashed-string form) the first time it runs and persists it under the app's standard settings
folder (`userApplicationDataDirectory/<kSettingsFolderName>/device_id`, the same
`getSpecialLocation`/`kSettingsFolderName` convention `ThemeManager`/`SnippetManager` use), then
reuses it for the lifetime of the install. If the file is missing, empty, or its contents don't
look like a plausible id, a fresh one is generated and written rather than crashing or leaving the
id blank — a lost/corrupted id just makes the backend see this install as new, which is harmless.

**Not a secret.** Unlike the refresh token (`KeychainTokenStore`, Keychain-backed), the device id
grants no account access by itself — it is only a "this install" signal — so it is deliberately
stored in a plain file, not the Keychain. Both `AccountService` (for `AuthClient`) and
`RemoteProvider` construct their own `DeviceIdStore` in their production constructors and read the
same persisted value; their test constructors take an explicit `deviceId` string instead (default
empty, field/header omitted) so unit tests never touch the real per-install file.

**Where it's sent:**

- `AuthClient::requestDeviceCode()` / `pollDeviceToken()` / `refreshToken()` — `device_id` form
  field alongside the existing `client_id`, whenever non-empty.
- `RemoteProvider` — `X-Device-Id` header on every `/v1/capability/*` request, sent whether or not
  `Authorization` is also set: an anonymous free-request-tier signal when there's no bearer token,
  anti-abuse signal once there is one.

**Trial-exhausted UX path.** When the free trial is used up, the capability endpoint answers
`402 {"error":{"code":"TRIAL_EXHAUSTED","message":"..."}}`, mapped by `RemoteProvider` to
`AIErrorKind::TrialExhausted` with the server's `message` carried through unchanged (see the
error-kind table above). That response reaches `AIChatComponent` through the same path every other
provider error already does — appended to the conversation as an assistant bubble
(`"Error: " + error.message`) — so no new UI surface is needed. This "no new UI surface" reasoning
is specific to `TrialExhausted`: the caller isn't signed in yet, so "upgrade" isn't the right verb —
the server's message text specifically invites signing in with Google to continue, and the existing
`AccountRow` "Sign in" affordance (see "Account Sign-In Surface" above) is already visible
immediately above the chat whenever an `AccountService` is attached, so the next step (opening
`SignInDialog`) is always one click away without this feature needing to auto-launch it. A
service-wide `503 {"error":{"code":"SERVICE_CAPACITY_EXCEEDED","message":"..."}}` is handled the
same way but mapped to the distinct `AIErrorKind::ServiceCapacityExceeded` — it's a daily cap on
the service as a whole, unrelated to the caller's own trial/quota, and gets its own message rather
than being confused with either. `AIErrorKind::Quota` (a signed-in account over its *paid* plan's
allowance) is the one case that does get a new UI surface — see below.

### Quota UI and the Upgrade Path (P4-4)

Once a caller is signed in, P4-3's monthly request quota (`enforce-quota.ts`, `synth-platform`)
answers `429 {"error":{"code":"QUOTA_EXCEEDED","message":"..."}}` when it's exhausted, mapped by
`RemoteProvider` to `AIErrorKind::Quota` with the server's message carried through unchanged (see
the error-kind table above). Unlike `TrialExhausted`, this *is* the "you're signed in, you're over
your plan, upgrading raises it" moment, so `AIChatComponent` gives it a distinct treatment instead
of the flat error bubble every other kind gets:

- The assistant bubble carries the server's message verbatim (no `"Error: "` prefix, same as
  `TrialExhausted`/`ServiceCapacityExceeded`) plus an **"Upgrade to Pro"** button, opening
  `synth::branding::kUpgradeUrl` (`Source/Branding.h` — a static Polar checkout link; P4-4 does not
  create checkout sessions dynamically, per `docs/billing.md`'s "what this deliberately does not
  do") via the injected `urlOpener` (`AIChatComponent::setUrlOpenerForTesting()` swaps this for a
  non-browser-launching fake in tests).
- This button is carried on `MessageData::showUpgradeAction`, which is deliberately **not**
  reconstructed by the history-replay loop in `AIChatComponent`'s constructor — a `New Chat` or app
  restart drops it along with the rest of that turn's transient UI state (the same way
  Cancel-button/spinner state never survives a reload).
- A `Quota` error also triggers `AccountService::refreshEntitlement()` — a fire-and-forget re-fetch
  of `GET /v1/entitlement` that updates the account's cached plan/limit/usage without touching
  sign-in state, so a user who upgrades mid-session and immediately retries sees their new plan
  reflected (in `PlanBadge`, below) without restarting the app.

**`PlanBadge`** (`Source/UI/PlanBadge.h/.cpp`) is a small usage indicator placed in the same
bottom-chrome stack as `AccountRow` and the model picker, showing `"Free · 240 / 1000 this month"`
or `"Pro · 1,203 / 10,000 this month"`. It follows `AccountRow`'s exact zero-height-when-absent
contract (`setAccountService()`/`refresh()`/`getPreferredHeight()`) — invisible and contributing
nothing to layout until an `AccountService` is attached *and* has a known entitlement
(`AccountSnapshot::entitlementKnown`), which `AccountService::completeSignIn()` populates alongside
`fetchMe()` (same non-fatal contract: an entitlement-fetch failure never blocks sign-in) and
`refreshEntitlement()` updates on demand. `usage.requests_used` is a P4-4 addition to the
`GET /v1/entitlement` response (`docs/billing.md`) — `AuthClient::fetchEntitlement()` degrades to
`requestsUsed = 0` rather than failing the whole parse if an older server doesn't send it.

**Reachable client-side as of P4-6, still blocked at the infra layer.** `RemoteProvider` is no
longer `hidden` (see "Visible as of P4-6" above) and is the default provider for a fresh install,
so this feature — the 429→`Quota` mapping, `fetchEntitlement()`, `AccountService`'s entitlement
fields, `PlanBadge`, and the upgrade bubble — is reachable end to end from the client for the first
time. It still can't complete against production today: the deployed Cloud Run service has no
`allUsers` invoker binding, so every request 403s at the IAM layer before the app's own
`AUTH_REQUIRED` check ever runs — a deliberate P4-7 follow-up, not a defect in this client (see
`synth::branding::kApiBaseUrl`'s comment in `Source/Branding.h`).

## 6. Future Considerations

-   **Direct Saving of AI Suggested Patches**: Implement functionality for users to directly save AI-generated patches as presets.
-   **Adding AI Suggested Patches Instead of Overriding**: Provide options to merge or add AI-suggested patch components without completely replacing the existing patch.
-   **Enhanced State Mapping**: More granular control and mapping of complex module interactions and parameters.
-   **Performance Optimization**: Improving the latency of AI responses and patch application.
-   **Advanced AI Prompting**: Techniques for more sophisticated prompt construction, including few-shot examples or more detailed system instructions.
-   **User Feedback Integration**: Allowing users to explicitly rate AI-generated patches to refine future suggestions.

---
