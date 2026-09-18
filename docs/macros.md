# Macros

This doc is split across three files: this one covers what shipped (P8-12) and the decided Macro
I/O model (P8-14) — §1-4 below. Port mechanics (node types, port ordering, poly/stereo shape,
cable rendering, latency, bypass/mute, hosted plugin, the macro menu's reachable entry points) are
in [`macros_ports.md`](macros_ports.md) (§5). AI authorability, the P8-15 implementation tracker,
and explicitly out-of-scope items are in [`macros_implementation.md`](macros_implementation.md)
(§6-8).


A **Macro** is a named, coloured, collapsible grouping of graph nodes on the canvas.

This document covers two things:

1. **What shipped (P8-12)** — the presentation-only container that exists today.
2. **The Macro I/O design (P8-14)** — the decided model for how a Macro gains configurable
   inputs and outputs, which **P8-15 implements**. §3-6 are the design; §7 tracks implementation
   progress against it and is the accurate record of what actually exists — all six items are
   built as of P8-15d, and a founder review afterwards produced a further round of presentation
   fixes layered on top without reopening the model itself (§5.3/§5.4, §7 items 3-5): a port no
   longer renders as a full module card, collapsed OR expanded — see "How a port is drawn" below.
   The same review also added a menu-reachability fix (§5.8): a macro member module's own
   right-click menu now offers the macro's actions too, not just the collapsed card and the
   expanded hull. A further founder-review pass (fix F5, §7 item 7) added the biggest behaviour
   change since P8-15d: grouping a selection that has a cable crossing its boundary can now
   auto-create matching ports for those cables instead of leaving them wired straight to an
   interior member, behind a remembered tri-state preference. A second founder-review pass on that
   feature (fix G3, §7 item 7) taught the auto-port splice to also handle a crossing modulation/CV
   cable wrapped in a hidden `AttenuverterModule` node — genuinely external ones now get a port too;
   only the case where BOTH the real mod source and target are being grouped together stays
   un-ported, since the attenuverter can never itself be a member. That same second pass also
   shrank the docked port widget itself (fix G4, §5.3's "Rendering" note) after founder feedback
   that it read as a small module rather than a boundary jack, and added a visible collapse button
   to the EXPANDED hull (fix G5, §5.8) — the same asymmetry the P8-12 collapsed card's own expand
   chevron was built to fix, now closed on the other side of the toggle. That same pass also fixed
   every user-facing member COUNT/LIST to report modules rather than raw graph membership (fix G6,
   §7 item 4 note) — a port node is a genuine `Macro::members` entry (load-bearing, per §5.1), but
   it is not a module the user put in the box, and the two are different quantities. A third
   founder-review pass, raised after using the shipped feature, decided the one open question the
   second pass had left for him: **Ungroup now removes every one of a macro's ports and splices
   the cable each one proxied straight back together** (fix G7, §7 item 8), so Group then Ungroup
   is a true round trip, and a port node also gained its own right-click "Delete Port" affordance
   for the case where its macro is otherwise still alive. Founder-review item P8-29 (T148, §7 item
   9) then extended auto-porting past grouping time: dragging a cable across an EXPANDED macro's
   boundary now mints and fully wires a matching port on the fly, and a port whose last cable is
   disconnected (`disconnectCable`/`disconnectPort`) is now auto-deleted, dissolving its macro too
   if it was the last member — both behaviours are Preferences toggles, default ON.

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
"a cable is not a graph edge" (`docs/layout/cables.md`).

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
`docs/layout/cables.md`: cables are enumerated through
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
  (`docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step`) would need a notion of "resolve a uuid through a container", which
  is a change to the addressing model itself, not to macros.
- **Undo.** `recordGraphAndMacroChange` snapshots the graph and the `MacroSet`. Grouping would
  become a *graph topology change* (nodes leaving one graph for another) rather than a metadata
  change, so every group/ungroup is a full structural rewrite.
- **Hosted plugin.** The plugin build's `AudioEngine` graph is our own inner graph already
  (`docs/architecture/plugin-layer.md#latency-compensation`). Nesting again is not fatal but compounds the latency point above.

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
