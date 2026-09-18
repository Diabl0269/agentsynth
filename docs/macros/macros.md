# Macros

A **Macro** is a named, coloured, collapsible grouping of graph nodes on the canvas, plus its own
configurable set of inputs and outputs. This doc is the area hub: what a macro is, the model it is
built on, and why its ports are proxy nodes rather than a nested graph. The mechanics live in the
sibling docs:

- [`docs/macros/ports.md`](ports.md) — port node types, the port list, shape rules, how a port is
  drawn, cable rendering across the boundary, latency, bypass and mute.
- [`docs/macros/configure-io.md`](configure-io.md) — the Configure I/O dialog: adding, renaming,
  reordering, recolouring and reshaping a port.
- [`docs/macros/auto-ports.md`](auto-ports.md) — every path that creates or removes a port without
  the user asking for one by hand.
- [`docs/macros/menu-and-membership.md`](menu-and-membership.md) — the macro menu's entry points and
  every way membership changes.

---

## What a macro is

A macro is **presentation and organisation over a flat graph**, plus proxy port nodes. It adds no
processing of its own and no graph edges beyond the ones its ports carry.

- `synth::Macro` (`Source/MacroSet.h`) is `{ id, name, colour, collapsed, bounds, members, ports }`,
  where `members` is a list of **node uuids** — never `juce::AudioProcessorGraph::NodeID`, which is
  only valid for the lifetime of one loaded graph.
- `synth::MacroSet` is the live set for the current patch, owned by `GraphEditor` and serialised by
  `ProjectBundle` under the `"macros"` key alongside the graph, timeline and `PatchDocument`.
- Collapsed, a macro draws as a `MacroCardComponent` and its member `ModuleComponent`s are hidden.
  Expanded, it draws as a dashed hull around the union of its members' bounds.
- Selection, drag and delete are **not** a parallel mechanism: selecting a macro selects its members
  in the ordinary `SelectionModel`, so the existing group-drag and delete paths do the work
  unchanged.

Cables that cross a collapsed macro's boundary are re-anchored to the card's edge or to a port's own
card jack by `rebuildVisibleCables()`; cables wholly inside one collapsed macro are dropped from the
visible set. This is a **rendering** rule — the underlying graph edges are untouched, consistent with
"a cable is not a graph edge" ([`docs/layout/cables.md`](../layout/cables.md)).

## The macro model

**The model is flat.** A node in one macro cannot be grouped into a second one; `Cmd+G` refuses that
with a status message rather than doing something ad hoc. A macro inside a macro is refused the same
way.

**Nodes are addressed by uuid, everywhere.** Timeline bindings, automation lanes
(`synth::resolveLaneParameter`), the AI patch format and the undo system all address a node by its
persistent uuid (`ModuleBase::setNodeUuid`, mirrored once and never rewritten). Macro membership and
every `MacroPort` are keyed the same way.

**`"macros"` is a reserved patch-format key.** `validatePatch` refuses it outright on the untrusted
path (`PatchValidationError::MacrosNotAllowed`) — see [AI authorability](#ai-authorability) below and
[`docs/ai/patch-format.md`](../ai/patch-format.md).

**Undo covers the graph and the macro set together.** `AppUndoManager::recordGraphAndMacroChange`
snapshots both halves in one action, which is what lets a single `Cmd+Z` undo a grouping and every
port it spliced. `AppUndoManager`'s `GraphAndMacroSnapshotAction` is one action rather than two
pushed into one transaction, so redo restores both halves as well as undo does.

## Macro ports are proxy nodes on a flat graph

A macro's ports **are** its inlet and outlet nodes: four internal-only module types
(`MacroInlet`, `MacroOutlet`, `MacroMidiInlet`, `MacroMidiOutlet`) that pass signal through
unchanged. A cable from outside lands on an inlet node; the inlet feeds whatever member is wired to
it inside. The graph stays a single flat `juce::AudioProcessorGraph`.

**Why proxy nodes and not a container node that owns an inner graph.** Three invariants this
codebase is built on decide it, and a real container node breaks all three:

- **(a) A module's channel count is fixed for its lifetime.** `Source/Modules/CLAUDE.md`:
  variable-port modules declare a **maximum** and vary only the *visible* count
  (`getVisiblePortCount`), because the graph's render sequence bakes each node's bus layout in. A
  container whose port set the user defines is exactly the runtime channel growth that rule forbids;
  declaring a generous maximum instead would make every macro carry that many channels of buffer
  regardless of use and cap macro I/O at a compile-time constant. A proxy port is a *separate node*
  with its own fixed, small channel count decided at construction, so adding a port adds a node
  rather than growing one, and macro I/O has no compile-time cap.
- **(b) A cable is not a graph edge.** Cables are enumerated through
  `GraphEditor::buildVisibleCables()`, which already applies macro-aware treatment, so a boundary
  stays a rendering decision. Collapsed-card anchoring generalises from "project onto the card's
  edge" to "anchor at the card jack for that inlet or outlet" — a refinement of existing code, not a
  new mechanism.
- **(c) Nodes are addressed by uuid, everywhere.** With proxy ports nothing moves: members keep
  their uuids in the same graph, so every timeline binding and automation lane resolves exactly as it
  does outside a macro, collapsed or expanded, and group and ungroup stay metadata changes plus a
  port splice. A container node instead moves member nodes out of uuid-addressable space, orphaning
  every binding and lane that points at one on group and un-orphaning it on ungroup — a change to the
  addressing model itself, not to macros.

**Latency is the second, independent reason.** `juce::AudioProcessorGraph` derives parallel-path
compensation delays from each node's `latencySamples` and only re-derives them when the render
sequence is rebuilt, which is why `MainComponent::rebuildGraphForLatencyChange()` exists for hosted
plugins. An inner graph would create a second, nested compensation domain that has to report its
total latency outward and force an outer rebuild on every inner change. A proxy port reports **zero
latency** and does no buffering, so the graph stays one compensation domain and a macro adds nothing
to compensate.

**What this costs.** The boundary is a UI fiction rather than a DSP one. A macro is not a real
processor, so it cannot be bypassed, muted or latency-reported *as a unit* by the engine — those are
defined in terms of its members ([`docs/macros/ports.md`](ports.md#bypass-and-mute)). Two extra nodes
per port is also a small real cost in the render sequence. And **a macro's encapsulation is advisory,
not enforced**: ports are the *intended* interface, not the only one, and nothing in the engine stops
a cable reaching past the boundary straight to an interior member.

**The precedent this follows is load-bearing.** `TimelineMidiSource` ("Track In"),
`TimelineAudioSource` ("Track Audio") and `RecordTap` are already internal-only node types created
by a *flow* rather than by the module library, each excluded three ways: no library row, no
replace-menu entry, never authorable by a model (see the `ModuleType` enum comments in
`Source/Modules/ModuleBase.h`). The four macro port types are the same shape of thing.

## AI authorability

**A model may not author a macro, a macro port, or an inlet or outlet node.**

`validatePatch` refuses `"macros"` outright on the untrusted path. Macro membership is keyed by node
uuid and a provider-supplied `uuid` is ignored (`adoptUuidIfTrusted`), so provider-authored macro
data could never resolve to anything real even if it were let through; refusing it keeps that true
regardless of future changes.

All four port types are in `kNonAuthorableModuleTypes`
(`Source/AI/AIStateMapper/AIStateMapperInternal.h`), an explicit name set with a reason recorded
against each entry and consulted by `isInternalOnlyModule`. **Registering a module in
`moduleFactory` makes it model-authorable by default**, and the resulting allowlist is pinned by
`AIStateMapperTest.AuthorableModuleTypesGolden` — so adding a type to the factory (which the port
types need, so our own saves round-trip them) *fails the build* until it is deliberately added to the
non-authorable set too. That failure is the golden test's intended mode, not a regression to work
around.

**This is never achieved by relaxing anything.** `validatePatch` is the security boundary for
untrusted model output; validity is fixed on the *generation* side, most upstream first (schema,
bounded retry, narrow repair, prompt), measured with `Tools/AIPatchHarness`
([`docs/ai/patch-safety.md`](../ai/patch-safety.md)). If the goal becomes "the AI can build a macro",
the correct shape is an app-side **tool or action** the model invokes, with the app authoring the
macro from a validated node set — never a `"macros"` key the model writes directly.

## Deliberate limits

- **Nested macros.** The flat model stands; a macro inside a macro is refused with a status message.
- **A macro is not a saveable library item.** Snippets already cover "save this group and paste it
  again" (`SnippetManager`); a macro-as-preset is a different feature.
- **No per-voice macro instancing.** One macro instantiated per voice is the case that would justify
  a real container node as a DSP unit, and it is designed then — with the uuid-addressing question
  answered first — rather than anticipated now.
- **No macro-level parameter exposure.** A knob on the card driving a member's parameter is what
  `MacroControlModule` already does. `MacroControlModule` is a **modulation knob bank, not a
  container** — the name is a false friend, and conflating the two would make it permanent.

## Related

- [`docs/layout/macro-cards.md`](../layout/macro-cards.md) — the collapsed card's own canvas
  behaviour.
- [`docs/layout/selection.md`](../layout/selection.md) · [`docs/layout/cables.md`](../layout/cables.md)
  — selection, group drag and cable interaction.
- [`docs/architecture.md`](../architecture.md) — the flat graph, latency compensation, plugin state
  format.
- [`docs/modules/modules.md`](../modules/modules.md) · `Source/Modules/CLAUDE.md` — channel-count
  rules; [`docs/modules/fx-modules.md#stereo-io-dual-io-toggle`](../modules/fx-modules.md#stereo-io-dual-io-toggle)
  — Dual I/O rules.
- [`docs/modules/modulation.md`](../modules/modulation.md) — the logical-port API and poly-bus wires.
- [`docs/mixer/mixer.md`](../mixer/mixer.md) — a mixer channel is a macro boxing a Channel Strip.
