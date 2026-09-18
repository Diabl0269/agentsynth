# Macros: Port Mechanics

The proxy inlet/outlet port design for the decided Macro I/O model (P8-14, §5 below): node types,
port set and ordering, poly/stereo shape rules, cable rendering across the macro boundary,
latency, bypass/mute, hosted-plugin behaviour, and the macro menu's reachable entry points. The
P8-12 presentation-only container and the decision that led here are in [`macros.md`](macros.md)
(§1-4); the AI-authorability decision, the P8-15 implementation tracker, and out-of-scope items
are in [`macros_implementation.md`](macros_implementation.md) (§6-8).

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
actually deletes members (`GraphEditor::deleteMacroAndMembers`). **Ungrouping still never deletes
an ordinary module** — but as of founder-review fix G7 (§7 item 8) it DOES remove every one of the
macro's own port nodes, splicing back the cable each one proxied; a port is a boundary jack, not a
module the user put in the box, and once the boundary (the macro) is gone a port node has no
meaningful state left to preserve. See §5.4/§7 item 8 for the full splice-out design.

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
    int          order;      // draw order on the card, user-reorderable (drag OR Cmd+Up/Cmd+Down
                             // on a row control — T152/T153, §7 item 5)
    MacroPortKind kind;      // which pair of node types this port is (§5.1) — kept here too so a
                             // macro's port list can be rendered with no per-port graph lookup
    std::optional<juce::Colour> colour; // T152; unset (every pre-T152 save) falls back to the
                                        // kind tint (accent for AudioCV, audioWire for Midi)
                                        // everywhere a port paints — the row swatch, the
                                        // collapsed card's jack dot
};
```

Membership stays keyed by uuid, and every port's `nodeUuid` must also appear in the owning
macro's `members` — `MacroSet::fromVar` rejects a saved macro where that does not hold, and
`retainOnly`/`removeMemberEverywhere` drop a port the moment the member it fronts is removed, by
either path. The port list is derived state in the sense that the nodes are the truth, but the
**order, name, kind and colour** are macro-level presentation and belong on the macro, next to
`name`/`colour`/`bounds`. `"ports"` is optional in a saved macro's JSON — absent parses as no
ports, which is what every macro saved by P8-12 already looks like; `"colour"` is likewise
optional on each port entry (`Colour::toString()`/`fromString()`, the same encoding
`Macro::colour` itself already uses) and, unlike `kind`/`nodeUuid`, a malformed or absent value
never rejects the whole port — it is decorative only, so it just parses as unset.

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
  `resolvePolyLink`/`getJackTargets` already do elsewhere in the GraphEditor sources. Until then the
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

**Rendering (founder-review fix F2, docs/macros_implementation.md §7 items 3/4).** A port node is a real
`ModuleComponent` — selection, hit-testing, cable drag/drop, undo and serialisation all key off
that, unchanged — but it no longer PAINTS or SIZES like an ordinary module card, expanded or
collapsed. `ModuleComponent::layoutMacroPortWidget()` sizes it purely from the shape's own visible
jack count: one row (`kMacroPortWidgetHeaderY + kMacroPortWidgetBottomPad`) for Mono, Poly-N,
`StereoCollapsed` (founder-review fix G2 — deliberately ONE row despite carrying two raw channels,
§5.3's `StereoCollapsed` bullet) or a MIDI port (all of which have exactly one visible jack a side,
a MIDI port's fanned Poly-N bus included), and a second row (`+ kMacroPortWidgetRowStep`) only for
the two-jack `Stereo` shape. See §5.4 for where that widget is PLACED.

**Sizing (founder-review fix G4).** A second founder pass, hands-on with a macro containing a
Delay and a Reverb, called the widget "too large" next to the modules it fronts. The fix is
tuning, not redesign: `kMacroPortWidgetWidth`/`kMacroPortWidgetHeaderY`/`kMacroPortWidgetRowStep`/
`kMacroPortWidgetBottomPad` (`ModuleComponent.h`) shrank from a 104x26 (Mono) box to 92x21 — about
a 29% cut in drawn area, matched by `paintMacroPortWidget()` drawing a 7px jack dot (was 10px), a
4px corner radius (was 6px) and a 9.5f name font (was 10.5f). None of that touches
`getPortForPoint()`'s hit test, which stays the same generous radius every module's jack uses —
the drawn dot and the clickable target are two different knobs, and a "sleek" pass that shrinks
both is a worse regression than the one it fixes. The name still fits a realistic port name
("Delay 1 Audio") at full, unscaled size at the new width; `drawFittedText` (compress-then-ellipsise,
never mid-glyph clip) is the fallback for a longer name, not the expected path.

**Text inset (post-G4 polish fix).** G4's shrink took the name's own text area (`getLocalBounds()`
`.reduced(n, 2)` in `paintMacroPortWidget()`) from 16px down to 12px along with everything else,
but didn't re-derive it against the ALSO-shrunk jack dot: at `n=12`, the 7px dot centred at
`x=10`/`width-10` has its outer edge at 13.5px from the widget's edge — inside a 12px text area. A
render through the real theme showed the collision it predicts: on a Mono widget showing
"Delay 1 Audio", the left dot visibly overlapped the "D", and the text crowded the right dot too.
The fix widens the inset back to 16 (clearing the dot's outer edge by 2.5px each side) WITHOUT
moving the dot or the widget's own width/height — `RealisticPortNameFitsWithinTheWidgetAtFullUnscaledSize`
still holds at the smaller resulting text budget (92 - 2*16 = 60px) for a realistic name at 9.5f.

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

**Ungrouping a macro removes its ports and splices their cables back together (founder-review fix
G7, §7 item 8).** This section originally described the opposite — a port's fronting node
surviving ungroup as an ordinary ex-member, its connections untouched, on the theory that
ungrouping (like grouping) is purely a `MacroSet` metadata change. A second founder review pass,
hands-on with the shipped feature, decided otherwise: `ungroupSelection()` now calls
`GraphEditor::spliceOutMacroPort()` for every one of the macro's ports BEFORE dissolving the
`Macro` record — disconnecting the port node, reconnecting its external endpoint(s) directly to
its internal endpoint(s) on the original raw channels (the full cross product, so fan-in/fan-out
reconnect completely), and removing the node — all inside the SAME undo step as the record's
removal. A macro's ORDINARY members are still never touched by ungroup; only its ports are. See §7
item 8 for the full algorithm, the pass-through verification behind it, and why no provenance
field was needed to treat auto-created and hand-added ports identically.

Because this makes ungroup a genuine graph edit for the first time, it also now runs through the
same post-mutation path `deleteSelection()` does (`updateComponents()` inside the transaction,
firing `onGraphStructureChanged` -> the timeline reconcile pass) — see §7 item 8.

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
- **Not individually selectable or draggable by a LEFT click — but RIGHT-click gets its own menu
  (founder-review fix G7, §7 item 8).** A port widget's body still refuses any left click (no
  selection, no drag): making it draggable would need its own suppression logic distinct from
  "part of a macro-wide selection," for a widget whose whole point is that its position is not the
  user's to set — `dockMacroPortWidgets()` would just snap it back on the next layout pass anyway.
  A right click is the one exception: it opens `ModuleComponent::buildMacroPortContextMenu()`
  ("Delete Port" always; "Rename Port..."/"Configure I/O..." when the port's macro is still alive)
  rather than nothing — before G7 a port node had no delete affordance of its own at all, and
  Configure I/O (rename/reorder/delete/shape-change, still there, unchanged) was the only UI
  surface for it, full stop.

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

**Channel macros (P9-2, [`docs/mixer.md`](mixer.md) §5.5).** When a macro contains a
`Channel Strip`, the **Bypass** fan-out skips the strip and the source node(s) (Track In, Track
Audio) — "bypass" on a channel means "bypass the inserts": the chain's effects go dry while the
source keeps producing and the strip keeps passing signal. `macroBypassState` reports over the
same reduced member set, so a channel whose inserts are all bypassed reads fully on. **Mute** has
no such carve-out: muting a channel macro mutes the strip too. The filter is
`bypassFanOutMembers` in `GraphEditorMacroPortSplice.cpp`.

### 5.7 Hosted plugin

Nothing macro-specific. Because the graph stays flat and macros are canvas presentation, a macro
in the plugin build behaves as it does in the app: it is state in the same `project.json` blob
the plugin already round-trips (`docs/architecture.md`, plugin state format). The plugin editor's
`GraphEditor` renders cards and hulls identically. No new host-mode branch.

The one existing rule that still applies: an over-wide hosted plugin is **refused, never
truncated** — grouping one into a macro changes nothing about that.

### 5.8 The macro menu's reachable entry points

`GraphEditor::buildMacroMenu` is the ONE builder for a macro's own actions (Expand/Collapse,
Rename, Change Colour, Configure I/O, Bypass/Mute, Save as Snippet, Ungroup, Add Selection to
Macro, Remove from Macro, Delete Macro & Modules). There are three ways to reach it, all funnelling
through it rather than each keeping its own copy:

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

**T138: "Add Selection to Macro" / "Remove from Macro" — changing membership without ungroup +
regroup.** Before this, the only way to change who is in a macro was to ungroup it (dissolving the
record) and group again. `GraphEditor::addSelectionToMacro`/`removeSelectionFromMacro` are the
incremental counterparts to `groupSelectionIntoMacro`/`ungroupSelection` for a macro that already
exists, and `buildMacroMenu` offers them as two more items, right after "Ungroup".

- **"Add Selection to Macro" is computed from a candidate list captured BEFORE either call site's
  own forced reselect, never from the selection `buildMacroMenu` sees when it actually runs.** The
  first cut of this feature read `selection.getSelected()` inside `buildMacroMenu` itself and
  shipped with "Add Selection to Macro" silently unreachable from BOTH real entry points — caught
  only by live GUI testing (2026-09-10), because every test up to that point called
  `buildMacroMenu()` directly with `setSelectedNodes()` already set, which never exercises the real
  mouseDown()/reselect sequence a live click goes through. Both the collapsed card's own
  right-click (`MacroCardComponent::mouseDown`) and the expanded hull's empty-space right-click
  (`GraphEditor::mouseDown`'s `macroHullAt` branch) call `selectMacro(macroId, false)` **before**
  showing the menu at all (a UX nicety — the card/hull highlights what you're about to act on) — by
  the time `buildMacroMenu` runs, "the current selection" is already just this macro's own members,
  and any external batch the user picked before right-clicking is gone. The fix:
  `buildMacroMenu(macroId, renameAction, addCandidateSelection)` takes an optional third parameter,
  a `const std::vector<NodeID>*` — both call sites capture `getSelectedNodes()` themselves, one line
  before their own `selectMacro`/`isMacroSelected` reselect, and pass the address through. "Remove
  from Macro" is unaffected and still reads the CURRENT live selection directly (correct either way
  — after either reselect it equals the macro's own members, exactly what removal should see). The
  `ModuleComponent` member-submenu graft (entry point 3) passes no override at all and falls back to
  the live selection, since its own retarget-if-not-already-selected never destroys an external
  batch the same way, and "Add" barely applies there regardless (the clicked module is already this
  macro's member). `MacroCardComponent` gained a `setShowContextMenuHookForTest` seam identical to
  `ModuleComponent`'s own (same headless-CI-segfault reason), and
  `MacroMembershipMenu.AddItemAndSelectionBorderBothSurviveTheRealCardRightClick` drives the real
  gesture end-to-end.
- **The forced reselect itself is skipped whenever there was ANY prior selection at all** — found
  via a *second* round of live GUI testing the same day, in two stages. First: the fix above made
  the menu item work, but the reselect still ran first and swapped the visible selection border
  onto the macro's own members right as the menu opened, so a user right-clicking with an external
  module selected saw no visual cue for what "Add Selection to Macro" was about to insert (reported
  as "the module doesn't seem selected"). The first patch gated the skip on
  `GraphEditor::selectionHasMacroAddCandidate(macroId, priorSelection)` — true only when the prior
  selection had something addable — which fixed that report but left a second, symmetric bug: a
  user who selected a **partial subset of the macro's own members** (meaning to remove just that
  subset) still got force-reselected to *every* member, because a pure-member subset has nothing
  addable either. Reported live as "if I select one module... it just auto-selects all of the
  modules in the macro... if I click remove selected from macro, it just removes all of the
  modules." The fix generalizes the condition all the way down: both call sites now simply check
  `priorSelection.empty()` before calling `selectMacro`/checking `isMacroSelected` — replacing
  `selectionHasMacroAddCandidate`, which is now dead code and was removed. Any non-empty prior
  selection, addable or not, survives untouched (and with it, its selection border); the reselect
  fires only for the genuinely empty case (the plain "right-click a fresh card" case
  `CardRightClickStillReselectsMacroWhenNothingWasSelected` pins), since that's what lets "Remove
  Selection from Macro"/"Ungroup" find the macro's own members via live selection when nothing else
  was selected. `RemoveSelectionFromMacroSurvivesTheRealCardRightClickWithAPartialSubset` pins the
  partial-subset case end-to-end (select one of three members, right-click the card, confirm the
  selection border and menu still target only that one, confirm "Remove from Macro" removes only
  it).
- **A top-level "Remove from Macro" item on a member module's own menu**, alongside the nested
  "Macro: <name>" submenu's own copy of the same verb. Live testing found the nested submenu
  correct but undiscoverable: a user's first instinct right-clicking a module they wanted out of a
  macro was "right-click the module and remove it", not "open its macro's own submenu first".
  `ModuleComponent::buildModuleContextMenu` grafts this item right next to "Expand/Collapse Macro",
  calling the new `GraphEditor::removeNodeFromMacro(nodeId)` — which resolves `nodeId`'s own uuid
  and calls `removeSelectionFromMacro(macro->id, {uuid})` directly, **never the live selection** —
  so it always acts on exactly the module whose card was right-clicked, regardless of what else
  happens to be selected (unlike the nested submenu's own item, which correctly reads live
  selection, itself already narrowed to just this module by `mouseDown`'s own retarget-if-not-
  selected). Only reachable for an ordinary member: `mouseDown` routes a port module
  (`isMacroPortType`) to `buildMacroPortContextMenu()` before `buildModuleContextMenu` is ever
  built, so `macroForNode(nodeId) != nullptr` here always means a real member.
  `TopLevelRemoveFromMacroItemActsOnThisModuleAloneRegardlessOfSelection` pins it.
- **Each item is omitted, not shown disabled, when it would have nothing to do** — mirroring
  "Mute Macro"'s own precedent of omitting a command that can only ever no-op. "Add Selection to
  Macro" needs the captured selection to contain at least one uuid not already a member of THIS
  macro; "Remove from Macro" needs at least one ordinary (non-port) member of THIS macro in it.
  The label pluralizes ("Remove Selection from Macro") when more than one member is being removed.
- **Ports are never pulled out of `members` by either path** — a port's uuid in the selection is
  skipped by `removeSelectionFromMacro`, not merely hidden from the count, since a port has its own
  delete affordance (`ModuleComponent::buildMacroPortContextMenu`) and removing one through this
  generic path would desync the "every port's nodeUuid is a member" invariant `MacroSet::fromVar`
  enforces on load.
- **Both paths now auto-create and auto-delete ports for a cable the membership change crosses or
  makes interior** (second live-testing round, 2026-09-10) — reported as "I remove a module from a
  macro, but its connection still remains, and the output of that module still goes into the macro
  connection". The cable itself was never actually broken (an un-ported boundary crossing is a
  supported, correctly-rendered state — the directional edge-anchor rule above), but it never got a
  real port the way a from-scratch `groupSelectionIntoMacro(true)` grouping would have given it.
  Reusing `buildMacroPortCrossingPlan`/`spliceMacroPorts` as-is for an incremental membership DELTA
  doesn't work directly — the earlier scope-cut note (below, superseded) correctly flagged that
  changing an EXISTING macro's membership can make the crossing set change in **both directions** at
  once, which the plain creation-time function was never built to see past a full "inside" set
  passed in one shot. The actual fix is three new `GraphEditor` members, each scoped to exactly one
  direction of one operation:
  - `buildMacroPortCrossingPlanForNewMembers(macroId, addedUuids)` — the plan for `addSelectionToMacro`.
    Computes `buildMacroPortCrossingPlan` over (existing ordinary members + `addedUuids`), then keeps
    only groups whose internal node is one of `addedUuids` (a group fronting an already-established
    member is a pre-existing un-ported crossing this add didn't create, and is left alone), and drops
    any edge whose external endpoint is one of the macro's OWN existing ports (that crossing isn't
    new either — see the next bullet).
  - `macroPortsThatBecomeInteriorOnAdd(macroId, addedUuids)` — the reverse direction the earlier
    scope-cut note didn't anticipate: if the joining member was already wired straight into one of
    the macro's own EXISTING ports from outside, that port's job is now redundant (both its ends are
    interior) and must be spliced back OUT (`spliceOutMacroPort`) into a plain direct connection —
    otherwise the join leaves a pointless double-port (port → new-port → joining member). Returns
    only ports where *every* remaining connection's other side is now interior; a port with even one
    connection to a node that stays genuinely external is never returned, since splicing it out would
    silently drop that cable (`spliceOutMacroPort`'s own "wired on only one side... disappears"
    behaviour, safe only when the whole macro is dissolving alongside it, as in `ungroupSelection`).
  - `buildMacroPortCrossingPlanForRemovedMembers(macroId, removedUuids)` — the plan for
    `removeSelectionFromMacro`. Computes `buildMacroPortCrossingPlan` over the REMAINING ordinary
    members (existing minus `removedUuids`, still excluding the macro's own ports), then keeps only
    edges whose external endpoint is actually one of `removedUuids` — otherwise a remaining member's
    pre-existing, already-ported connection would look like a brand-new crossing too, since its port
    is excluded from the "inside" set the same as any other port, and get double-ported.

  `buildMacroPortCrossingPlan`'s own `memberUids.size() < 2` early return — a leftover from it only
  ever being called with a fresh, ≥2-module selection at creation time — was removed: the
  incremental callers above legitimately need a crossing plan for a one-member "inside" set (e.g.
  removing one of a macro's two ordinary members leaves exactly one remaining member whose newly-
  external cable still needs a port), and the loop itself was always correct for any size.
  `addSelectionToMacro` splices new ports in, THEN splices redundant ones out, both inside the same
  `recordGraphAndMacroChange` transaction as the membership change itself.
  `removeSelectionFromMacro` splices new ports in BEFORE the membership removal — `spliceMacroPorts`
  needs `macros.find(macroId)` to still resolve, and `removeMemberEverywhere` can dissolve the macro
  record outright if the removal drops its last member, so doing the splice first means that dissolve
  (if it happens) always lands after the boundary is already correct. Configure I/O remains the
  manual way to add/reshape a port beyond what either auto path covers.
- **One `recordGraphAndMacroChange` undo step per gesture**, matching every other macro mutation —
  covering the port splice(s) and the membership change together, not as two separate steps.

`Tests/MacroContainerTests.cpp`'s `MacroMembership` suite covers `addSelectionToMacro`/
`removeSelectionFromMacro` directly (add, all-or-nothing refusal when a uuid is already in another
macro, shrink vs. dissolve-on-last-member, the port-skip rule, the new auto-create/auto-delete-on-add
and auto-create-on-remove cases with their own one-undo-step coverage); `MacroMembershipMenu` covers
the menu's own gating (item appears/is omitted, the plural label, the item's `action` invoking the
real mutation, the partial-subset selection-preservation regression); `MacroMemberContextMenu` covers
the top-level "Remove from Macro" item on a member module's own menu.

### 5.9 The expanded hull's collapse button (founder-review fix G5)

The COLLAPSED card has always drawn a visible expand chevron
(`MacroCardComponent::getExpandButtonBounds`) precisely because the right-click "Expand" menu item
alone left nothing on the card that *looked* clickable — a user had to already know the menu
existed, or find the undocumented double-click, to get back in. The second founder-review pass
pointed out the identical gap on the other side of the toggle: *"add a button to collapse it -
currently there's only a button to expand."* An EXPANDED macro's only routes back to collapsed were
the right-click menu (§5.8) and an undocumented double-click on the name chip
(`GraphEditor::mouseDoubleClick`'s `macroChipAt` branch, which renames rather than collapses) — and
the chip itself, while mouse-interactive, reads as a drag handle, not a collapse control.

**The fix mirrors the card's own chevron exactly**, one rung up: `macroCollapseButtonBounds()`
(`GraphEditor.{h,cpp}`) is the ONE definition of a small square hit zone at the RIGHT end of the
same top-of-hull row `macroChipBounds()` occupies at the left end — the two together read as one
row-spanning control, chip on the left for "drag/rename", button on the right for "collapse".
Painted as a filled `juce::Path` triangle, not a text glyph, for the same reason the card's own
chevron is (`check-nonascii-literals.test.sh` rejects a chevron character outright) — pointing UP,
the opposite direction from the card's expand chevron (which points down), so the pair reads as
opposite ends of one toggle rather than two unrelated icons. `macroCollapseButtonAt()` is the
matching hit-test, called from `GraphEditor::mouseDown` BEFORE the chip check: the two rectangles
never actually overlap (the chip's width is a short label plus a fixed pad, comfortably clear of
the hull's right edge for any real macro), but the button is still checked first and returns
immediately, so it can never be shadowed by the chip's own drag gesture even in a future geometry
change. Both are empty (like `macroChipBounds()`) whenever `macroHullBounds()` is — collapsed,
unknown, or unresolvable — so the button never appears on a collapsed card (which already has its
own chevron) or for a macro with nothing to draw.

**The click goes through `setMacroCollapsed(macroId, true)`** — the exact same call
`buildMacroMenu`'s "Collapse" item and the collapsed card's own chevron (`setMacroCollapsed(...,
false)`) both use, so there is only ever one code path that can flip a macro's collapsed state, and
it is one `recordGraphAndMacroChange` undo step regardless of which of the three affordances
triggered it. The button click does not touch selection — matching the card's own chevron handler,
which also just toggles state and returns — so clicking it while other things are selected leaves
that selection alone rather than surprising the user with an implicit re-select.
`Tests/MacroContainerTests.cpp`'s `MacroCollapseButton` suite drives a real synthesised
`juce::MouseEvent` into `GraphEditor::mouseDown()` (not a direct `setMacroCollapsed()` call — the
whole point of this fix is the hit-test, which testing the layer beneath `mouseDown()` cannot
catch, the same rule that already caught a real bug on this branch) and covers: the button
collapsing the macro and arming neither a selection drag nor a selection change, a click just
outside its bounds not collapsing, the button and chip never overlapping (including a tight
two-module macro — the degenerate case a real macro's hull ever gets that narrow), and collapsing
via the button being one undo step that a single undo re-expands.

### 5.10 Cmd/Ctrl-drag membership (FRO40): crossing a hull border joins/leaves it

Before this, the ONLY ways to change a macro's membership were the menu items §5.8 documents
("Add Selection to Macro" / "Remove from Macro", T138) and ungroup+regroup — both explicit, both
several clicks away from the drag the user is already doing. FRO40 adds a direct-manipulation
route: **Cmd+drag a module across an EXPANDED macro's hull border** (`macroHullBounds()`, §5.4)
**to add it to, or remove it from, that macro** — the drag itself decides membership, with no
separate confirmation step.

**The query, `MacroGroupController::macroDragJoinOrLeaveTarget`** (declared beside
`macroHullBounds`/`macroHullAt`), is given the dragged node's id and its CENTRE (not its top-left)
in canvas coordinates, and answers with a macro id or empty ("neither"):

- **JOIN** — the dragged node is not a member of any macro: test its centre against the plain
  `macroHullAt()`, which already only considers EXPANDED macros (a collapsed one is never a
  candidate — its members are hidden `ModuleComponent`s that cannot be dragged in the first place)
  and picks the smallest hull when more than one overlaps.
- **LEAVE** — the dragged node IS a member of macro X: test its centre against
  `macroHullBoundsExcluding(X, ownUuid)`, a hull-excluding-self variant added alongside
  `macroHullBounds`. This exists because `macroHullBounds()` is a LIVE union of member bounds — a
  member being dragged OUTWARD keeps inflating its own hull's union, so without excluding its own
  contribution first it could never test as "outside" and could never leave. One consequence worth
  knowing: for a two-member macro, `macroHullBoundsExcluding` reduces to just the OTHER member's
  own footprint, so almost any Cmd-drag of either member reads as "outside" it — by design, not a
  bug; the wider the macro, the more room a member has to move before crossing out. This still
  holds with THREE OR MORE members: excluding the dragged member's own contribution only unions
  the OTHER members' bounds, so the exit distance is direction-dependent, never unbounded — three
  members in a horizontal row is the case that looks worst for this (the two outer members keep
  the excluding hull's bounding box wide along the row), and it is still reachable, just further,
  along the row, and short in the perpendicular direction (bounded by roughly half the row's own
  height plus the hull margin, regardless of how far apart the outer members are). See
  `MacroDragMembership.ThreeMemberMacroLeaveIsReachablePerpendicularToTheRow`.

**The gesture mirrors `ModuleComponent`'s existing Ctrl deferred-classification exactly**
(`ctrlTogglePending`/`ctrlPressSelection`): Cmd+press arms a NEW `cmdReparentPending` flag and
`cmdPressSelection`, collapses the selection onto the pressed module, and falls through to arm the
drag like a plain click would; Cmd+CLICK (no movement) completes as the deferred additive-select
toggle, exactly like Ctrl's. `GraphContentComponent::paint()`'s existing dashed hull outline (§ the
expanded-macro grouping hull, above) reads `GraphEditor::getMacroDragCandidateId()` and draws the
SAME stroke heavier and fully opaque for whichever macro is the live candidate, rather than
inventing a second visual language for "about to change".

**Whether a drag can reparent at all is a THIRD, separate flag — `reparentArmed` — not derived
from `ctrlTogglePending || cmdReparentPending`.** The first cut of this feature gated `mouseDrag`'s
candidate query and `mouseUp`'s reparent-vs-plain-finalize choice on that OR, which seemed safe
(one of the two is always true exactly when a drag might cross a hull) but was wrong: on macOS,
Ctrl and Cmd are genuinely distinct keys, so a PLAIN Ctrl-drag — the SHIPPED insert-between
gesture, unrelated to this feature — also sets `ctrlTogglePending`, and the OR silently let it
reparent too whenever it happened to cross a hull, compounding two gestures nobody asked to
combine. `reparentArmed` is set to `e.mods.isCommandDown()` alone, unconditionally, at the top of
`mouseDown`'s modifier chain (before the `isCtrlDown()`/`isCommandDown()` branches, so a plain
click/drag with neither modifier reaches them with it already false) — the platform matrix that
falls out:

| Platform | Gesture | `reparentArmed` | Result |
|---|---|---|---|
| macOS | Cmd+drag | true | reparent (join/leave) only |
| macOS | Ctrl+drag | false (`isCommandDown()` is false — distinct keys) | insert-between only, unchanged from before FRO40 |
| Windows/Linux | Ctrl+drag (= Cmd+drag: `commandModifier` IS `ctrlModifier`) | true | **both**: insert-between AND reparent, if the drag happens to cross a hull — see below |

`mouseDrag` recomputes the candidate on every tick, but ONLY while `reparentArmed` (gating a plain
drag, and a macOS Ctrl-drag, out of the feature entirely) via `GraphEditor::
updateMacroDragCandidate`, which stores it in the ONE new private field `macroDragCandidateId_`
(`getMacroDragCandidateId()` is the public accessor).

**`reparentArmed` is re-derived on every `mouseDrag` tick for a SINGLE-module drag, not only
latched once at `mouseDown`.** The first cut only sampled `e.mods.isCommandDown()` at press time,
so the user had to already be holding Cmd (or Ctrl on Windows/Linux) before grabbing the module —
pressing it partway through an otherwise-plain drag never armed reparent, with no way to discover
the gesture from the candidate highlight since it never lit up. `mouseDrag` now re-reads the live
modifier each tick, gated on `!isSelectionDragActive()`: a multi-selection group drag keeps
whatever `mouseDown` latched for its whole gesture (reparenting one member out of a group drag is
ambiguous — which macro, which of several dragged modules — and stays out of scope), but an
ordinary single-module drag can have Cmd/Ctrl pressed OR released mid-gesture and see the
candidate highlight arm or clear immediately, right along with it.

**The painted hull for the macro a drag is pulling a member OUT of shrinks away from that member
immediately, not only once LEAVE actually arms.** `macroHullBounds()` is a LIVE union, so painting
it directly for the module's OWN (about-to-be-left) macro made the outline visually chase the
module as it was dragged out — pulling a member toward the edge of a 2-member macro looked like it
was growing the hull to stay around it, making "remove from macro" look impossible. `GraphEditor`
now tracks a second field, `macroDragDraggedNodeId_`, set/cleared by the exact same
`updateMacroDragCandidate`/`clearMacroDragCandidate` calls as `macroDragCandidateId_` (one
lifetime, not two), and set FIRST, unconditionally, before the candidate itself is computed — so
the shrink starts on the very first tick of the gesture, before any LEAVE candidate has actually
armed. `GraphEditor::paintedMacroHullBounds(macroId)` is what `paintExpandedMacroHulls`
(`GraphEditorCables.cpp`) calls instead of `macroHullBounds` directly: it returns
`macroHullBoundsExcluding` for the macro the dragged module currently belongs to, and the ordinary
live `macroHullBounds` for every other macro — including one the drag might JOIN, which by
definition isn't the dragged module's current macro and has nothing to exclude it from. Hit-
testing (`macroHullAt`, used by click-to-select and the hull's right-click menu) is unaffected and
keeps using `macroHullBounds` — this substitution is paint-only.

**The Windows/Linux arbitration is still at mouseUp, but only decides WHICH of the two gestures a
reparent-armed drag ends up as, never WHETHER one can fire at all** (that is `reparentArmed`'s job,
decided at press). If the last candidate `mouseDrag` computed is non-empty, `mouseUp` reparents;
otherwise it falls through to the plain finalize path, which is what already carries Ctrl's own
insert-between behaviour (`SmartConnectionEngine` samples `isInsertModifierDown()` live, from
inside `finalizeModuleDrag`). Because Windows/Linux cannot express "Ctrl but not Cmd" at all, a
Ctrl-drag there that happens to cross a cable AND a hull in the same gesture performs BOTH:
inserting the dragged module into that cable's chain AND joining/leaving the macro whose hull it
crossed. This is a genuine platform limitation, not a bug — there is no way to offer the two
gestures as separately addressable there without a second, unrelated modifier.

**One more single-step subtlety worth stating explicitly:** a member dragged out of macro A
directly into macro B's hull, in ONE continuous drag, LEAVES A and does not also join B —
`macroDragJoinOrLeaveTarget` checks membership FIRST (§ the query, above) and returns the LEAVE
target as soon as it finds one, never falling through to also test JOIN against B in that same
call. Joining B is a second, separate drag, started from outside any macro.

**One undo step.** `GraphEditor::finalizeMacroMembershipDrag` (`GraphEditorDragDrop.cpp`) is
modelled on `finalizeMacroCardDrag` (`GraphEditorSelection.cpp`, § the macro card drag above): one
lambda runs the ordinary `finalizeModuleDrag` THEN the membership mutation, and the whole lambda is
handed to ONE `recordGraphAndMacroChange` call. `addSelectionToMacro`/`removeSelectionFromMacro`
(§ T138, above) both gained a trailing `recordUndo = true` parameter for this: `recordUndo=false`
skips their own `recordGraphAndMacroChange` and runs the mutation directly, so the outer finalize
owns the one transaction instead — every one of their ~10 other existing callers keeps the default
and stays byte-identical.

The graph "before" half of that ONE transaction is deliberately NOT `recordGraphAndMacroChange`'s
own default fresh `graphToJSON()` capture — it is whatever `ModuleComponent::mouseDown`'s
`captureBeforeState()` stashed, handed back via the new `AppUndoManager::
takeCapturedGraphBeforeState()` and passed as `recordGraphAndMacroChange`'s new
`graphBeforeOverride` parameter. This is load-bearing, not a style choice, and it is the one place
this feature's original design (position writes happen only inside finalize) turned out wrong
against the real code: `ModuleComponent::moved()` fires on EVERY position change, including every
live `mouseDrag` tick, and unconditionally writes the module's CURRENT (mid-drag, unsnapped)
position into its graph node's properties right then — not only once, at finalize. By the time
`finalizeMacroMembershipDrag` runs, a fresh capture would already see that contaminated mid-drag
position as "before", and the combined undo would land the module back at wherever the live drag
last released it, never at its true pre-drag position — exactly the failure
`MacroDragMembership.OneUndoStepRestoresBothPositionAndMembership` caught. Consuming the mousedown-
time capture instead is what every ordinary (non-reparenting) body-drag already relies on too, via
the plain `captureBeforeState()`/`pushSnapshotFromCapture()` pair — this reuses the SAME captured
value rather than a second, independent capture. `ModuleComponent::mouseUp` must NOT also call
`pushSnapshotFromCapture()` on the reparent path (that would push a SECOND, position-only undo
step from the same capture) — it doesn't need to explicitly discard it either, since
`finalizeMacroMembershipDrag` already consumes it via `takeCapturedGraphBeforeState()`, which
clears it as a side effect.

Reuses the T138 membership path wholesale (`buildMacroPortCrossingPlanForNewMembers` +
`macroPortsThatBecomeInteriorOnAdd` + `spliceMacroPorts` + `spliceOutMacroPort` on join;
`buildMacroPortCrossingPlanForRemovedMembers` + `spliceMacroPorts` on leave) — a crossing drag gets
exactly the same auto-port-creation and splice-out-when-interior behaviour §5's crossing-plan
machinery already gives the menu-driven add/remove, with no new port logic of its own.
`Tests/Macros/MacroContainer/MacroDragMembershipTests.cpp` drives the real
mouseDown/mouseDrag/mouseUp gesture (never a direct `addSelectionToMacro` call — see the T138 note
above on why a direct-call test can hide an unreachable feature) and covers join, leave, a join
that simultaneously creates a new crossing port and splices out one that became interior, the
single combined undo step, the Cmd-click-with-no-movement toggle, a drag that never crosses a
hull, and the Windows/Linux Ctrl/Cmd arbitration.
