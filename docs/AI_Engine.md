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
commit that deliberately deletes the check. The trusted path (preset/undo replay) accepts it.

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

**This is the client-only half of the roadmap's P6·3.** `Source/AI/PatchFeedbackStore` appends one
JSON object per line to `<user app data>/Agent Synth/patch_feedback.jsonl` (`{timestamp, rating,
comment?, patch}` — `patch` is the parsed patch JSON, falling back to a `patchRaw` string if it
doesn't parse). There is currently nowhere on the server to send this: `packages/conversations`'
`ConversationMessage` (synth-platform) has no rating field, and the client doesn't hold a
conversation id to key a rating against until P6·8 ships. Syncing this log to the server is tracked
separately (P6·9 in `synth-platform/roadmap.json`), gated on P6·8.

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

## 5. AIChatComponent and Logging

`AIChatComponent` (`Source/UI/AIChatComponent.cpp`) is the chat UI for AI-assisted patching. It wires user prompts to `AIIntegrationService` and displays the conversation history with optional JSON patch previews.

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
`deleteAllConversations()` — for a future history panel (not wired into any UI yet). Note
`listConversations()` is not a side-effect-free read: the server's `GET /v1/conversations` lazily
sets/clears a grace-period deletion date on every call (`ListConversationsResult::deletionScheduledAt`,
empty when null).

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

**Conversational mode is out of scope.** The service exposes only `patch.generate`; there is no
plain-chat capability. `AIIntegrationService::sendMessage()` calls `sendPrompt()` with a void
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
