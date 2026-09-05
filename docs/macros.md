# Macros

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
   un-ported, since the attenuverter can never itself be a member.

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

**Four** additions to `ModuleType` (§7 item 1, **implemented**), all **internal-only** with the
same three exclusions as `TimelineMidiSource` / `RecordTap` / `TimelineAudioSource`: no library
row, no replace-menu entry, never authorable by a model (`kNonAuthorableModuleTypes`, §6).

- `MacroInlet` — "Macro In". Audio/CV. One logical port in, one out; passes its input through
  unchanged.
- `MacroOutlet` — "Macro Out". Audio/CV, the mirror of MacroInlet.
- `MacroMidiInlet` — "Macro MIDI In". MIDI, zero audio channels.
- `MacroMidiOutlet` — "Macro MIDI Out". MIDI, the mirror of MacroMidiInlet.

MIDI is a **separate pair of node types**, not a "kind" flag on `MacroInlet`/`MacroOutlet` —
confirmed against the actual `TimelineMidiSourceModule`/`TimelineAudioSourceModule` code (the
Track In / Track Audio precedent this follows) rather than assumed from the type names. The
load-bearing reason there is a **construction-time channel-shape difference**:
`ModuleBase(name, 0, 0)` for a MIDI node vs `ModuleBase(name, N, N)` for an audio/CV one, which a
single type would have to branch its bus layout on — exactly what the fixed-channel-count
invariant makes awkward, since the layout is frozen the moment the constructor runs. It is *not*
because a MIDI `processBlock` and an audio `processBlock` inherently differ (here both bodies are
near-identical pass-through no-ops); it is specifically that a MIDI port carries no channel shape
at all — no Mono/Stereo/Poly-N — just `acceptsMidi()`/`producesMidi()`, like `ExternalMidiModule`
and `PolyMidiModule`.

All four are created by the **macro port flow** (§7 item 3, **done**), never by the module library
and never by the replace menu. An inlet/outlet always belongs to exactly one macro; it is a member
of that macro's `members` list like any other node, and is dissolved with it by an action that
actually deletes members (`GraphEditor::deleteMacroAndMembers`). **Ungrouping is not that
action** — it dissolves only the `Macro` record (and, with it, the `MacroPort` descriptors), never
the member nodes or their connections; see §5.4 for what that means for a cable landing on a port.

Each carries a user-visible **port name** ("Pitch In", "Wet Out") — this lives on the macro's own
`MacroPort` entry (§5.2), not in the node's extra state, since the name is macro-level
presentation and the port-creation flow (§7 item 3) is what actually wires a name to a node.

### 5.2 Port set and ordering

`synth::Macro` gains an ordered list of port descriptors (§7 item 2, **implemented**, including
`toVar`/`fromVar` and `retainOnly` reconciliation):

```cpp
enum class MacroPortKind { AudioCV, Midi };

struct MacroPort {
    juce::String nodeUuid;   // the MacroInlet/MacroOutlet (or MIDI variant) node this port fronts
    bool         isInput;
    juce::String name;
    int          order;      // draw order on the card, user-reorderable
    MacroPortKind kind;      // which pair of node types this port is (§5.1) — kept here too so a
                             // macro's port list can be rendered with no per-port graph lookup
};
```

Membership stays keyed by uuid, and every port's `nodeUuid` must also appear in the owning
macro's `members` — `MacroSet::fromVar` rejects a saved macro where that does not hold, and
`retainOnly`/`removeMemberEverywhere` drop a port the moment the member it fronts is removed, by
either path. The port list is derived state in the sense that the nodes are the truth, but the
**order, name and kind** are macro-level presentation and belong on the macro, next to
`name`/`colour`/`bounds`. `"ports"` is optional in a saved macro's JSON — absent parses as no
ports, which is what every macro saved by P8-12 already looks like.

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

Shape is therefore an **input to port creation**, supplied one of these ways:

- **From the configure-I/O modal** — the user picks Mono, Stereo or Poly-N when adding the port.
  This is the primary flow and the plain reading of "open a modal to configure inputs/outputs".
  `StereoCollapsed` (below) is never one of the choices here — hand-picking a stereo port always
  means the two-jack `Stereo` shape.
- **From a cable** — dragging a wire onto a collapsed card's boundary creates a port matching the
  cable's direction/kind, wired to the EXTERNAL end of the drag. A convenience path; the shape is
  genuinely known here, from the dragged cable's own poly/stereo fan. **Not yet implemented
  (P8-15b): the cable-drop path currently always creates Mono** — shape inference from the
  cable's fan is deferred, not ruled out; a future increment can read it the same way
  `resolvePolyLink`/`getJackTargets` already do elsewhere in `GraphEditor.cpp`. Until then the
  modal is the only way to get Stereo/Poly-N. This path never wires anything on the macro's
  INTERIOR side, regardless of shape inference ever landing — the macro is collapsed, so there is
  no member visible to pick a target from; only the modal, or a manual cable drawn after expanding
  the macro, connects a port to a specific member.
- **From nothing (a bare "Add Input"/"Add Output" in the modal)** — Mono, Stereo or Poly-N, picked
  explicitly; today the modal is the only way to reach Stereo/Poly-N, since the cable-drop path
  above is Mono-only for now.
- **From grouping a selection with a crossing cable** (founder-review fix G2/F5, §7 item 7) — the
  ONLY source of `StereoCollapsed`. `GraphEditor::buildMacroPortCrossingPlan` derives the shape
  from the internal jack the crossing cable actually lands on, never from a user choice.

Given a shape, the node is constructed so that the existing rules produce the right result with
no special-casing:

- **Mono** — raw ch0 is the node's one visible jack.
- **Stereo** — raw ch0 (Left) and a dedicated `kRightBase` raw channel (Right) are the node's two
  visible jacks — the split-block convention every other stereo-capable module in this codebase
  follows (never ch1) **when its two legs sit on SEPARATE visible jacks**, applied here even though
  a Macro In/Out has no CV on ch1 to protect, because the convention is about consistency across
  the codebase, not just this module's own channel layout. Unlike a real DSP module's Dual I/O
  toggle, there is no runtime collapse/expand affordance: the shape is fixed at creation (§5.3's
  own rule), so a Stereo port always shows both legs — a toggle that could hide one would itself be
  a form of "the shape changes after creation," which is exactly what this section rules out.
  `hasStereoOutputPairShape`/`hasDualIOParameter()` are NOT involved (they key off the module's
  fixed *raw* channel count, which a Macro In/Out never varies from `kMaxChannels`);
  `rightAudioLegChannel()` is overridden directly instead, which is what a peer module's own
  stereo-pair auto-complete (`completeStereoPairConnections`) actually reads. Reachable ONLY from
  the Configure I/O modal, and from `buildMacroPortCrossingPlan`'s merge pass for a Dual-I/O-ON
  crossing (§7 item 7) — both cases where the module being fronted genuinely shows two jacks.
- **StereoCollapsed** (founder-review fix G2, §7 item 7) — raw ch0 (head, `polyVoiceSpan` 2) and
  raw ch1 (follower) are CONTIGUOUS and both map to the SAME ONE visible jack. This is the OTHER
  stereo shape already in this codebase — `ModuleBase::mapStereoPairOutput`'s Dual-I/O-OFF branch,
  which every FX module (Delay, Reverb, ...) defaults to: one "Audio" jack whose raw ch0/ch1 are a
  contiguous pair, not two separately-jacked legs. The "never ch1" sentence above governs the
  two-jack `Stereo` shape, where a separately-jacked right leg on ch1 would collide with the CV
  ch1 already reserved on the split-block voice modules (Oscillator/Filter/VCA); it does not
  extend to a COLLAPSED pair, which is the FX pattern every collapsed stereo module in this
  codebase already uses on ch0/ch1, ports included. Contiguity here is load-bearing, not
  cosmetic: `GraphEditor::getJackTargets()` and every caller that expands a `JackTarget` walk
  `rawHeadChannel .. + voiceSpan - 1` assuming adjacency, so a span-2 head at ch0 paired with a
  follower on the SPLIT-block `kRightBase` (as `Stereo` uses) would make that expansion target
  ch1 — inactive under this shape — and leave the real second leg's cable unreachable from any
  jack: audible, but with no jack to unplug it from, exactly the "hidden cable" failure
  `dropRoutingsOnHiddenJacks`/`dropHiddenRightLegConnections` exist to prevent elsewhere.
  **Never selectable by hand** — the Configure I/O modal has no entry for it (§5.3's
  configure-I/O bullet below covers Mono/Stereo/Poly-N only); it exists purely so
  `GraphEditor::buildMacroPortCrossingPlan` can splice a port that mirrors an internal module's
  own collapsed jack (a founder-reported bug: grouping FX modules with one "Audio" jack each
  produced two-jack Stereo ports, mismatching the module they front). A port already carrying
  this shape still shows as "Stereo" in the Configure I/O modal (it IS a stereo pair), but the
  modal can never re-select it — picking any shape there, "Stereo" included, is a real,
  deliberate conversion to the two-jack shape, one undo step like any other shape change (§7
  item 5).
- **Poly-N** — every raw channel `0..voiceCount-1` maps to the SAME single visible jack, raw ch0
  marked `isPolyGroupHead` with `polyVoiceSpan == voiceCount` in its `LogicalPort` mapping, so a
  poly-bus wire crossing the boundary is drawn and counted as one poly bus rather than N separate
  cables.

A cable whose shape does not match the port it is dropped on is refused the same way any
mismatched connection is today — not silently adapted at the boundary.

**Implementation note (P8-15b, done).** `MacroInletModule`/`MacroOutletModule` use the
declare-a-maximum-and-vary-the-visible-count mechanism `Source/Modules/CLAUDE.md` documents for
Audio Input and Hosted Plugin: the node always carries `kMaxChannels` (8) raw channels for its
whole lifetime (never renegotiated — invariant (a), honoured exactly), and
`mapInputChannel`/`mapOutputChannel` overrides (identical in both directions — the node is a
symmetric pass-through) decide, from a `shape_`/`voiceCount_` pair set ONCE by
`setPortShape()` right after construction and persisted through `getExtraState()`/
`setExtraState()` (trusted-path only, like every module's `"state"`), which raw channels are
active and how they map to visible jacks. This is what let §7 item 3 add Stereo/Poly-N with **no
new factory type and no migration for a Macro In/Out already on disk**: a pre-P8-15b save has no
`"shape"` key at all and parses as Mono, exactly the shape it already behaved as.

**Rendering (founder-review fix F2, docs/macros.md §7 items 3/4).** A port node is a real
`ModuleComponent` — selection, hit-testing, cable drag/drop, undo and serialisation all key off
that, unchanged — but it no longer PAINTS or SIZES like an ordinary module card, expanded or
collapsed. `ModuleComponent::layoutMacroPortWidget()` sizes it purely from the shape's own visible
jack count: one row (`kMacroPortWidgetHeaderY + kMacroPortWidgetBottomPad`) for Mono, Poly-N,
`StereoCollapsed` (founder-review fix G2 — deliberately ONE row despite carrying two raw channels,
§5.3's `StereoCollapsed` bullet) or a MIDI port (all of which have exactly one visible jack a side,
a MIDI port's fanned Poly-N bus included), and a second row (`+ kMacroPortWidgetRowStep`) only for
the two-jack `Stereo` shape. See §5.4 for where that widget is PLACED.

### 5.4 Cable rendering across the boundary

`buildVisibleCables()` keeps ownership of the whole rule. Generalising what P8-12 already does:

| Case | Today (P8-12) | With ports (P8-15) |
| --- | --- | --- |
| Both endpoints inside one collapsed macro | dropped | dropped (unchanged) |
| Crosses a collapsed boundary, through a port | anchored to the card edge via `projectToRectEdge` | anchored to the **card jack** of the inlet/outlet it passes through |
| Crosses a collapsed boundary, straight to an interior member (no port) | anchored to the card edge via `projectToRectEdge`, facing the other endpoint | anchored to the card's **left edge if entering** the macro, **right edge if leaving** it (founder-review fix F3, below) — never facing-the-other-endpoint |
| Both endpoints outside | untouched | untouched |
| Expanded macro | untouched | untouched; inlets/outlets draw as the DOCKED port widget (founder-review fix F2, below) rather than an ordinary card |

A cable that crosses a collapsed boundary **without** going through an inlet/outlet — i.e. wired
straight to an interior member — keeps landing on the card's edge, but not via `projectToRectEdge`
any more. That case does not disappear and must not be treated as an error: a macro is a grouping
first and a black box second, and the user is allowed to wire into its guts.

**Founder-review fix F3: the no-port edge anchor is DIRECTIONAL, not facing.** The original
`projectToRectEdge` treatment picked whichever edge of the card's rectangle faced the cable's
other endpoint — a ray from the card's centre toward that endpoint, projected onto the rectangle's
perimeter. For a member wired straight through the boundary that reads as arbitrary: a founder
screenshot showed two cables, one entering and one leaving a collapsed macro, both landing on the
card's TOP edge at a single point, because the other endpoint of each happened to sit above the
card. The fix keys the edge off the cable's **direction relative to the macro**, exactly like a
real port jack already does (inputs on the left, outputs on the right):

- the macro is the cable's **source** (signal *leaving* it) → anchor on the card's **right** edge;
- the macro is the cable's **destination** (signal *entering* it) → anchor on the card's **left**
  edge.

The Y coordinate is still derived from the old facing projection — `projectToRectEdge` is still
called, just to read off a Y rather than a full point — so several crossing cables on the same
side keep spreading vertically instead of collapsing onto one pixel; that Y is then clamped into
the same vertical jack band a real port's jack lays out in (`kMacroCardJackBandTop`/`Bottom`). X
lands exactly on the card's boundary (`cardBounds.getX()`/`getRight()`), not inset the way a real
port jack is (`kMacroCardJackInsetX`), so a no-port edge anchor never sits under a case-(a) port
dot occupying the same side.

The consequence is worth stating plainly, because it bounds what a macro is:
**a macro's encapsulation is advisory, not enforced.** Ports are the *intended* interface, not the
*only* one. A macro is a reusable building block by convention and by what the UI makes easy — it
is not a sealed unit, and no part of the engine will stop a cable reaching past its boundary.
Enforcing encapsulation would require the interior to be a genuinely separate graph, which is
Candidate A and was rejected in §4.

**Implemented (P8-15c; edge-anchor case redirected by F3).** The collapsed card draws one jack per
configured `MacroPort` (`GraphEditor::macroCardPortLayout`) — inputs evenly spaced down the left
edge, outputs down the right, in `order`. That layout is the ONE definition three places read:
`MacroCardComponent::paint` draws the dot, `GraphEditor::endConnectionDrag` hit-tests a drop
against it (`macroCardPortForPoint`), and `rebuildVisibleCables()` anchors a boundary cable through
a port at that same jack. A cable straight to an interior member instead gets the directional edge
anchor above, per the table above — the two treatments coexist and are told apart by which
endpoint node the cable resolves to, a port's fronting node or an ordinary member.

Dropping a cable exactly on an existing port's jack wires straight into that port's node, in place
of always minting a fresh one; a jack whose direction or kind does not match the drag is refused,
the same as a mismatched drop on an ordinary module jack. Missing that jack still falls through to
the whole-card "shape from a dropped cable" convenience (§5.3).

**Ungrouping a macro does not drop a cable landing on one of its ports.** `ungroupSelection()`
calls `MacroSet::remove`, which only erases the `Macro` record — no member node and no graph
connection is touched. A port's fronting node (`MacroInlet`/`MacroOutlet` or a MIDI variant) is an
ordinary member like any other, so it survives ungrouping exactly as the rest of the group does,
its connections (both the internal pass-through wiring and any external cable wired into it)
intact; the node's `ModuleComponent` simply becomes visible again (no collapsed card left to hide
it behind), and the cable that used to anchor at the card jack now renders as an ordinary cable to
that node's own jack. This follows from §1's framing — ungrouping, like grouping, is a
presentation-only change to the `MacroSet`, never a graph edit — but is worth stating explicitly
here because the opposite (the cable silently disappearing) is the more intuitive-sounding
assumption and is not what happens.

**The docked port widget (founder-review fix F2).** Founder review on the first Macro I/O cut:
*"We're now creating separate modules for each i/o, instead I had in mind a small widget like i/o
on the top left/right of the macro box"* — and separately, that a port's chosen name *"is not
shown on the module UI that's presented; ... it should be presented."* Both are rendering/placement
fixes, not a model change: the four proxy node types, `MacroPort`, and "a port is a real member of
its macro" are all exactly as §4/§5.1-§5.3 decided.

- **Presentation.** `ModuleComponent::paintMacroPortWidget()` draws no header chrome and no body —
  a small row tinted with the owning macro's `colour`, its jack(s) (§5.3's rendering note) and the
  port's own `MacroPort::name`, resolved LIVE every paint via
  `GraphEditor::macroPortOwnerFor(nodeId)` (walks the node's uuid to its macro, then to the
  `MacroPort` fronting it) rather than cached on the node — a rename in the Configure I/O dialog is
  therefore reflected on the very next repaint, with nothing to invalidate. The same name is drawn
  next to the matching jack on the COLLAPSED card too (`MacroCardComponent::paint`, reading the
  identical `name` field off `GraphEditor::macroCardPortLayout`), elided if the card is too narrow —
  an expanded and a collapsed macro read a port's name the same way.
- **Placement.** `GraphEditor::dockMacroPortWidgets()` — called at the end of every
  `updateComponents()` pass, and again after a single-module drag settles (`finalizeModuleDrag`) —
  positions each EXPANDED macro's port widgets against `macroHullBounds()`: inputs down the LEFT
  edge, outputs down the RIGHT, both starting near the top, ordered by `MacroPort::order` — the
  SAME order `macroCardPortLayout()` already uses for the collapsed card, so a port's expanded
  position and its row in the Configure I/O dialog never disagree. A widget's canvas position is
  therefore fully DERIVED, never independently dragged — `ModuleComponent::mouseDown` refuses to
  arm a body drag or a selection-click for a macro-port node (mirroring the Attenuverter's own "no
  header, nothing to click" early return), so nothing on the canvas can desync it from the hull.
- **The hull-feedback trap.** `macroHullBounds()` unions only NON-PORT members — a port's own
  fronting node is excluded, because if it counted toward the bounds that DEFINE the hull, docking
  it against that hull would grow the hull, which would push it out again, every layout pass. A
  macro made ENTIRELY of ports (no ordinary member) has nothing left to union; that case falls back
  to the macro's own persisted `bounds` (the same footprint its collapsed card uses) rather than
  leaving its ports with no edge to dock against. `applyMacroCollapsed`'s own "seed the collapsed
  card at the current member bounding box" union (§1's expand/collapse transition) excludes port
  members for the identical reason — folding a docked-left input widget into that union would seed
  the collapsed card noticeably left of where the real members sit, growing worse with every
  additional input port.
- **Group drag stays consistent, but not for free — `finalizeSelectionDrag()` re-docks
  explicitly.** A WHOLE-macro drag (the collapsed card, or a selection built through
  `selectMacro()`) moves every member — ports included — by the identical delta via
  `beginSelectionDrag`/`dragSelectionBy`/`finalizeSelectionDrag`, exactly as any other multi-select
  drag; because that delta (and `finalizeSelectionDrag`'s own snap/de-overlap offset) is uniform
  across the whole group, the hull moves by the same amount the ports just moved by, so their
  hull-relative offset is preserved automatically in that case. A PARTIAL selection has no such
  guarantee: a marquee can catch a port widget together with some unrelated module without the
  macro's other (non-port) members, in which case the hull does not move by the delta the port
  just moved by — that would desync it. `finalizeSelectionDrag()` therefore calls
  `dockMacroPortWidgets()` unconditionally at the end (idempotent — a no-op for the whole-macro
  case, which already agrees; a real correction for the partial-selection case), the same as
  `updateComponents()`/`finalizeModuleDrag()` above.
- **Not individually selectable.** A port widget's only UI surface is its jack (cable drag/drop,
  unchanged) and the Configure I/O dialog (rename/reorder/delete/shape-change, unchanged) — never a
  canvas click on its body. This was a deliberate scope call, not an oversight: making it
  selectable would need its own drag suppression logic distinct from "part of a macro-wide
  selection," for a widget whose whole point is that its position is not the user's to set.

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

### 5.8 The macro menu's reachable entry points

`GraphEditor::buildMacroMenu` is the ONE builder for a macro's own actions (Expand/Collapse,
Rename, Change Colour, Configure I/O, Bypass/Mute, Save as Snippet, Ungroup, Delete Macro &
Modules). There are three ways to reach it, all funnelling through it rather than each keeping its
own copy:

1. **The collapsed card's own right-click** (`MacroCardComponent::showContextMenu`) — the original
   P8-12 entry point. It pre-selects nothing itself before this fix; it passes its own inline-rename
   callback as `buildMacroMenu`'s `renameAction` override, which the other two entry points below
   don't have a card to host. Its "Ungroup"/"Save as Snippet..." semantics change with this fix —
   see below.
2. **Right-clicking empty canvas inside an expanded macro's hull** (`GraphEditor::mouseDown` ->
   `macroHullAt()`), reachable without collapsing first (P8-12 follow-up). This site calls
   `selectMacro(hullMacroId, false)` before showing the menu — see below for why, and why that call
   is now redundant but left in place.
3. **Right-clicking a MEMBER MODULE itself** (`ModuleComponent::buildModuleContextMenu`,
   founder-review item 4: *"Right clicking a module in the macro should also show the macro
   options"*) — appends `buildMacroMenu()`'s items as a "Macro: `<name>`" submenu on the module's
   own right-click menu, when and only when the clicked module resolves to a macro via
   `MacroSet::findByMember()`. A module in no macro sees no change to its menu at all. This site
   deliberately does **not** pre-select the macro at click time: a member module's own right-click
   retargets the *module* selection (so its own Copy/Duplicate/Delete Module items act on what was
   clicked), and macro-wide selection only happens if and when a macro submenu item is invoked.

Only entry point 2 pre-selected the macro before this fix; entry points 1 and 3 did not. That
matters because `buildMacroMenu()`'s "Ungroup" and "Save as Snippet..." act on the **current
selection** (`ungroupSelection()` / `onSaveSnippetRequested`), not on the macro id the menu was
built for — so `buildMacroMenu()` itself now selects that macro (`selectMacro(macroId, false)`)
immediately before running either one, rather than leaning on each caller to have pre-selected it.
This is a real behaviour change for entry point 1 (the collapsed card), not just plumbing for the
new entry point 3: previously, right-clicking one collapsed card while *other* macros' cards were
also selected and choosing "Ungroup" would dissolve every selected macro, not just the one
right-clicked; it now dissolves only the card that was actually clicked, matching what a user
reading "Ungroup" off that card's own menu would expect. Entry point 2's own `selectMacro()` call —
`GraphEditor::mouseDown`'s own comment calls it "load-bearing, not cosmetic" because
`mouseUp` deliberately preserves whatever was selected on a right-click (so the canvas menu's Paste
keeps working), and without it "Ungroup"/"Save as Snippet..." would silently act on whatever was
selected before the click — is now redundant, since `buildMacroMenu()` handles that itself; it is
left untouched here to keep this fix's diff scoped to the actual bug, not to make a UI-visible
statement. Without this fix, invoking "Ungroup" from a member's macro submenu (entry point 3) while
a *different* macro's module was also selected would dissolve both macros instead of only the one
whose submenu was opened — `Tests/MacroContainerTests.cpp`'s
`MacroMemberContextMenu.UngroupFromTheSubmenuDissolvesTheRightMacroDespiteAMixedSelection` pins
this exact case.

**Testing entry point 3 headlessly.** The `MacroMemberContextMenu` suite drives a real synthesised
right-click `juce::MouseEvent` into `ModuleComponent::mouseDown()` — the actual gesture/hit-test/
retarget entry point, not `buildModuleContextMenu()` called cold — because testing the layer beneath
`mouseDown()` cannot catch a broken hit-test. That real body right-click branch calls
`juce::PopupMenu::showMenuAsync()`, which opens a genuine popup and, on a headless (no-display) Linux
CI runner, segfaults inside `juce::PopupMenu::HelperClasses::MenuWindow::getParentArea` — surviving on
macOS/Windows is exactly why this shipped green locally and red only in CI. `ModuleComponent` fixes
this with the same seam `TimelineTrackHeaderComponent` already uses for its own non-headless picker:
a `showContextMenuHook_` member (defaulting to the real `showMenuAsync()` call) that mouseDown()'s
right-button branch calls instead of showing the menu directly, plus a public
`setShowContextMenuHookForTest()` a test uses to capture the menu the real gesture built without ever
opening it. This is strictly stronger than asserting against a second, separately-built
`buildModuleContextMenu()` call: the test now inspects the exact menu object the click itself
produced. `MacroMemberContextMenu.RightClickFiresTheContextMenuHookExactlyOnce` guards the wiring
itself, so a future revert to a direct `showMenuAsync()` call fails there first rather than only as an
unexplained Linux-only segfault.

---

## 6. AI authorability

**Decision: a model may not author a macro, a macro port, or an inlet/outlet node. This does not
change in P8-15.**

`validatePatch` already refuses `"macros"` outright on the untrusted path, and the reasoning in
`AIStateMapper.cpp` holds unchanged for ports: macro membership is keyed by node uuid, and a
provider-supplied `uuid` is ignored (`adoptUuidIfTrusted`), so provider-authored macro data could
never resolve to anything real even if it were let through. Refusing it keeps that true
regardless of future changes.

`MacroInlet`, `MacroOutlet`, `MacroMidiInlet` and `MacroMidiOutlet` join the internal-only set and
get the **same three exclusions**: no library row, no replace-menu entry, **never authorable by a
model**. **Done as of P8-15a** (§7 item 1).

The enforcement point already exists and is cheap: `kNonAuthorableModuleTypes` in
`AIStateMapper.cpp` is an explicit name set with a reason recorded against each entry, consulted
by `isInternalOnlyModule`. Registering a module in `moduleFactory` makes it model-authorable **by
default**, and the resulting allowlist is pinned by
`AIStateMapperTest.AuthorableModuleTypesGolden` — so adding the four Macro I/O types to the
factory (which they need, so our own saves round-trip them) *fails the build* until they are
deliberately added to the non-authorable set too. P8-15a added all four entries with their
reason, not just the factory rows — this was the golden test's *intended* failure mode, not a
regression to fix around.

**This must not be achieved by relaxing anything.** `validatePatch` is the security boundary for
untrusted model output; the rule stands that validity is fixed on the *generation* side, most
upstream first (schema → bounded retry → narrow repair → prompt), measured with
`Tools/AIPatchHarness`. If a future goal is "the AI can build a macro", the correct shape is an
app-side **tool/action** the model invokes — the app authors the macro from a validated node set
— never a `"macros"` key the model writes directly.

---

## 7. What P8-15 implements

In order, each independently shippable:

1. **DONE (P8-15a).** `MacroInlet` / `MacroOutlet` / `MacroMidiInlet` / `MacroMidiOutlet` module
   types + the three internal-only exclusions + the `validatePatch` rejection and its test. Audio/
   CV vs MIDI is two node-type pairs, not a "kind" flag (§5.1). `MacroInlet`/`MacroOutlet` already
   use the declare-max/vary-visible channel mechanism (§5.3's implementation note) so a later
   Stereo/Poly-N does not need a factory or format change.
2. **DONE (P8-15a).** `MacroPort` on `synth::Macro` (with a `kind` distinguishing audio/CV from
   MIDI ports, §5.2), its `toVar`/`fromVar` round trip, and its `retainOnly` reconciliation (a
   port whose node died is dropped like any other member — and so is one dropped singly via
   `removeMemberEverywhere`).
3. **DONE (P8-15b; presentation reworked by founder-review fix F2).** The port-creation flow —
   folded into ONE "Configure I/O" modal together with item 5 below, per an explicit founder
   request rather than piecemeal menu actions (`GraphEditor::promptConfigureMacroIO`,
   `synth::ui::MacroPortConfigDialog`). Picks Mono/Stereo/Poly-N/MIDI at creation (§5.3), writes
   the port's user-visible name (§5.1), and also covers the "shape from a dropped cable"
   convenience (`GraphEditor::createMacroPortFromDroppedCable`, Mono-only — see §5.3).
   `estimateModuleSize` in `GraphEditor.cpp` has an entry for all four types, sized to
   `ModuleComponent`'s compact docked-widget geometry (`kMacroPortWidgetWidth`/
   `kMacroPortWidgetHeaderY`/`kMacroPortWidgetBottomPad`, §5.3's "Rendering" note) rather than a
   full 280-wide card — F2 replaced the "real rendered card" this used to measure against, since a
   port node no longer renders as one (`MacroPortWidget.MonoPortWidgetSizeMatchesEstimateModuleSize`).
   Placement is no longer free: a newly-created port's widget is DOCKED to its macro's hull the
   moment `updateComponents()` runs (item 4's "Placement" below), not stacked below the card via
   `resolvePlacement()` as it briefly was pre-F2.
4. **DONE (P8-15c); collapsed-card jacks now carry names, and F2 added the expanded-macro
   equivalent.** Card jacks: the collapsed card draws one jack per port
   (`GraphEditor::macroCardPortLayout`), `buildVisibleCables()` anchors boundary cables to them,
   and a cable dropped exactly on an existing port's jack wires straight into it (§5.4).
   Founder-review fix F2 (item 3 of that review — "it's not shown on the module UI...it should be
   presented") added the port's `MacroPort::name` next to its jack on the collapsed card
   (`MacroCardComponent::paint`, elided if too narrow) and, for an EXPANDED macro, a compact
   docked widget per port — `GraphEditor::dockMacroPortWidgets()` positions it against
   `macroHullBounds()` (inputs down the left edge, outputs down the right, ordered by
   `MacroPort::order`, the same order the collapsed card uses) and
   `ModuleComponent::paintMacroPortWidget()` draws its name and jack(s), resolved live through
   `GraphEditor::macroPortOwnerFor()`. See §5.3/§5.4 for the full design and the hull-feedback trap
   this had to avoid.
5. **DONE (P8-15b).** Port rename and reorder — the same modal as item 3
   (`GraphEditor::renameMacroPort`/`moveMacroPortOrder`). Reorder is scoped to one direction at a
   time (inputs against inputs, outputs against outputs), a no-op at either edge. Changing an
   existing port's SHAPE (Mono/Stereo/Poly-N) is also reached from this modal
   (`GraphEditor::changeMacroPortShape`): per §5.3's immutability rule this is delete-node +
   create-node + rewire underneath, landed as ONE undo step
   (`AppUndoManager`'s `GraphAndMacroSnapshotAction` — see its class comment for why the combined
   graph+macro restore has to be a single action, not two pushed into one transaction, to get
   both undo AND redo right). A saved cable on a raw channel the new shape no longer exposes is
   dropped, not adapted (dropRoutingsOnHiddenJacks' rule, applied honestly). **UI redesign
   (founder-review fix, F1):** the modal's per-row "Apply Shape" button is gone — picking a new
   shape in the row's combo box, or typing a new poly voice count and pressing Return/losing
   focus, commits immediately (`MacroPortConfigDialog`'s combo `onChange` / voice-count
   `onFocusLost` call `onChangePortShape` directly). Only the UI gesture collapsed from two steps
   to one; the underlying delete+create+rewire is still exactly one undo step as above. Rows also
   group under "Inputs"/"Outputs" section headers, a MIDI row hides its shape controls entirely
   (no shape/poly concept applies, §5.1) instead of showing them disabled, the poly voice count is
   labelled and only shown for a Poly shape, Up/Down/Delete are compact glyph buttons instead of
   full-width text buttons, and the dialog sizes itself to its content (clamped, with the row list
   scrolling past the clamp) instead of a fixed-size box.
6. **DONE (P8-15d).** Bypass/mute fan-out (§5.6): `GraphEditor::setMacroBypassed`/`setMacroMuted`
   call `ModuleBase::setBypassed`/`setMuted` — already `setValueNotifyingHost` parameter writes —
   on every member as ONE undo step (`recordStructuralChange`, the same before/after graph-JSON
   snapshot `applySmartSuggestions` uses to batch several connections into one step); no new
   mutation mechanism, and every member's own `processBlock` keeps honouring the two-branch
   bypass/mute contract unchanged. Mute additionally skips (never crashes on) a member with no
   "muted" parameter (`ModuleBase::hasMuteParameter()` — Macro In/Out and their MIDI variants
   among them, the pre-existing gap §7 item 1 flagged). `macroBypassState`/`macroMuteState` give
   the tri-state (`AllOff`/`AllOn`/`Mixed`) read the "mixed-state members show an indeterminate
   indicator" requirement calls for; the collapsed card (`MacroCardComponent::paint`) draws it as
   two small badges next to the expand chevron — solid when `AllOn`, half-filled when `Mixed`, and
   undrawn (matching an un-pressed per-module bypass/mute button) when `AllOff` — read fresh on
   every paint and explicitly repainted by both setters, since a collapsed card's own members are
   hidden and it has no parameter listener of its own to notice the change otherwise.
   `toggleMacroBypassed`/`toggleMacroMuted` (wired into `buildMacroMenu` as "Bypass Macro"/
   "Mute Macro") converge a `Mixed` or `AllOff` state to ON and an `AllOn` state to OFF, mirroring
   `toggleSelectionMacrosCollapsed`'s own convergence rule.
7. **DONE (founder-review fix F5).** Auto-create ports for a crossing cable when grouping, behind
   a remembered preference. Founder verdict: *"When gathering the modules into the macro for the
   first time, they should automatically create i/o ports for modules that have cables going in or
   out of the macro,"* and *"There should be a modal asking if you prefer this way, or just leave
   the cables as is. After the user selects a preference it is saved and he can later change it in
   the preferences menu."*

   **The preference (`GraphEditor::MacroAutoPortPreference`)** is a TRI-STATE, not a bool — `Unset`
   (ask on the next group with a crossing cable), `AutoCreatePorts`, `LeaveCablesAsIs` — DEFAULT
   `Unset`. Persisted through `juce::ApplicationProperties` by `PreferencesSettingsTab` exactly the
   way it persists its own keys (`"macroAutoCreatePorts"`, one of `"ask"`/`"auto"`/`"leave"`; a
   getter/setter pair and a `Macro auto-ports:` combo row in the Preferences tab, including "Always
   ask" to go back). `GraphEditor::requestGroupSelectionIntoMacro()` — what Cmd+G
   (`groupOrToggleSelectionMacros`), the right-click "Create Macro from N Modules" menu item, and
   the drag-group canvas menu all call now, instead of `groupSelectionIntoMacro()` directly — shows
   `synth::ui::MacroAutoPortPromptDialog` (in `MacroPortConfigDialog.{h,cpp}`, alongside the
   Configure I/O dialog it shares a translation unit with rather than a new `Source/UI/*.cpp` pair)
   ONLY when the preference is `Unset` AND the selection actually has a crossing connection — a
   grouping with nothing to decide is never interrupted. Two buttons ("Create Ports" / "Leave
   Cables As Is") plus a "Remember my choice" toggle (default ON); the choice persists (through
   `propertiesFile_`, the same seam the macro recolour favourites shelf already uses, since this
   modal can fire before a Settings window — and therefore a `PreferencesSettingsTab` — has ever
   been constructed) only when remember is checked, and applies once otherwise. Follows the
   async-modal idiom already used for Configure I/O (`DialogWindow::LaunchOptions::launchAsync` +
   a callback, never a blocking modal loop); `GraphEditor::macroAutoPortModalForTest` is a test
   seam that replaces the real dialog launch with a callback a test drives directly.

   **The crossing-cable gate (`selectionHasCrossingMacroCable`) reads live NodeIDs, never
   uuids.** A module freshly dropped on the canvas has no `"uuid"` property yet (only lazily
   assigned on first save, or by `groupSelectionIntoMacro()` itself once it decides to proceed) —
   gating the check on resolvable uuids would silently under-detect on the single most common real
   path: drop two never-saved modules, wire one to an existing module, group immediately. So
   `buildMacroPortCrossingPlan()` takes `std::vector<NodeID>` as its primary overload (a thin
   `std::vector<juce::String>` uuid-resolving wrapper remains for `groupSelectionIntoMacro`, which
   by the time it calls it has already assigned every member a uuid). The gate also mirrors
   `groupSelectionIntoMacro`'s own "already in a macro" refusal (`macros.findByMember`) before
   computing a plan — a selection that grouping will refuse outright has nothing to decide, so the
   modal must not ask a question whose answer can never be applied.

   **The splice itself** — `GraphEditor::groupSelectionIntoMacro(bool autoCreatePorts)` (the
   original zero-crossing-port behaviour is `autoCreatePorts = false`, still the signature every
   pre-existing caller and test compiles against unchanged). Before the macro exists,
   `buildMacroPortCrossingPlan()` reads `graph.getConnections()` the same way
   `rebuildVisibleCables()`/`AudioEngine`'s routing enumeration do — `LogicalPort::role`/
   `visibleJackIndex`/`isPolyGroupHead`/`polyVoiceSpan`, never a raw-channel guess — and groups
   every connection with exactly one endpoint among the about-to-be-members into a
   `MacroPortCrossingGroup`, keyed by **(internal node, direction, visible jack)** — this is the
   de-duplication key: two cables landing on the same internal jack (a collapsed stereo pair's two
   raw legs, or several external sources fanned into one jack) share ONE port. A group's shape is
   read off the jack's own head raw channel, never defaulted: `polyVoiceSpan > 1 && role == Audio`
   is `StereoCollapsed` — **founder-review fix G2**, corrected from an earlier `Stereo` here: a
   span-2 head means the internal jack is COLLAPSED (one visible jack for both raw legs, the FX
   pattern every Delay/Reverb/etc. defaults to), so the port must present that same one jack
   (§5.3's `StereoCollapsed` bullet), never the two-jack `Stereo` shape a hand-picked Configure
   I/O choice means — `polyVoiceSpan > 1` otherwise is Poly-N with that exact voice count;
   anything else is Mono; a MIDI connection is its own group kind entirely, yielding
   `MacroMidiInlet`/`MacroMidiOutlet`, never an audio port. A Dual-I/O-on module's Left/Right legs
   sit on two SEPARATE visible jacks rather than one span-2 jack (`polyVoiceSpan == 1` on each, so
   each starts life as its own Mono group here), so a second merge pass folds them into one
   `Stereo` group (the two-jack shape, correctly — the module they front genuinely shows two
   jacks) when a crossing connection reaches both — paired via `ModuleBase::rightAudioLegChannel()`,
   never jack index 0/1 (`Source/Modules/CLAUDE.md`), correct for the split-block (voice module)
   layout this merge pass exists for. `spliceMacroPorts()` then, for
   each group: disconnects every original external<->internal edge, constructs the port node with
   the derived shape/kind (named from the internal module's own name plus
   `getInputPortLabel`/`getOutputPortLabel` at the jack it fronts — "Filter Cutoff", never
   "Input 2"), adds it as a macro member with a `MacroPort` entry, and reconnects
   external->port->internal (or the reverse for an outlet) on exactly the raw channels the original
   edges used. All of this — `macros.add()` AND every splice — runs inside
   `groupSelectionIntoMacro`'s own ONE `recordGraphAndMacroChange` transaction (splice-before-
   `updateComponents()`, group-then-add-as-a-second-pass was rejected), so a single Cmd+Z undoes
   the grouping and every spliced port together.

   **A mod-routing knob's edge is spliced when it genuinely crosses the boundary, and left alone
   when it doesn't (founder-review fix G3).** `AudioEngine::addModRouting` always wraps a
   single-slot CV routing as source -> attenuverter(ch0) -> destination, and the attenuverter
   itself can never be a macro member — it never gets a `ModuleComponent`
   (`GraphEditor::updateComponents()` skips it outright), so it can never be part of a canvas
   selection. That means a crossing connection whose EXTERNAL endpoint is the attenuverter can mean
   two different things, told apart by looking at the mod chain's OTHER real endpoint (the
   attenuverter's other ch0 connection):

   - **Both real endpoints are about to become members** (the user selected the mod source and its
     target together, e.g. an ADSR and the VCA it drives). The attenuverter sitting nominally
     "outside" is then only an artefact of its own invisibility, not a real crossing — splicing
     here would spawn TWO spurious ports (an outlet off the source, an inlet onto the target) for a
     routing the user is grouping wholly inside the macro. Left un-ported, both edges, exactly like
     any other fully-internal connection (`MacroAutoPortTests.cpp`'s
     `ModRoutingWithBothRealEndpointsInsideStaysWhollyInternal`).
   - **The far endpoint is genuinely external** (or the chain is only half-wired) — this IS a real
     crossing, and now DOES get a port. `AudioEngine::getModulationRoutings()`'s AttenuverterChain
     pass is keyed purely on the ATTENUVERTER's own node identity — it walks every
     `AttenuverterModule` node and reads whichever connections currently sit on its ch0 in/out,
     never on what those connections point at — so retargeting the attenuverter's own edge onto the
     new port (with the attenuverter itself standing in as the "external" node for the splice) does
     NOT desync that classification: the routing still shows up in `getActiveModRoutings()` (the
     mod matrix), now reporting the port as its source/dest, exactly how any other boundary-crossing
     cable reports the port it passes through rather than the member further inside. The
     modulation signal keeps flowing because the port is a pure pass-through, and
     `buildVisibleCables()` keeps drawing the BOUNDARY segment as ONE `AttenuverterChain`-kind wire
     (ModCV-coloured, carrying the knob) all the way to the port's own jack — collapsed, that lands
     on the port's card jack via the same `macroCardPortLayout`/re-anchoring machinery every other
     ported cable already uses; nothing macro-specific needed adding to either function. The other,
     wholly-INTERNAL leg (member <-> port, on the outlet's side of the chain) is unaffected by this
     fix and renders as an ordinary `Direct` (plain Audio-coloured) cable when the macro is
     EXPANDED — visible only then, since a collapsed macro drops any cable wholly inside it; this
     was true of every other auto-created port before G3 too, and is not new here.
     `MacroAutoPortTests.cpp`'s `AttenuverterAdjacentCrossingIsSplicedForAGenuineExternalCrossing`
     and `AttenuverterAdjacentCrossingSplicedModulationSurvives` prove this holds down to the DSP
     level (the second renders real blocks through the spliced chain and checks the destination
     actually receives the modulated signal, and reacts live to an amount-parameter change, not
     just that a port node exists), and `UndoRestoresAModRoutingCrossingSpliceExactly` covers
     undo/redo.

     **The mod matrix's own destination combo needed a matching fix.** `MacroInletModule`
     deliberately declares no `getModulationTargets()` (`GraphEditor::connectPorts()` relies on that
     empty list to keep a plain cable drop onto a Macro In's jack a plain connection, never
     auto-wrapped in a fresh attenuverter — `Tests/MacroPortFlowTests.cpp`'s drop-a-cable tests pin
     that), so a `ModMatrixComponent::ModRow` whose destination is now a spliced port had nothing to
     resolve its combo selection against and would render blank. Rather than give
     `MacroInletModule` a real `ModulationTarget` (which would resurrect the auto-wrap problem for
     every ordinary cable drop, since `connectPorts()` reads that same list), `ModMatrixComponent`'s
     own combo-population (`destinationCandidatesForCombo`, `Source/UI/ModMatrixComponent.cpp`)
     substitutes a display-only synthetic target at channel 0 whenever the module has none AND is a
     `MacroInletModule` — a spliced port from this fix is always Mono with its one active channel at
     0 (the internal jack an `AttenuverterChain` lands on is never poly-fanned), so channel 0 is the
     only candidate. `Tests/ModMatrixTests.cpp`'s
     `DestinationLabelStillResolvesAfterGroupingSplicesAMacroPort` pins the row no longer going
     blank.

   **Hull/card ordering.** `macroHullBounds()`/`applyMacroCollapsed`'s hull-seeding union already
   excludes port members (§5.4's "hull-feedback trap"), and the collapsed card's own `macro.bounds`
   is seeded from the ORIGINAL (pre-port) selected members' bounds before any splicing happens — so
   there is nothing for the splice to retroactively distort. What the splice DOES have to get right
   is running before `updateComponents()`, which is what lays out the card jacks
   (`macroCardPortLayout`) and docks the expanded hull's port widgets
   (`dockMacroPortWidgets`) against however many ports now exist — `groupSelectionIntoMacro` calls
   `spliceMacroPorts()` and `updateComponents()` in that order, inside the one transaction, never
   the reverse.

   **OPEN QUESTION, raised for the founder rather than decided silently: what should Ungroup do
   with an auto-created port?** This fix's answer: **nothing** — `ungroupSelection()` is untouched,
   and per §5.4 it already dissolves only the `Macro` record, never a member node or its
   connections, so an auto-created port's fronting node survives ungrouping exactly like any other
   member, its connections (external->port and port->internal, both) intact; the node's
   `ModuleComponent` simply becomes visible again as an ordinary card sitting mid-signal-chain.
   Signal keeps flowing either way, which is the one hard requirement, but a "Macro In"/"Macro Out"
   box left behind in the middle of a plain patch is a stray artefact a user did not ask for and
   has no obvious next step for (delete it and reconnect by hand? leave it forever?). The
   alternative — teach `ungroupSelection()` to splice an AUTO-CREATED port back OUT on ungroup,
   reconnecting external straight to internal, as one undo step — was NOT implemented here, for two
   reasons: (1) `MacroPort` carries no provenance field distinguishing "auto-created by this fix"
   from "added by hand through Configure I/O," so telling them apart would need a new persisted
   field this stage did not add; (2) it changes what Ungroup means for every macro, not just ones
   this feature touched. This is exactly the "least surprising, still not obviously right" case the
   task called out — the current behaviour (ports survive as ordinary members) is what ships;
   splice-out-on-ungroup is the documented alternative for the founder to weigh in on.

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
