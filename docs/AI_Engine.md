# Agent Synth AI Engine Documentation

This document provides a comprehensive overview of the AI Engine integrated into Agent Synth, detailing its architecture, components, communication patterns, and key functionalities. Providers, accounts, conversation history, and quota mechanics live in the companion doc, [`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md).

- [1. Overview](#1-overview)
- [2. Architecture](#2-architecture)
- [3. Communication Pattern](#3-communication-pattern)
- [4. Key Functionality](#4-key-functionality)
- [5. Patch Validity: Constrained Decoding, Retry, and Repair](#5-patch-validity-constrained-decoding-retry-and-repair)
- [6. Few-Shot Patch Examples](#6-few-shot-patch-examples)
- [7. Untrusted Timeline Data: `validateTimeline`](#7-untrusted-timeline-data-validatetimeline)
- [8. Arrangement Context: `ArrangementContext::summarize`](#8-arrangement-context-arrangementcontextsummarize)
- [9. Timeline Operations](#9-timeline-operations)
- [10. The Agentic Timeline Security Model](#10-the-agentic-timeline-security-model)
- [11. AIChatComponent and Logging](#11-aichatcomponent-and-logging)

## 1. Overview
The Agent Synth AI Engine serves as an intelligent sound design assistant, enabling users to generate and modify synthesizer patches using natural language commands. Its primary goal is to bridge the gap between intuitive textual instructions and the complex, modular architecture of the Agent Synth synthesizer. This allows for a more accessible and creative sound design workflow.

## 2. Architecture

The AI Engine's architecture is designed for modularity and extensibility, primarily centered around the `AIIntegrationService` which orchestrates interactions between AI models and the core synthesizer.

### Core Components:

-   **`AIProvider`**: An abstract interface defining the contract for any AI backend integration. This allows Agent Synth to support various large language models (LLMs) or AI services (e.g., Ollama for local inference or Remote for a hosted backend) by implementing this interface. It specifies methods for sending prompts, retrieving responses, and managing available models.

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
separately-gated door — see [§7, `validateTimeline`](#7-untrusted-timeline-data-validatetimeline).

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

### Per-node `displayName`

A node may carry a `"displayName"` string: the user's custom card title, set by double-clicking the
card header (see [`docs/layout.md`](layout.md)). It is emitted only when set, so an un-renamed node's
JSON is byte-identical to before the field existed.

Unlike `uuid` and `state`, it is applied on **both** the trusted and untrusted paths — because it is
**display-only**. It is never consulted for module-type resolution (that is `"type"`), for node
identity (`"uuid"`), for parameter values, or for anything else semantic, so the worst an untrusted
patch can do with it is mislabel a card, which the user can see and rename. It IS length-capped at
`synth::kMaxModuleDisplayNameChars` (64) on every path that accepts one — including the one the user
types into, so a title typed in the app and a title loaded from a file can never disagree about what
is storable. The cap is what stops a hostile patch stuffing a megabyte of text into a title and
wedging the canvas paint. Blank or whitespace-only means "no custom title".

Deliberately a separate field rather than reusing the processor's name: `ModuleBase::getName()` is
the auto-numbered `"Chorus 2"` that `AudioEngine::updateModuleNames()` recomputes wholesale on every
graph change, so a custom title stored there is clobbered by the next node added.

Pinned by `ModuleTitleRoundTripsThroughGraphJSON` and
`UntrustedPatchDisplayNameIsCappedAndDisplayOnly`.

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

### Patch Feedback (thumbs up/down)

`AIChatComponent::PatchCard` carries a "Good"/"Bad" pair next to the diff preview, plus an
optional single-line comment revealed once a rating is picked. Clicking either commits
immediately — thumbs are meant to be zero-friction, not a form — and reveals the comment field for
anyone who wants to say why; submitting a comment later (Enter or "Save") writes a second record
rather than mutating the first, since the underlying store is an append-only log, not a keyed
table.

`Source/AI/PatchFeedbackStore` appends one JSON object per line to
`<user app data>/Agent Synth/patch_feedback.jsonl` (`{timestamp, rating, comment?, conversationId?,
messageId?, patch}` — `patch` is the parsed patch JSON, falling back to a `patchRaw` string if it
doesn't parse; `conversationId`/`messageId` are additions described in
[`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md#3-patch-feedback-sync-to-server),
present only when known).
This local log is written **unconditionally** on every rating, regardless of plan or whether a
server sync happens — see "Patch Feedback Sync to Server" in
[`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md#3-patch-feedback-sync-to-server)
for when a rating additionally reaches the server.

The rating lives on `MessageData::ratingState`/`ratingComment` for the session (same
"not reconstructed on replay" precedent as `showUpgradeAction`, just above) — the durable copy is
the JSONL log, not the in-memory chat history.

### General Feedback (P6-10)

The Settings dialog's "Feedback" tab (`Source/UI/FeedbackSettingsTab`, last tab, added after
Appearance) is the general-purpose sibling of the patch-specific thumbs above: free-text bug
reports, feature requests, or comments not tied to any one AI-generated patch. `Source/
GeneralFeedbackStore` appends one JSON object per line (`{timestamp, category, text}`) to
`<user app data>/Agent Synth/general_feedback.jsonl` — same append-only JSON-Lines shape and
rationale as `PatchFeedbackStore` above, but its own file since the two logs track unrelated
things. P6-16 additionally syncs each submission to the server, fire-and-forget, mirroring
"Patch Feedback Sync to Server (P6-9, client side)" below with one deliberate difference: this
sync is gated on sign-in only, NOT Pro — `POST /v1/feedback` has no plan check server-side either,
so any signed-in account may submit general feedback. The local log above is still written
unconditionally regardless of sign-in state or sync outcome.

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

## 5. Patch Validity: Constrained Decoding, Retry, and Repair

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

**A quoted rate means nothing without the model and the sampling settings alongside it.**
`AIPatchHarness` pins `--temperature 0` and a fixed `--seed` by default (unlike `AIEvalHarness`,
which leaves them unset for its own before/after comparisons) — an unpinned run swings ~8
points on the same model and prompts, so a bare percentage is not comparable across runs unless
both sides fixed sampling the same way. `--provider remote` ignores these entirely (`RemoteProvider`
has no sampling knobs), which is why the harness's `--json` output only ever records
`temperature`/`seed` for `--provider ollama` — never claim pinned sampling that wasn't actually
applied. `.github/workflows/ai-eval-nightly.yml` runs this measurement on a schedule, OFF by
default; see `Tools/AIPatchHarness/README.md`'s "Nightly scheduled eval" section for the switch,
the runner, and the ratchet against a committed baseline.

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
      **`Rec Tap`**. Rec Tap's reason is the sharpest of the four: it **names a file path on
      disk**, so a model that could author one could aim a recording anywhere the app can write —
      the write-side twin of the `"state"` file-path restriction below.

      **The deny set is enforced by the validator, not just by the schema.**
      `validatePatch(..., trusted=false)` rejects any node
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

## 6. Few-Shot Patch Examples

The first `AIEvalHarness` sweep (6 local models, 40 prompts each, single run) found nothing
shippable — pass rates ranged from the high 50s to low 70s.
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
would be train/test contamination and invalidate the measurement. Each example's prompt text was
checked by hand against `Tools/AIEvalHarness/Main.cpp`'s `scenarios()` for verbatim or near-paraphrase
overlap, and `AIIntegrationServiceTest.WorkedExamplePromptsDoNotOverlapEvalScenarios`
(`Tests/AIIntegrationServiceTests.cpp`) enforces it in CI against a manually-synced copy of the 40
prompts — a guard against future drift, not a substitute for the manual check when new examples are
added.

**Proof.** `AIIntegrationServiceTest.WorkedExamplePatchesAreStructurallyValid` catches a hand-authoring
mistake (a dangling node, an out-of-range parameter) at CI time by running every example through
`AIStateMapper::applyJSONToGraph` and `synth::evaluatePatch`. That is necessary but not sufficient — it
proves the examples themselves are valid, not that they help a model.

The actual evidence is a before/after `Tools/AIEvalHarness` pass-rate delta, measured on the same
machine immediately before and after this change. The technique's biggest win was on the backend that
ignores `format` entirely and falls back to prompt compliance (see "Measuring first" above) — exactly
the case where schema/grammar enforcement can't help and prompt content is the only lever available.
Results were more mixed on other models: a longer prompt costs some models basic JSON-production
reliability, so a bigger example set is not a strictly free improvement and should be measured per
model before being widened further — the system prompt's literal text roughly doubled in size to add
these five examples, a real cost on any backend billed or latency-bound per token.

## 7. Untrusted Timeline Data: `validateTimeline`

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
| The **tools** — the discrete app-side timeline tools (add-track, place-clips, write-lane), §9 below | **Open, guarded** | Each payload goes through `validateTimeline` before it touches `TimelineDoc`. |

This is the deliberate commit that opens the door the patch format closed — through its own guarded
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
    `nextTrackId`/`nextClipId`/`nextLaneId`/`nextNoteId`/`nextMarkerId` counter — the document's own
    bookkeeping, which a tool payload has no reason to carry.)
-   **Rejects where the trusted paths repair.** `fromVar` clamps a breakpoint's tension into
    `[-1, 1]` and its value into the lane's range snapshot, and repairs a broken sort order. None of
    that happens for untrusted data: a value we would have to correct is a value the sender did not
    mean, so it is refused with a message that says which one and why.
-   Only the **first** problem is reported, like `validatePatch` — fixing one class can unmask the
    next. Every message names the offending track/clip/note/lane so it can be handed back as a
    correction rather than a complaint.

### The checks

1.  **Structural** — root is an object of the `TimelineDoc` dialect; `version` present, integer, and
    no newer than `TimelineDoc::kFormatVersion`; `tracks` and `markers` (if present) arrays; ids
    present, positive and unique per kind; one lane per `(nodeUuid, paramId)` doc-wide.
    **Unknown top-level keys are refused**, where `PatchDocument` deliberately *preserves* the ones
    it does not understand. That asymmetry is the point: forward-compatibility is a property a
    document format needs and an untrusted payload does not, and an ignored key is exactly how a
    later build starts honouring a field today's gate never inspected. The allowlist is `version`,
    `tracks`, `markers` and the five next-id counters — and a key earns its place there **only** by
    having a per-item check written for it (see 9 below). Adding one without that check defeats this
    whole paragraph.
2.  **Caps** — the per-container limits are `TimelineDoc`'s own constants, referenced and never
    duplicated: `kMaxTracks` (256), `kMaxClipsPerTrack` (4096), `kMaxNotesPerClip` (16384),
    `kMaxLanesPerTrack` (512), `kMaxBreakpointsPerLane` (16384), `kMaxMarkers` (1024),
    `kMaxMarkerTextLength` (128). Two more exist only on this path,
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
    disappears under an existing lane, not one untrusted input may author from nothing. The
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
9.  **Markers** — the `"markers"` array (`TimelineDoc::Marker`: `id`, `beat`, `text`, `colourArgb`)
    is allow-listed at the top level *because* it is checked here, field by field. A marker carries
    no binding, no asset and nothing executable — it is a beat, a label and a colour — so the rules
    are entirely about bounds and shape:
    - `id` required, positive, unique across markers (`MalformedRoot`), like a note's;
    - `beat` finite, `>= 0` and `<= kMaxPpqUntrusted` (`BeatOutOfBounds`) — the same bound every
      other beat in the document gets;
    - `text` a string of at most `kMaxMarkerTextLength` (128) characters, **rejected not truncated**
      (`MarkerTextTooLong`) — the same rule note pitch 128 gets: a label we would have to shorten is
      not the label the sender meant. An EMPTY label is legal (an unlabelled flag);
    - `colourArgb` an integer in the full 32-bit range `0 .. 4294967295` — inert display data, so
      every value is legal and only the *type* and the range are checked;
    - array size at most `kMaxMarkers` (`TooManyMarkers`);
    - **unknown keys inside a marker object are refused.** Markers are the one container checked with
      a *closed key set* — tracks, clips and notes only reject unknown keys at the top level. The
      reasoning is check 1's, one level down, and markers got the stricter rule from the start
      because that reasoning was already known when they were added; retro-fitting it onto the older
      containers is a separate change with its own compatibility question.

### Trusted-only forever

Audio assets and the clips that reference them, plugin state blobs, a node's `"state"` object
(`ModuleBase::setExtraState`), and lane record arming. Each is a capability that reaches outside the
document — the filesystem, opaque third-party state, or the user's own playing — and none of them
has an untrusted form. Widening `validateTimeline` to admit one is the same class of mistake as
relaxing `validatePatch` to raise the AI pass rate.

Tests: `Tests/TimelineValidatorTests.cpp` (table-driven, one deliberate defect per case, every
`TimelineValidationError` value covered).

## 8. Arrangement Context: `ArrangementContext::summarize`

`synth::ArrangementContext::summarize` (`Source/Timeline/ArrangementContext.h/.cpp`) is the
timeline sibling of the patch-context injection: a compact, token-bounded, read-only text summary
of the arrangement — tracks, clip windows, note counts and automation lanes — folded into the same
outgoing AI request the current patch JSON already rides on.

### Where it's built in

`AIIntegrationService::buildPatchAugmentedContent` (the function `sendMessage` calls to build the
structured-output request) is the one seam patch context reaches the model through today ("Current
patch state:\n\`\`\`json...\`\`\`"). This adds a second, independent section right beside it,
under its own "## Arrangement" delimiter, included only when
`ArrangementContext::summarize` returns non-empty text (an empty/absent timeline adds nothing, the
same "say nothing rather than say empty" rule the patch section already follows). `AIIntegrationService`
does not own a `TimelineDoc`/`TransportService` itself — `MainComponent::initialiseCommon` installs
non-owning pointers to its own (app-lifetime) instances via `setTimelineContext()`, mirroring
`setProvider()`/`setUndoManager()`.

### Security model — read path only

This is a **read-only summary that never round-trips**: nothing it emits can be replayed back into
the timeline, and it inherits the same two boundaries `validateTimeline` (§7) enforces on the
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

## 9. Timeline Operations

`synth::TimelineOps` (`Source/Timeline/TimelineOps.h/.cpp`) is the **write** half of the timeline
seam: discrete, validated, previewable operations a model may ask for, applied to `TimelineDoc` as
one undo step. §7 is the gate they go through; §8 is the read-only context that lets a model know
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

This is the **client** half of the capability. The private backend repo owns the **server** half — the
capability schema the model actually emits against, the counterpart of `getPatchSchema` for
patches. Nothing here trusts that schema: an envelope is re-validated locally whatever produced it.

**The LOCAL (Ollama) path can author this envelope too**, behind
`AIIntegrationService::setTimelineToolsEnabled` — set unconditionally on by `MainComponent` now
that the timeline is GA, and additionally gated on a timeline context being installed
(`hasTimelineContext()`). While active, three things change and nothing else:

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
| `writeLane` | Find-or-creates the lane for `(nodeUuid, paramId)` on the document's Automation track (creating that track if there is none, exactly as `MainComponent::automateParameter` does it), then REPLACES every point in the written span (min..max beat of the payload, inclusive) in one `editBreakpoints` call. | Never sets a record mode; never widens a range. |
| `placeMidiClip` | Decodes `midBase64` and parses it with `MidiClipFile::importFromStream`, placing one clip per non-empty imported SMF track on the target MIDI track at `startBeat` (clip length is `ceil` of its last note's end, floored at 1 beat — reusing `MidiClipFile::importIntoTrack`). | No paths, no plugin ids, no code — a `.mid` blob can only ever decode to notes, which is why this is the one op that accepts an opaque binary payload at all. |

### `placeMidiClip` — the `.mid` blob is the safest AI note surface

Every other op in this grammar is closed field-by-field (§"Capabilities are absent from the
grammar" below). `placeMidiClip` is the one exception that accepts an opaque, base64-encoded blob —
and it is safe to accept specifically *because* `MidiClipFile::importFromStream`
(`Source/Timeline/MidiClipFile.h`) was designed as "the safest future AI patching surface" from the
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
a `"timeline"` key in patch JSON and always will (§7's two-door model) — `"timelineOps"` is a
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
    its **display name**, never its uuid (§8's rule, on the write path).
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

`AIIntegrationService` (once
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

### Measuring validity: the timeline-ops eval scenarios

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

## 10. The Agentic Timeline Security Model

The single statement the per-feature sections above implement. When extending the AI's reach into
the timeline, this table is the contract to preserve — every row exists because the mechanism next
to it enforces it, not because a prompt asks nicely.

**What AI output may author:**

| Surface | Mechanism | Bound |
|---|---|---|
| MIDI notes | `placeClips` note lists, or `.mid` blobs via `placeMidiClip` (§9) | note caps, pitch/velocity/channel ranges REJECTED not clamped; blob ≤ 256 KiB, PPQ-only SMF parsed by `MidiClipFile` (a format that structurally cannot carry a path, a plugin id, or code) |
| Automation lanes | `writeLane` (§9) | values validated against the **live** parameter's range intersected with the lane snapshot; (nodeUuid, paramId) must resolve against the live graph — untrusted input can never author an orphan |
| Doc-only tracks | `addTrack` (kinds `midi`/`automation` only) | unbound — wiring a Track In/Track Audio node stays a user gesture |

**What AI output may never touch, and why:**

| Never | Why | Enforced by |
|---|---|---|
| Asset references / file paths | an assetRef is a file **read**; honoring one from a model turns a chat reply into arbitrary file access | `AssetNotAllowed` in `validateTimeline` (§7); unknown-field rejection makes `assetRef` unreachable by grammar in ops (§9) |
| Plugin identifiers / state blobs | a plugin blob is a code-execution surface, not a parameter | node `state` is trusted-path-only (`applyExtraStateToProcessor`); internal-only module types rejected untrusted (`InternalModuleNotAllowed`); hosted-plugin types join that list |
| Record arming / record modes | untrusted input must not start capturing the user's audio | `RecordModeNotAllowed` (§7); ops carry no such field by grammar |
| The patch grammar's `timeline` key | timeline data rides its own validated door, never the patch schema — every property in `getPatchSchema` invites the model to emit it on every request | `TimelineNotAllowed` (permanent, §7); pinned in both directions by tests and a harness fixture |

Read-path symmetry: what the model *sees* (§8) follows the same rule — arrangement summaries carry
bare file names only, never paths or directories.

## 11. AIChatComponent and Logging

`AIChatComponent` (`Source/UI/AIChatComponent.cpp`) is the chat UI for AI-assisted patching. It wires user prompts to `AIIntegrationService` and displays the conversation history with optional JSON patch previews.

### Response timing marker

Assistant bubbles that end an in-flight wait (successful reply, provider error, cancel, or the
request timeout) show a compact elapsed-time label right-aligned on the same role row as `"AI"`
(e.g. `340ms`, `1.2s`, `1m 5s`). The value is wall-clock ms from send until the wait ends, stored
on `MessageData::responseMs`. History-restored turns and patch-retry / apply-failure bubbles leave
`responseMs` at `-1` and omit the marker. Format helper: `AIChatComponent::formatResponseTime`.

While a request is in flight, the `"AI is thinking..."` status line shows the same formatted elapsed
time and refreshes on a 500 ms `juce::Timer` tick (label text only — not a full chat redraw). That
timer also enforces the request timeout, described next.

### Request timeout

The default request timeout is **4 minutes** (240000 ms), user-configurable via Settings → AI →
Request Timeout, with presets of 2, 4 (default), 6, and 10 minutes. The persisted key is
`aiRequestTimeoutMs` (milliseconds, `juce::ApplicationProperties`). Crucially, ONE value now drives
both halves of the timeout: `AIChatComponent`'s in-flight-request watchdog (the 500 ms timer above,
comparing elapsed time against `AIChatComponent::requestTimeoutMs`) and the active `AIProvider`'s
own HTTP connection timeout (`OllamaProvider`/`RemoteProvider::requestTimeoutMs`, pushed via
`AIProvider::setRequestTimeoutMs()`). `AIIntegrationService` holds the last-configured value and
re-applies it to any newly installed provider inside `setProvider()` — the same "must survive a
provider swap" contract as `refreshModels()` (see the Model Discovery Ordering Contract below) —
so switching providers can never silently reset the timeout to that provider's own hardcoded
default. Previously these were two independent, hardcoded constants (a 120 s UI watchdog and a
240 s provider timeout) that had drifted apart: the UI cancelled every request at 120 s, well before
a local Ollama model on modest hardware could legitimately finish, always producing a misleading
"timed out" error. They are now the same configurable value everywhere.

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

