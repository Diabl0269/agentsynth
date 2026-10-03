#Patch Preview

What the chat card shows for a proposed patch before the user presses Apply, and why it is computed
the way it is.

## The card

A proposed patch is shown on the chat's one card, `AIChatComponent::EditPlanCard`
(`Source/UI/Assistant/AIChatComponent/AIChatComponentEditPlanCard.cpp`), whatever else the answer
carries ([chat component](chat-component.md#one-answer-one-card)). Its preview is computed once, when
the message is created, by `AIIntegrationService::previewProjectEdit`, which runs the patch phase on a
scratch copy and words it as one line (`describePatchPhase`): a merge as what it changes ("Merges a
patch that adds 1 module, changes 2 parameters"), from `computeDiff`; a replace as what the new patch
contains ("Replaces the patch with 3 modules and 2 connections"), from `summarizePatch`.

The card's **Show details** panel holds the per-change list behind that line, from the same two
snapshots (`ProjectEditResult::patchBefore`/`patchAfter`):

- **Merge mode** (stable node identity): one line per change, "+ Reverb", "Filter: Cutoff 400 ->
  800", "+ mod LFO -> Filter Cutoff", grouped by `PatchChange::Kind` (`groupChangesByKind`) so adds,
  removes and changes are not interleaved.
- **Replace mode**: "New patch: 12 modules" followed by each node's type name, plus a connection
  count if any. **Never a diff against the old graph**; see below.

Below the list the panel shows the plan's JSON, pretty-printed (`juce::JSON::toString(parsed,
allOnOneLine=false)`), falling back to the raw string if it fails to parse. A patch the scratch
apply rejects never reaches a diff: the card says "This plan was rejected and was not applied:
<reason>" and offers no Apply.

## The diff comes from two graph snapshots, never the raw patch JSON

This is the whole point, not an implementation detail. Diffing the patch JSON against the live graph
would misreport three things the patch itself never states:

- merge mode auto-connects a new audio node with no outgoing wire to Audio Output, and a new
  MIDI-accepting node to an existing MIDI source (`applyJSONToGraph`'s `autoConnectNewNodes`);
- replace mode deletes every node the patch does not restate;
- the untrusted apply path rescales a value in `[0,1]` that is not a legal value of a wider parameter
  range (`AIStateMapper::applyParamsToProcessor`'s normalized-value heuristic: a cutoff of 0.5 on
  20..20000 Hz becomes 10010 Hz), so the value that lands is not the value the patch states. A value
  the range can hold (an ADSR decay of 0.2 s on 0..5, a drive of 1.0 on 1..10) lands exactly.

`AIIntegrationService::computePatchPreview(jsonString, mergeMode, before, after)` produces the two
snapshots without touching the live graph:

- `before` is `AIStateMapper::graphToJSON(audioGraph)` for replace mode. For merge mode it is
  `graphToJSON` of a scratch graph immediately after `replayLiveGraphTrusted()` — the live graph
  replayed onto scratch, trusted, `clearExisting=true` — rather than a second direct call on
  `audioGraph`. Both must travel the *same* param round-trip (denormalize,
  `setValueNotifyingHost`, renormalize, including `snapToLegalValue` on a skewed or interval range)
  or an untouched node on a skewed range, such as `LFOModule`'s `rateHz`, shows a phantom
  `ParamChanged` purely from replay rounding. Guarded by
  `PatchDiffIntegrationTest.NoOpPatchWithSkewedParamProducesEmptyDiff`.
- `after` is `graphToJSON` of that same scratch graph once the untrusted candidate patch has
  actually been applied to it (`applyJSONToGraph(json, scratch, clearExisting, trusted=false)`) —
  the exact scratch-graph construction `applyPatch()`'s own `PatchEval` regression check uses, with
  `replayLiveGraphTrusted()` as the shared piece.
- It mirrors `applyPatch()`'s mode-less-patch repair (a replace that only validates as a merge is
  applied as a merge), so the previewed diff matches what Apply or Merge will actually do.
- It never mutates `getLastPatchError()`, `getLastPatchErrorCode()` or `didLastPatchRepairMode()`:
  it runs entirely against a scratch graph and must not clobber the error state from a previous real
  Apply attempt still on screen. Guarded by
  `PatchDiffIntegrationTest.ComputePatchPreviewDoesNotClobberLastPatchError`.
- It returns `false`, with `before` and `after` still populated, if the candidate patch fails
  validation or application on the scratch graph; a caller must then show no diff, since `after`
  would otherwise reflect the unapplied, pre-patch state — misleadingly "everything removed" for a
  failed replace-mode patch, whose scratch starts empty. (The chat card goes through
  `previewProjectEdit`, which refuses such a plan with the reason instead.)

## `PatchDiff`

`Source/AI/PatchDiff.h`'s `computeDiff(before, after)` is a pure function over two
`graphToJSON`-shaped `juce::var` snapshots, returning `std::vector<PatchChange>` (`NodeAdded`,
`NodeRemoved`, `ParamChanged`, `ConnectionAdded`, `ConnectionRemoved`, `ModulationAdded`,
`ModulationRemoved`), each renderable via `describe()`.

- **Node identity is the `uuid` field, not the integer `id`.** Merge mode's trusted replay preserves
  both `id` and `uuid` for nodes it recreates one-for-one from the live graph (`applyJSONToGraph`'s
  `preservedId` and `adoptUuidIfTrusted`, trusted-path only), and the subsequent untrusted patch
  apply never reassigns a matched node's `uuid`. A brand-new node has no `uuid` in `before`, so it
  never spuriously matches. Connection and modulation endpoints reference nodes by `id`, which is
  meaningful only within one snapshot, so they are resolved to `uuid` via each snapshot's own
  `id -> uuid` map before comparing.
- **Replace mode has no stable node identity between snapshots.** `applyJSONToGraph` preserves `id`
  and `uuid` on the trusted path only, and a replace-mode apply is always untrusted, so `computeDiff`
  over a replace-mode pair reports the entire prior graph removed and the entire new patch added,
  even where a node is conceptually unchanged. That is technically correct — every processor really
  is destroyed and recreated on replace — but useless to someone reviewing a brand-new patch, which
  is why the edit-plan card uses `summarizePatch()` for replace mode instead. `computeDiff` itself stays
  mode-agnostic and correct for any snapshot pair;
this is a note about how the UI uses it,
    not a limitation of the function.-
        **`graphToJSON` already collapses attenuverter chains into a `modulations` array **,
    scanning
  `AttenuverterModule` nodes and their wires,
    so `computeDiff` diffs `modulations` directly rather than pattern -
            matching attenuverter plumbing itself.It excludes `type ==
        "Attenuverter"` nodes from the node diff and any connection with an Attenuverter endpoint from the connection
            diff,
    so a modulation change is reported exactly once rather than also as raw node and connection noise.One known omission
    : an attenuverter wired on only one side produces no `modulations` entry in
  `graphToJSON` at all
    , so it is silently dropped from the diff instead of shown as add / remove noise — deliberate
    , since a half - wired attenuverter only arises from a malformed patch.-
              Only a node's `params` object is diffed, never `position`, `state`, `id` or `uuid`, so a merge -
              mode patch that repositions or
          re - lists an unrelated existing node does not read as moved or
          changed noise.A parameter's display name comes from a throwaway, never-processed instance of its module
              type(`AIStateMapper::createModule`)
    , falling back to the raw param id when not found;
numeric values render at roughly 3 significant figures, with no units,
    because no per - module unit -
        formatting table exists in this codebase.

        Two smaller pure helpers in the same header are used only by the UI
    :

    -**`summarizePatch(after)`**returns a `PatchSummary` — the node type list in snapshot node order
    , plus a non - attenuverter connection count — read from a single snapshot.This is what replace
          - mode cards render.- **`groupChangesByKind(changes)`**stable
          - sorts a `computeDiff` result by `PatchChange::Kind`
    , whose declaration order already matches the desired grouping
    , so changes sharing a kind keep
  `computeDiff`'s original relative order. Used only for merge-mode rendering; `computeDiff`' s own output order is
          untouched and is still what its tests assert on.

          Tests : `Tests
          / AI / PatchDiffTests.cpp` — pure `computeDiff` cases
    , `summarizePatch` and
`groupChangesByKind` coverage
    , plus two regression tests(`MergeModeAutoWireAppearsInDiff`,
`UntrustedRescaleShowsLandedValueNotRawPatchValue`) proving the snapshot diff catches what a raw - patch - versus
          - live - graph diff would miss.
