# Agent Synth AI Engine Documentation

This document covers the AI Engine's architecture, communication pattern with the model, and the
patch-diff/feedback UI (§1-4 below). Patch validity (constrained decoding, retry, repair),
few-shot examples, the untrusted-timeline data model, arrangement context, timeline operations,
and the agentic timeline security model live in the companion doc,
[`AI_Engine_patch_safety.md`](AI_Engine_patch_safety.md). `AIChatComponent` and its logging rules
live in [`AI_Engine_chat_component.md`](AI_Engine_chat_component.md). Providers, accounts,
conversation history, and quota mechanics live in
[`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md).

- [1. Overview](#1-overview)
- [2. Architecture](#2-architecture)
- [3. Communication Pattern](#3-communication-pattern)
- [4. Key Functionality](#4-key-functionality)

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

    Source layout (`Source/AI/AIStateMapper/`, split by concern — no file over 1,000 lines):

    | Unit | Concern |
    |------|---------|
    | `AIStateMapper.h` | Class declaration, shared across every unit below |
    | `AIStateMapper.cpp` | Module factory access, graph ↔ JSON mapping (`graphToJSON`/`applyJSONToGraph`), `createModule` |
    | `AIStateMapperValidation.cpp` | `validatePatch`/`validateNodeParams` — the untrusted-input security boundary |
    | `AIStateMapperSnapshots.cpp` | Undo/redo snapshot restore (`applySnapshotPreservingNodes`) |
    | `AIStateMapperSchema.cpp` | AI-facing schema generation (`getPatchSchema`, `getPatchSchemaWithTimelineOps`, `getTimelineOpsEnvelopeSchema`) |
    | `AIStateMapperInternal.h` | Private helpers shared by two or more units above (the module factory map, `isInternalOnlyModule`, `mirrorUuidIntoProcessor`, etc.) — not part of the public API, never included outside this directory |

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

`AIChatComponent::PatchCard` (`Source/UI/Assistant/AIChatComponent.cpp`) shows a human-readable preview as
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

Tests: `Tests/AI/PatchDiffTests.cpp` — pure `computeDiff` cases, `summarizePatch`/`groupChangesByKind`
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
sync is NOT Pro-gated — `POST /v1/feedback` has no plan check server-side either, so any account
may submit general feedback. P6-17 extended this further: the sync now fires whenever
`FeedbackSettingsTab` has an `accountService` attached at all, whether or not the user is signed
in. `AuthClient::submitGeneralFeedback` sets `Authorization: Bearer <accessToken>` only when
`accessToken` is non-empty (signed in); it always sets `X-Device-Id` from the `AuthClient`'s own
stable per-install device id (see `Source/Auth/DeviceIdStore.h`) when non-empty, so a signed-out
submission is still attributable server-side via that anonymous id rather than being dropped
client-side. The local log above is still written unconditionally regardless of sign-in state or
sync outcome.

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

Tests live in `Tests/AI/AIUndoTests.cpp`.

