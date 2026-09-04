# Macros

A **Macro** is a named, coloured, collapsible grouping of graph nodes on the canvas.

This document covers two things:

1. **What shipped (P8-12)** — the presentation-only container that exists today.
2. **The Macro I/O design (P8-14)** — the decided model for how a Macro gains configurable
   inputs and outputs, which **P8-15 implements**. This half is a *design*, not a description of
   working code; nothing in section 3 onward exists yet.

---

## 1. What exists today (P8-12)

A Macro is **visual and organisational only**. It adds no ports, no graph edges, and no
processing of its own. Concretely:

- `synth::Macro` (`Source/MacroSet.h`) is `{ id, name, colour, collapsed, bounds, members }`,
  where `members` is a list of **node uuids** — never `juce::AudioProcessorGraph::NodeID`, which
  is only valid for the lifetime of one loaded graph.
- `synth::MacroSet` is the live set for the current patch, owned by `GraphEditor`, serialised by
  `ProjectBundle` under the `"macros"` key alongside the graph, timeline and `PatchDocument`.
- The model is **flat**: a node in a macro cannot be grouped into a second one. `Cmd+G` refuses
  that with a status message rather than doing something ad hoc.
- Collapsed, a macro draws as a `MacroCardComponent` and its member `ModuleComponent`s are
  hidden. Expanded, it draws as a dashed hull around the union of its members' bounds.
- Selection, drag and delete are **not** a parallel mechanism: selecting a macro selects its
  members in the ordinary `SelectionModel`, so the existing group-drag and delete paths do the
  work unchanged.

Cables that cross a collapsed macro's boundary are re-anchored to the card's edge by
`rebuildVisibleCables()`; cables wholly inside one collapsed macro are dropped from the visible
set. This is a **rendering** rule — the underlying graph edges are untouched, consistent with
"a cable is not a graph edge" (`docs/layout_selection_canvas.md` §3).

`"macros"` is a **reserved patch-format key**: `validatePatch` refuses it outright on the
untrusted path (`PatchValidationError::MacrosNotAllowed`). See §8.

---

## 2. The problem P8-14 has to decide

Today a macro is a box drawn around modules. What it is *not* is a thing you can patch into.
You cannot wire "the macro's input" to anything; you wire to a specific member module, and the
macro boundary is a fiction the renderer maintains. For a macro to be a reusable building block —
the point of grouping in the first place — it needs its own named ports.

Three documented invariants constrain any answer, and they are the reason this is the only
architecturally hard item in P8:

**(a) A module's channel count is fixed for its lifetime.**
`Source/Modules/CLAUDE.md`: variable-port modules declare a **maximum** and vary only the
*visible* count (`getVisiblePortCount`), because the graph's render sequence bakes each node's
bus layout in. A container whose ports the user defines cannot simply grow channels at runtime.

**(b) A cable is not a graph edge.**
`docs/layout_selection_canvas.md` §3: cables are enumerated through
`GraphEditor::buildVisibleCables()`, which already applies macro-aware treatment. Anything
crossing a boundary is a rendering decision, not a graph one.

**(c) Nodes are addressed by uuid, everywhere.**
Timeline bindings, automation lanes (`synth::resolveLaneParameter`), the AI patch format and the
undo system all address a node by its persistent uuid (`ModuleBase::setNodeUuid`, mirrored once
and never rewritten). Any model that hides nodes inside an opaque container has to answer what a
lane bound to a now-interior node resolves to.

And there is nothing to build on. The engine is a **single flat `juce::AudioProcessorGraph`**.
Grepping `subgraph` / nested graph / `containerModule` across `Source/` and `docs/` returns
nothing; no processor owns an inner graph. `MacroControlModule` is a **modulation knob bank**,
not a container — the name is a false friend and must not be allowed to mislead the
implementation.

---

## 3. Candidates

### Candidate A — a real container node

A `ModuleBase` subclass that owns an inner `juce::AudioProcessorGraph`, with member nodes moved
*into* it, and its `mapInputChannel` / `mapOutputChannel` mapping its outer logical ports onto
inner endpoints. Conceptually the clean answer: a macro genuinely becomes a module.

**Where it breaks:**

- **Channel counts (invariant a).** A user-configurable port set is exactly the runtime channel
  growth the fixed-count rule forbids. Declaring a generous maximum (say 16 in / 16 out) and
  varying only the visible count is technically possible, but it makes every macro carry 32
  channels of buffer regardless of use, and it caps macro I/O at a compile-time constant chosen
  now.
- **Latency compensation.** `juce::AudioProcessorGraph` derives parallel-path compensation delays
  from each node's `latencySamples`, and only re-derives them when the render sequence is
  rebuilt — this is why `MainComponent::rebuildGraphForLatencyChange()` exists for hosted
  plugins. An inner graph creates a **second, nested compensation domain**: the container must
  report its inner graph's total latency outward and force an outer rebuild on every inner
  change. Every latency bug we already have with hosted plugins, we would now have recursively.
- **uuid addressing (invariant c).** Member nodes leave the main graph. `AudioEngine`'s lane
  resolution walks the main graph's nodes; every timeline binding and automation lane pointing at
  a member would orphan on group and un-orphan on ungroup. `MainComponent`'s reconcile pass
  (`docs/architecture.md` §8) would need a notion of "resolve a uuid through a container", which
  is a change to the addressing model itself, not to macros.
- **Undo.** `recordGraphAndMacroChange` snapshots the graph and the `MacroSet`. Grouping would
  become a *graph topology change* (nodes leaving one graph for another) rather than a metadata
  change, so every group/ungroup is a full structural rewrite.
- **Hosted plugin.** The plugin build's `AudioEngine` graph is our own inner graph already
  (`docs/architecture.md`). Nesting again is not fatal but compounds the latency point above.

### Candidate B — proxy inlet/outlet nodes, flat graph

The macro **stays a presentation layer over a flat graph** (exactly what P8-12 built) and gains
two internal-only module types, **Macro Inlet** and **Macro Outlet**. A macro's ports *are* its
inlet/outlet nodes. Cables from outside land on an inlet node; the inlet passes signal through
unchanged to whatever member it feeds inside.

Precedent exists and is load-bearing: `TimelineMidiSource` ("Track In"), `TimelineAudioSource`
("Track Audio") and `RecordTap` are already internal-only node types created by a *flow* rather
than by the library, each excluded three ways — no library row, no replace-menu entry, never
authorable by a model (see the `ModuleType` enum comments in `Source/Modules/ModuleBase.h`).
Macro Inlet/Outlet are the same shape of thing.

**How it answers each constraint:**

- **(a) Channel counts.** Each inlet/outlet is a *separate node* with a fixed, small channel
  count decided at construction. Adding a port to a macro adds a **node**, it does not grow an
  existing node. The fixed-count rule is never touched, and macro I/O has no compile-time cap.
- **(b) Cables.** The boundary stays a rendering concept, which is what
  `buildVisibleCables()` already implements. Collapsed-card anchoring generalises from "project
  onto the card's edge" to "anchor at the card jack for that inlet/outlet" — a refinement of
  existing code, not a new mechanism.
- **(c) uuid addressing.** Nothing moves. Members keep their uuids in the same graph; timeline
  bindings and automation lanes resolve exactly as they do today, collapsed or expanded. Group
  and ungroup stay metadata changes.
- **Latency.** One compensation domain, unchanged. An inlet/outlet is a pass-through reporting
  zero latency, so it adds nothing to compensate.
- **Undo/serialisation.** `recordGraphAndMacroChange` already snapshots both halves together.
  Adding a port is a graph change *and* a macro change in one step — precisely the case that
  function exists for.

**What it costs:** the boundary is a UI fiction rather than a DSP one. A macro is not a real
processor, so it cannot be bypassed, muted or latency-reported *as a unit* by the engine — those
have to be defined in terms of its members (§6). Two extra nodes per port is also a small real
cost in the render sequence.

---

## 4. Decision

> **Candidate B — proxy inlet/outlet nodes on a flat graph.**

The tiebreaker is not aesthetics. It is that **B preserves uuid addressing and never varies a
live module's channel count**, and A violates both. Those two are not local design choices inside
the macro feature; they are the addressing model the timeline, automation, undo, the AI patch
format and the hosted-plugin layer are all built on. A macro feature is not worth changing them
for.

The latency argument is the second, independent reason: a nested `AudioProcessorGraph` is a
nested compensation domain, and we already know from hosted plugins how sharp that edge is.

Candidate A is not permanently ruled out — if a future feature genuinely needs a subgraph as a
DSP unit (a per-voice macro, say), it should be revisited then, on its own merits, with the
addressing question answered first.

---

## 5. The design

### 5.1 Node types

Two additions to `ModuleType`, both **internal-only** with the same three exclusions as
`TimelineMidiSource` / `RecordTap` / `TimelineAudioSource`:

- `MacroInlet` — "Macro In". One logical port in, one out; passes its input through unchanged.
- `MacroOutlet` — "Macro Out". Same, in the other direction.

They are created by the **macro port flow**, never by the module library and never by the
replace menu. An inlet/outlet always belongs to exactly one macro; it is a member of that macro's
`members` list like any other node, and is dissolved with it.

Each carries a user-visible **port name** ("Pitch In", "Wet Out") in its extra state — this is
the name that appears on the collapsed card's jack and in the expanded hull's edge label.

### 5.2 Port set and ordering

`synth::Macro` gains an ordered list of port descriptors:

```
struct MacroPort {
    juce::String nodeUuid;   // the MacroInlet / MacroOutlet node
    bool         isInput;
    juce::String name;
    int          order;      // draw order on the card, user-reorderable
};
```

Membership stays keyed by uuid. The port list is derived state in the sense that the nodes are
the truth, but the **order and names** are macro-level presentation and belong on the macro,
next to `name`/`colour`/`bounds`.

### 5.3 Poly and stereo — a port's shape is chosen at creation and then fixed

This is the constraint most likely to be got wrong, so it is stated as a rule.

**A port node's channel shape is decided when the node is constructed, and is immutable for that
node's lifetime.** Changing a port from mono to stereo means deleting the port and adding a new
one — it never means widening a live node. That is not a limitation worked around; it *is*
invariant (a) applied honestly, because a port node is an ordinary module and the fixed-channel
rule binds it like every other module.

There is deliberately **no shape inheritance** here. Inheritance is the right rule for Dual I/O on
a real DSP module (`ModuleBase::hasStereoOutputPairShape` derives it from channel counts, never
from a per-module registration) and the wrong frame for a node we construct on demand: a port is
created *before* it is wired, so at construction time there is nothing to inherit from.

Shape is therefore an **input to port creation**, supplied one of two ways:

- **From the configure-I/O modal** — the user picks Mono, Stereo or Poly-N when adding the port.
  This is the primary flow and the plain reading of "open a modal to configure inputs/outputs".
- **From a cable** — dragging a wire onto a collapsed card's boundary creates a port whose shape
  is that of the cable being dropped. A convenience path; the shape is genuinely known here.

Given a shape, the node is constructed so that the existing rules produce the right result with
no special-casing:

- **Mono** — 1-in/1-out.
- **Stereo** — constructed with the split-block layout so the second audio leg sits on a
  dedicated `kRightBase` block, never on ch1. `hasStereoOutputPairShape` then reports true for it
  exactly as it does for an Oscillator or Filter, and the Dual I/O affordance appears through the
  ordinary inherited path.
- **Poly-N** — declares its voice span and marks the lowest raw channel `isPolyGroupHead` in its
  `LogicalPort` mapping, so a poly-bus wire crossing the boundary is drawn and counted as one
  poly bus rather than N separate cables.

A cable whose shape does not match the port it is dropped on is refused the same way any
mismatched connection is today — not silently adapted at the boundary.

### 5.4 Cable rendering across the boundary

`buildVisibleCables()` keeps ownership of the whole rule. Generalising what P8-12 already does:

| Case | Today (P8-12) | With ports (P8-15) |
| --- | --- | --- |
| Both endpoints inside one collapsed macro | dropped | dropped (unchanged) |
| Crosses a collapsed boundary | anchored to the card edge via `projectToRectEdge` | anchored to the **card jack** of the inlet/outlet it passes through |
| Both endpoints outside | untouched | untouched |
| Expanded macro | untouched | untouched; inlets/outlets draw as ordinary nodes inside the hull |

A cable that crosses a collapsed boundary **without** going through an inlet/outlet — i.e. wired
straight to an interior member — keeps today's `projectToRectEdge` treatment. That case does not
disappear and must not be treated as an error: a macro is a grouping first and a black box
second, and the user is allowed to wire into its guts.

The consequence is worth stating plainly, because it bounds what a macro is:
**a macro's encapsulation is advisory, not enforced.** Ports are the *intended* interface, not the
*only* one. A macro is a reusable building block by convention and by what the UI makes easy — it
is not a sealed unit, and no part of the engine will stop a cable reaching past its boundary.
Enforcing encapsulation would require the interior to be a genuinely separate graph, which is
Candidate A and was rejected in §4.

### 5.5 Latency

An inlet/outlet reports **zero latency** and does no buffering. The macro adds nothing to
compensate, the graph stays one compensation domain, and
`MainComponent::rebuildGraphForLatencyChange()` needs no change. A macro containing a
high-latency hosted plugin is compensated exactly as that plugin is today, because it is the same
flat graph it always was.

### 5.6 Bypass and mute

A macro is not a processor and therefore has no `processBlock` and no bypass/mute state of its
own. "Bypass macro" and "Mute macro" are **fan-out commands** over its members, applied as one
undo step:

- **Mute macro** → `ModuleBase::setMuted(true)` on every member. Each member's own `processBlock`
  honours the contract as written (`buffer.clear()` then return).
- **Bypass macro** → `ModuleBase::setBypassed(true)` on every member. Each member's own dry
  pass-through applies.

Both setters already exist and are **parameter writes** (`setValueNotifyingHost` on
`bypassedParam` / `mutedParam`), so the fan-out is an ordinary parameter change: it is
automatable, host-visible and undoable through the paths that already handle parameter changes.
No new mutation mechanism is introduced.

This is deliberate: the bypass/mute contract is per-module and stated in full in `CLAUDE.md`
precisely because it is cheap to follow and catastrophic to get wrong. A macro-level
reinterpretation of it would be a second implementation of the most safety-critical rule in the
codebase. Fanning out to the existing per-module implementations means there is nothing new to
get wrong.

A macro whose members are in mixed bypass states shows an indeterminate indicator; the command
sets them all to the same state.

### 5.7 Hosted plugin

Nothing macro-specific. Because the graph stays flat and macros are canvas presentation, a macro
in the plugin build behaves as it does in the app: it is state in the same `project.json` blob
the plugin already round-trips (`docs/architecture.md`, plugin state format). The plugin editor's
`GraphEditor` renders cards and hulls identically. No new host-mode branch.

The one existing rule that still applies: an over-wide hosted plugin is **refused, never
truncated** — grouping one into a macro changes nothing about that.

---

## 6. AI authorability

**Decision: a model may not author a macro, a macro port, or an inlet/outlet node. This does not
change in P8-15.**

`validatePatch` already refuses `"macros"` outright on the untrusted path, and the reasoning in
`AIStateMapper.cpp` holds unchanged for ports: macro membership is keyed by node uuid, and a
provider-supplied `uuid` is ignored (`adoptUuidIfTrusted`), so provider-authored macro data could
never resolve to anything real even if it were let through. Refusing it keeps that true
regardless of future changes.

`MacroInlet` and `MacroOutlet` join the internal-only set and get the **same three exclusions**:
no library row, no replace-menu entry, **never authorable by a model**.

The enforcement point already exists and is cheap: `kNonAuthorableModuleTypes` in
`AIStateMapper.cpp` is an explicit name set with a reason recorded against each entry, consulted
by `isInternalOnlyModule`. Registering a module in `moduleFactory` makes it model-authorable **by
default**, and the resulting allowlist is pinned by
`AIStateMapperTest.AuthorableModuleTypesGolden` — so adding Macro In / Macro Out to the factory
(which they need, so our own saves round-trip them) *fails the build* until they are deliberately
added to the non-authorable set too. P8-15 must add both entries with their reason, not just the
factory rows.

**This must not be achieved by relaxing anything.** `validatePatch` is the security boundary for
untrusted model output; the rule stands that validity is fixed on the *generation* side, most
upstream first (schema → bounded retry → narrow repair → prompt), measured with
`Tools/AIPatchHarness`. If a future goal is "the AI can build a macro", the correct shape is an
app-side **tool/action** the model invokes — the app authors the macro from a validated node set
— never a `"macros"` key the model writes directly.

---

## 7. What P8-15 implements

In order, each independently shippable:

1. `MacroInlet` / `MacroOutlet` module types + the three internal-only exclusions + the
   `validatePatch` rejection and its test.
2. `MacroPort` on `synth::Macro`, its `toVar`/`fromVar` round trip, and its `retainOnly`
   reconciliation (a port whose node died is dropped like any other member).
3. The port-creation flow: "Add Input / Add Output" on the macro menu, with shape inherited per
   §5.3 and refusal-with-status when it cannot be.
4. Card jacks: the collapsed card draws one jack per port, and `buildVisibleCables()` anchors
   boundary cables to them (§5.4).
5. Port rename and reorder.
6. Bypass/mute fan-out (§5.6).

## 8. Explicitly out of scope

- **Nested macros.** The flat model stands. A macro inside a macro is refused with a status
  message, as it is today.
- **A macro as a saveable, reusable library item.** Snippets already cover "save this group and
  paste it again" (`SnippetManager`); a macro-as-preset is a different feature.
- **Per-voice / polyphonic macro instancing** (one macro instantiated per voice). This is the
  case that would justify revisiting Candidate A, and it should be designed then, not
  anticipated now.
- **Macro-level parameter exposure** (a knob on the card driving a member's parameter). That is
  what `MacroControlModule` already does, and conflating the two would make the false friend in
  §2 permanent.

---

## Related

- `docs/layout_selection_canvas.md` — selection, group drag, cable interaction, the P8-12 macro
  container's canvas behaviour
- `docs/architecture.md` — the flat graph, latency compensation, plugin state format
- `docs/modules.md` / `Source/Modules/CLAUDE.md` — channel-count and Dual I/O rules
- `docs/modulation.md` — logical-port API, poly-bus wires
- `docs/AI_Engine.md` — `validatePatch`, the untrusted path, reserved patch-format keys
