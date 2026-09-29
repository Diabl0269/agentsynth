# Macro Ports

Port mechanics for the proxy inlet and outlet model [`docs/macros/macros.md`](macros.md) decides:
node types, the port list, the shape rule, how a port is drawn, cable rendering across the boundary,
latency, bypass and mute, and hosted-plugin behaviour. The dialog that edits ports is
[`docs/macros/configure-io.md`](configure-io.md); the paths that create and remove ports on their own
are [`docs/macros/auto-ports.md`](auto-ports.md).

---

## Node types

**Four** `ModuleType` entries, all **internal-only** with the same three exclusions as
`TimelineMidiSource`, `TimelineAudioSource` and `RecordTap`: no library row, no replace-menu entry,
never authorable by a model (`kNonAuthorableModuleTypes` — see
[`docs/macros/macros.md`](macros.md#ai-authorability)).

- `MacroInlet` — "Macro In". Audio and CV. One logical port in, one out; passes its input through
  unchanged.
- `MacroOutlet` — "Macro Out". Audio and CV, the mirror of `MacroInlet`.
- `MacroMidiInlet` — "Macro MIDI In". MIDI, zero audio channels.
- `MacroMidiOutlet` — "Macro MIDI Out". MIDI, the mirror of `MacroMidiInlet`.

**Why MIDI is a separate pair of types rather than a "kind" flag on the audio pair.** The
load-bearing reason is a **construction-time channel-shape difference**: `ModuleBase(name, 0, 0)` for
a MIDI node against `ModuleBase(name, N, N)` for an audio or CV one, which a single type would have
to branch its bus layout on — exactly what the fixed-channel-count invariant makes awkward, since the
layout is frozen the moment the constructor runs. It is *not* that a MIDI `processBlock` and an audio
one inherently differ (here both bodies are near-identical pass-through no-ops); it is specifically
that a MIDI port carries no channel shape at all, no Mono, Stereo or Poly-N, just `acceptsMidi()` and
`producesMidi()` — the same split `TimelineMidiSourceModule`/`TimelineAudioSourceModule`,
`ExternalMidiModule` and `PolyMidiModule` already have.

All four are created by a **flow**, never by the module library and never by the replace menu. An
inlet or outlet always belongs to exactly one macro: it is a member of that macro's `members` list
like any other node, and is dissolved with the macro by an action that actually deletes members
(`GraphEditor::deleteMacroAndMembers`). **Ungrouping still never deletes an ordinary module**, but it
does remove every one of the macro's own port nodes and splice back the cable each one proxied — see
[`docs/macros/auto-ports.md`](auto-ports.md#ungroup-and-direct-deletion-of-a-port).

Each port carries a user-visible **port name** ("Pitch In", "Wet Out"). That name lives on the
macro's own `MacroPort` entry, not in the node's extra state, because the name is macro-level
presentation and the port-creation flow is what wires a name to a node.

## Port set and ordering

`synth::Macro` carries an ordered list of port descriptors, with `toVar`/`fromVar` round-tripping and
`retainOnly` reconciliation:

```cpp
enum class MacroPortKind { AudioCV, Midi };

struct MacroPort {
    juce::String nodeUuid;   // the MacroInlet/MacroOutlet (or MIDI variant) node this port fronts
    bool         isInput;
    juce::String name;
    int          order;      // draw order on the card, user-reorderable
    MacroPortKind kind;      // which pair of node types this port is — kept here too so a macro's
                             // port list renders with no per-port graph lookup
    std::optional<juce::Colour> colour; // unset falls back to the kind tint (accent for AudioCV,
                                        // audioWire for Midi) everywhere a port paints
};
```

**Every port's `nodeUuid` must also appear in the owning macro's `members`.** `MacroSet::fromVar`
rejects a saved macro where that does not hold, and `retainOnly`/`removeMemberEverywhere` drop a port
the moment the member it fronts is removed, by either path.

The port list is derived state in the sense that the nodes are the truth, but the **order, name, kind
and colour** are macro-level presentation and belong on the macro, next to `name`, `colour` and
`bounds`. `"ports"` is optional in a saved macro's JSON — absent parses as no ports, which is what
every macro saved before ports existed looks like. `"colour"` is likewise optional on each port entry
(`Colour::toString()`/`fromString()`, the same encoding `Macro::colour` uses) and, unlike `kind` and
`nodeUuid`, a malformed or absent value never rejects the whole port: it is decorative only, so it
parses as unset.

## A port shape is chosen at creation and then fixed

This is the constraint most likely to be got wrong, so it is stated as a rule.

**A port node's channel shape is decided when the node is constructed, and is immutable for that
node's lifetime.** Changing a port from mono to stereo means deleting the port and adding a new one;
it never means widening a live node. That is not a limitation worked around — it is the
fixed-channel-count invariant applied honestly, because a port node is an ordinary module and that
rule binds it like every other module.

**FRO20 (P8-25, founder review round 3, item 3.6): the delete-and-re-add auto-wires the NEW raw
channel a Mono -> Stereo/StereoCollapsed change adds.** `MacroGroupController::changeMacroPortShape`
replays every surviving cable onto the new node's matching raw channel first (unchanged), then, for
that specific transition, wires the raw channel the OLD Mono shape never had — reusing the SAME
rule the Dual I/O toggle applies when an ordinary module grows a right leg
(`GraphEditor::completeStereoPairConnections`), mirrored locally rather than called directly (this
collaborator never includes `GraphEditor.h`; see `MacroGroupController.h`'s class comment). `Stereo`
(two SEPARATE jacks) pairs the new right leg with the peer's own right leg when it has one the user
can reach, or sums into the SAME mono jack the peer already exposes when it does not.
`StereoCollapsed` (one jack, two raw legs) instead fans the identical cable onto the hidden follower
raw channel, matching how a collapsed FX jack's own dual-raw-leg pair carries one cable. The whole
thing — delete, create, replay, auto-wire — is still the ONE undo step the modal promises.

There is deliberately **no shape inheritance**. Inheritance is the right rule for Dual I/O on a real
DSP module (`ModuleBase::hasStereoOutputPairShape` derives it from channel counts, never from a
per-module registration) and the wrong frame for a node constructed on demand: a port is created
*before* it is wired, so at construction time there is nothing to inherit from.

Shape is therefore an **input to port creation**, supplied one of these ways:

- **From the Configure I/O dialog** — Mono, Stereo or Poly-N, picked explicitly. This is the primary
  flow, and the only way to reach Stereo or Poly-N. `StereoCollapsed` is never one of the choices.
  **The collapsed card's own '+' affordance (FRO24) reaches the same four choices** — Mono, Stereo,
  Poly-N or MIDI — through a small `juce::PopupMenu` calling the identical `addMacroPort()` API, not
  a second creation path; see
  [`docs/layout/macro-cards.md#direct-port-addremove-from-the-collapsed-card`](../layout/macro-cards.md#direct-port-addremove-from-the-collapsed-card).
- **From a cable dropped on a collapsed card's boundary** — creates a port matching the cable's
  direction and kind, wired to the EXTERNAL end of the drag. **Mono, `StereoCollapsed` or `Poly`**,
  inferred from the dragged cable's own jack fan (a 2-leg Audio or collapsed Key/sidechain jack gives
  `StereoCollapsed`; FRO234, 2026-09-27 founder decision — see
  [`docs/macros/auto-ports.md`](auto-ports.md#automatic-macro-ports) for the shared inference rule);
  never the two-jack `Stereo` shape, which needs two independent legs no single cable carries. This
  path never wires anything on the macro's INTERIOR side: the macro is collapsed, so there is no
  visible member to pick a target from. Only the dialog, or a manual cable drawn after expanding the
  macro, connects a port to a specific member.
- **From a cable dragged across an expanded macro's hull** — same inference (Mono/`StereoCollapsed`/
  `Poly`, never the two-jack `Stereo`), and it wires both sides. See
  [`docs/macros/auto-ports.md`](auto-ports.md#ports-on-a-cable-drag).
- **From grouping a selection with a crossing cable** — like the two cable paths above, can produce
  `StereoCollapsed`.
  `GraphEditor::buildMacroPortCrossingPlan` derives the shape from the internal jack the crossing
  cable actually lands on, never from a user choice
  ([`docs/macros/auto-ports.md`](auto-ports.md#auto-creating-ports-when-grouping)).

Given a shape, the node is constructed so the existing rules produce the right result with no
special-casing:

- **Mono** — raw ch0 is the node's one visible jack.
- **Stereo** — raw ch0 (Left) and a dedicated `kRightBase` raw channel (Right) are the node's two
  visible jacks, the split-block convention every other stereo-capable module follows when its two
  legs sit on SEPARATE visible jacks — never ch1. It applies here even though a Macro In or Out has no
  CV on ch1 to protect, because the convention is about consistency across the codebase, not just
  this module's own channel layout. Unlike a real DSP module's Dual I/O toggle there is no runtime
  collapse or expand affordance: the shape is fixed at creation, so a Stereo port always shows both
  legs, and a toggle that could hide one would itself be a form of the shape changing after creation.
  `hasStereoOutputPairShape`/`hasDualIOParameter()` are NOT involved (they key off the module's fixed
  *raw* channel count, which a Macro In or Out never varies from `kMaxChannels`);
  `rightAudioLegChannel()` is overridden directly instead, which is what a peer module's own
  stereo-pair auto-complete (`completeStereoPairConnections`) actually reads.
- **`StereoCollapsed`** — raw ch0 (head, `polyVoiceSpan` 2) and raw ch1 (follower) are CONTIGUOUS and
  both map to the SAME ONE visible jack. This is the other stereo shape already in the codebase:
  `ModuleBase::mapStereoPairOutput`'s Dual-I/O-OFF branch, which every FX module (Delay, Reverb and
  the rest) defaults to — one "Audio" jack whose raw ch0 and ch1 are a contiguous pair, not two
  separately-jacked legs. The "never ch1" rule above governs the two-jack `Stereo` shape, where a
  separately-jacked right leg on ch1 would collide with the CV ch1 reserved on the split-block voice
  modules (Oscillator, Filter, VCA); it does not extend to a COLLAPSED pair, which is the FX pattern
  every collapsed stereo module already uses on ch0 and ch1, ports included. **Contiguity is
  load-bearing, not cosmetic:** `GraphEditor::getJackTargets()` and every caller that expands a
  `JackTarget` walk `rawHeadChannel .. + voiceSpan - 1` assuming adjacency, so a span-2 head at ch0
  paired with a follower on the SPLIT-block `kRightBase` would make that expansion target ch1 —
  inactive under this shape — and leave the real second leg's cable unreachable from any jack:
  audible, with no jack to unplug it from, exactly the hidden-cable failure
  `dropRoutingsOnHiddenJacks`/`dropHiddenRightLegConnections` exist to prevent elsewhere. **Never
  selectable by hand**: the dialog has no entry for it, and it exists so a grouping-time splice can
  mirror an internal module's own collapsed jack. A port already carrying this shape still shows as
  "Stereo" in the dialog (it IS a stereo pair), but the dialog can never re-select it — picking any
  shape there, "Stereo" included, is a real, deliberate conversion to the two-jack shape, one undo
  step like any other shape change.
- **Poly-N** — every raw channel `0..voiceCount-1` maps to the SAME single visible jack, with raw
  ch0 marked `isPolyGroupHead` and `polyVoiceSpan == voiceCount` in its `LogicalPort` mapping, so a
  poly-bus wire crossing the boundary is drawn and counted as one poly bus rather than N separate
  cables.

A cable whose shape does not match the port it is dropped on is refused the same way any mismatched
connection is — never silently adapted at the boundary.

**How the shape is implemented without a factory or format change.**
`MacroInletModule`/`MacroOutletModule` use the declare-a-maximum-and-vary-the-visible-count mechanism
`Source/Modules/CLAUDE.md` documents for Audio Input and Hosted Plugin: the node always carries
`kMaxChannels` (8) raw channels for its whole lifetime, never renegotiated, and its
`mapInputChannel`/`mapOutputChannel` overrides — identical in both directions, since the node is a
symmetric pass-through — decide from a `shape_`/`voiceCount_` pair which raw channels are active and
how they map to visible jacks. That pair is set ONCE by `setPortShape()` right after construction and
persisted through `getExtraState()`/`setExtraState()`, trusted path only, like every module's
`"state"`. A save with no `"shape"` key parses as Mono, exactly the shape it already behaved as.

**Pass-through is verified, not assumed.** `mapInputChannel(c)` and `mapOutputChannel(c)` return the
identical `LogicalPort` on both `MacroInlet` and `MacroOutlet` for every shape a port can carry —
Mono, Stereo's split-block right leg, `StereoCollapsed`'s two-raw-channel single jack, and Poly-N's
fanned bus — so whatever enters the port on raw channel `c` is exactly what leaves it on raw channel
`c`. A MIDI port's single "channel" is always `juce::AudioProcessorGraph::midiChannelIndex` on both
sides. This is what makes splicing a port out signal-preserving.

## How a port is drawn

A port node is a real `ModuleComponent` — selection, hit-testing, cable drag and drop, undo and
serialisation all key off that, unchanged — but it does not PAINT or SIZE like an ordinary module
card. A macro's ports live in two **sidebar strips**, one down the left border (inputs) and one down
the right border (outputs), on the collapsed card and on the open macro alike. Each strip holds one
16 px row per port (a Stereo port takes two), each row carrying the port's jack, its
`MacroPort::name`, and — at the strip's foot — a '+' and a '-'.

**Why strips rather than cards or free-floating widgets.** A port is a boundary jack, not a module
the user put in the box, and drawing it as a full 280-wide card made a macro of two FX modules read
as a macro of four modules. The earlier hanging widgets outside the outline overlapped when a side had
several ports and pushed the '+' under a jack; a strip gives every port its own row.

- **Width is content, not zoom.** A strip is as wide as the longest port name on its side plus
  padding (`macroPortStripWidthFor`: 18 px jack inset + name + 8 px padding; the open macro adds 8 px
  for the inner jack). A side with no ports keeps the 26 px minimum so its '+' has a home. Nothing is
  persisted: the width is measured from the names each time, with a local `juce::Font`, so paint,
  hit-testing and cable anchoring all agree without a `Graphics` context.
- **Zoom decides only what is painted.** Below 50 percent canvas zoom
  (`macroPortNamesVisibleAtZoom`) the names and the '-' are not drawn; the strips, jacks and '+' keep
  their exact positions, so cable ends never move. Hovering a jack then shows its name in the
  tooltip. The zoom is read at paint time from the parent component's transform scale, so the state
  flips once per zoom gesture and adds no repaint of its own.
- **Collapsed card.** `MacroGroupController::macroCardPortLayout` puts jack `i` of a side at
  `y = 30 + 16 i + 8`, x = 10 (inputs) / width - 10 (outputs). The card grows to
  `30 + rows * 16 + 22` px when that exceeds its 90 px floor (`macroCardHeightFor`); the height is
  derived when the card is synced, never written into `Macro::bounds`, so an old save shows at the new
  height. `MacroCardComponent::paintPortStrips` fills each strip `surfaceHi` at 55 percent following
  the card's corner radius, with a 1 px `border` line on the inner edge; the title, member preview and
  count keep the middle column between the strips.
- **Open macro.** The hull widens by each strip so members keep their margin and do not move when a
  port is added, and grows down when the rows outrun the members
  (`computeMacroHullBounds`). `MacroGroupController::macroHullPortLayout` is the one layout: it gives
  each port's docked widget bounds, its **outer jack** (on the hull's dashed border, where cables from
  outside land) and its **inner jack** (5 px inside the strip's inner edge, where cables to members
  start). `dockMacroPortWidgets` sets each widget to those bounds. A widget is a bare
  `ModuleComponent` that draws only its jacks (10 px on the boundary side, 7 px on the interior side)
  and its name; the strips are painted under it by `paintMacroPortStrips` from
  `GraphContentComponent::paint`, before the dashed outline. There is no hover cross on the open
  macro, because a widget's jacks are where cable drags start; removal is the strip's '-' or the
  port's right-click Delete Port.
- **The '+' and '-'.** '+' opens the kind/shape menu (`MacroGroupController::buildAddPortMenu`, one
  menu for the card and the hull). '-' removes the **bottom** port on that side
  (`deleteBottomMacroPort`, through `deleteMacroPortManually`, so undo is identical to a hovered jack's
  cross).
- **The drawn dot and the clickable target are two different knobs.** `getPortForPoint()`'s hit test
  keeps the same generous radius every module's jack uses (10 px). Rows are 16 px apart, so on the
  card `macroCardPortForPoint` picks the nearest jack within that radius.

- **The name resolves live.** `MacroGroupController::macroPortOwnerFor(nodeId)` walks the node's uuid to its
  macro and then to the `MacroPort` fronting it, on every paint, rather than caching the name on the
  node — so a rename is reflected on the very next repaint with nothing to invalidate. The same name
  is drawn next to the matching jack on the COLLAPSED card too (`MacroCardComponent::paintPortStrips`,
  reading the identical field off `MacroGroupController::macroCardPortLayout`), so an expanded and a
  collapsed macro read a port's name the same way.
- **Placement is derived, never dragged.** `MacroGroupController::dockMacroPortWidgets()` runs at the end of
  every `updateComponents()` pass, after a single-module drag settles (`finalizeModuleDrag`) and
  unconditionally at the end of `finalizeSelectionDrag()`; it sets each EXPANDED macro's port
  widgets to the bounds `macroHullPortLayout()` derives from `macroHullBounds()` — inputs down the
  LEFT sidebar, outputs down the RIGHT, one row each from the top, ordered by `MacroPort::order`, the
  same order `macroCardPortLayout()` uses for the collapsed card, so a port's expanded position and its
  row in the dialog never disagree.
  `ModuleComponent::mouseDown` refuses to arm a body drag or a selection click for a macro-port node
  (mirroring the Attenuverter's own "no header, nothing to click" early return), so nothing on the
  canvas can desync a widget from its hull. A right click is the one exception — see
  [`docs/macros/auto-ports.md`](auto-ports.md#ungroup-and-direct-deletion-of-a-port).
- **The hull-feedback trap.** `macroHullBounds()` unions only NON-PORT members. A port's own fronting
  node is excluded because if it counted toward the bounds that DEFINE the hull, docking it against
  that hull would grow the hull, which would push it out again, every layout pass. A macro made
  ENTIRELY of ports has nothing left to union; that case falls back to the macro's own persisted
  `bounds` rather than leaving its ports with no edge to dock against. `applyMacroCollapsed`'s
  "seed the collapsed card at the current member bounding box" union excludes port members for the
  identical reason — folding a docked-left input widget into it would seed the collapsed card
  noticeably left of where the real members sit, worse with every additional input port.
- **Why a group drag still needs an explicit re-dock.** A WHOLE-macro drag moves every member, ports
  included, by the identical delta, so the hull moves by the same amount the ports did and their
  hull-relative offset is preserved for free. A PARTIAL selection has no such guarantee: a marquee can
  catch a port widget together with an unrelated module but without the macro's other members, and
  then the hull does not move by the delta the port just moved by. `finalizeSelectionDrag()`
  therefore calls `dockMacroPortWidgets()` unconditionally — a no-op for the whole-macro case, a real
  correction for the partial one.
- **Size estimation is nominal.** A widget's width is its strip's, set by the dock, so
  `estimateModuleSize` returns a nominal one-row size (`kMacroPortStripInset + 60 + kMacroPortStripPadding`
  by `kMacroPortRowHeight`) for all four types — the same first-layout default
  `ModuleComponent::layoutMacroPortWidget` uses before the first dock, pinned by
  `MacroPortWidgetG4.EstimateModuleSizeMatchesTheRealWidgetForEveryPortTypeName`.

## Member counts report modules, not ports

A port's uuid is a genuine `Macro::members` entry — load-bearing, and what the "every port's
`nodeUuid` is a member" invariant requires — but it is a boundary jack the macro exposes, not a
module the user put in the box. The two are different quantities, and **every user-facing count or
list reports the module one.**

`synth::Macro::memberIsPort()`/`moduleMemberCount()` (`MacroSet.h`) are the ONE place that exclusion
lives, and every presentation call site routes through them rather than re-deriving the filter:
`MacroCardComponent::getModuleCountText()` (the card's count line — "N modules" with no ports
configured, "N modules, M ports" otherwise, naming the port count rather than silently dropping it
from view) and `GraphEditor::macroMemberNames()`/`macroMemberPreviews()` (the tooltip's member-name
list and the card's content-preview glyphs, neither of which lists a port).

`Macro::members` itself, and the `MacroPort` and node it fronts, are untouched by this: bounds, group
drag, bypass and mute fan-out, undo and serialisation all keep reading `members` exactly as before. A
test pins that a port's uuid is still a member, guarding against a future "fix" that tries to make
the count line up by removing ports from membership instead. An all-ports macro reads
"0 modules, N ports" and draws no preview glyphs; that is the honest answer, not a bug.

This is presentation only. The count line sits in the card's middle column between the two port strips;
the card grows taller as ports are added (`macroCardHeightFor`), so the count row is never crowded by a jack.

## Cable rendering across the boundary

`buildVisibleCables()` keeps ownership of the whole rule:

| Case | Treatment |
| --- | --- |
| Both endpoints inside one collapsed macro | dropped |
| Crosses a collapsed boundary through a port | anchored to the **card jack** of the inlet or outlet it passes through |
| Crosses a collapsed boundary straight to an interior member | anchored to the card's **left** edge if entering the macro, **right** edge if leaving it |
| Both endpoints outside | untouched |
| Expanded macro | untouched; a cable through an inlet or outlet ends on that port's **outer jack** on the hull border (`ModuleComponent::getPortCenter` at the docked widget), and a cable to a member starts on the port's **inner jack** on the strip's inner edge |

A cable that crosses a collapsed boundary **without** going through an inlet or outlet — wired
straight to an interior member — still lands on the card's edge, and that case must not be treated as
an error: a macro is a grouping first and a black box second, and the user is allowed to wire into
its guts.

**Why the no-port edge anchor is DIRECTIONAL rather than facing.** The original treatment picked
whichever edge of the card faced the cable's other endpoint (a ray from the card's centre toward that
endpoint, projected onto the perimeter). For a member wired straight through the boundary that reads
as arbitrary — two cables, one entering and one leaving, both land on the card's TOP edge at a single
point whenever both of their other endpoints happen to sit above the card. Keying the edge off the
cable's direction relative to the macro matches what a real port jack already does, inputs on the
left and outputs on the right:

- the macro is the cable's **source** (signal *leaving* it) — anchor on the card's **right** edge;
- the macro is the cable's **destination** (signal *entering* it) — anchor on the card's **left**
  edge.

The Y coordinate is still derived from the facing projection (`projectToRectEdge` is still called,
just to read off a Y rather than a full point) so several crossing cables on the same side keep
spreading vertically instead of collapsing onto one pixel; that Y is then clamped into the card's
port-row span, from the first row's top (`kMacroPortRowsTop`) down to the strip footer
(`kMacroPortStripFooter` above the card's real bottom edge). X lands exactly
on the card's boundary (`cardBounds.getX()`/`getRight()`), not inset the way a real port jack is
(`kMacroCardJackInsetX`), so a no-port edge anchor never sits under a port dot occupying the same
side.

**The collapsed card's jacks are one definition read by three places.**
`MacroGroupController::macroCardPortLayout` draws one jack per configured `MacroPort` — inputs on
16 px rows down the left edge, outputs down the right, in `order` — and that layout is what
`MacroCardComponent::paint` draws, what `GraphEditor::endConnectionDrag` hit-tests a drop against
(`macroCardPortForPoint`), and what `rebuildVisibleCables()` anchors a boundary cable through a port
at. Every jack position is zoom-independent. A cable straight to an interior member instead gets the directional edge anchor above; the two
treatments coexist and are told apart by which endpoint node the cable resolves to, a port's fronting
node or an ordinary member.

Dropping a cable exactly on an existing port's jack wires straight into that port's node instead of
minting a fresh one; a jack whose direction or kind does not match the drag is refused, the same as a
mismatched drop on an ordinary module jack. Missing that jack falls through to the whole-card
shape-from-a-dropped-cable convenience above.

## Latency

An inlet or outlet reports **zero latency** and does no buffering. The macro adds nothing to
compensate, the graph stays one compensation domain, and
`MainComponent::rebuildGraphForLatencyChange()` needs no change. A macro containing a high-latency
hosted plugin is compensated exactly as that plugin is outside a macro, because it is the same flat
graph it always was.

## Bypass and mute

A macro is not a processor: it has no `processBlock` and no bypass or mute state of its own. "Bypass
macro" and "Mute macro" are **fan-out commands** over its members, applied as one undo step.

- **Mute macro** — `ModuleBase::setMuted(true)` on every member. Each member's own `processBlock`
  honours the contract as written (`buffer.clear()` then return).
- **Bypass macro** — `ModuleBase::setBypassed(true)` on every member. Each member's own dry
  pass-through applies.

Both setters are **parameter writes** (`setValueNotifyingHost` on `bypassedParam`/`mutedParam`), so
the fan-out is an ordinary parameter change: automatable, host-visible and undoable through the paths
that already handle parameter changes. No new mutation mechanism is introduced.

**Why fan out rather than reinterpret.** The bypass and mute contract is per-module and stated in
full in the root `CLAUDE.md` precisely because it is cheap to follow and catastrophic to get wrong. A
macro-level reinterpretation would be a second implementation of the most safety-critical rule in the
codebase; fanning out to the existing per-module implementations leaves nothing new to get wrong.

Mute **skips, never crashes on**, a member with no `"muted"` parameter
(`ModuleBase::hasMuteParameter()` — Macro In and Out and their MIDI variants among them).
`macroBypassState`/`macroMuteState` give the tri-state (`AllOff`, `AllOn`, `Mixed`) a macro with
members in mixed states needs; the collapsed card draws it as two small badges next to the expand
chevron — solid when `AllOn`, half-filled when `Mixed`, undrawn when `AllOff` — read fresh on every
paint and explicitly repainted by both setters, since a collapsed card's members are hidden and it has
no parameter listener of its own to notice the change otherwise.
`toggleMacroBypassed`/`toggleMacroMuted` converge a `Mixed` or `AllOff` state to ON and an `AllOn`
state to OFF, mirroring `toggleSelectionMacrosCollapsed`'s own convergence rule.

**A channel macro is a carve-out.** When a macro contains a `Channel Strip`, the **Bypass** fan-out
skips the strip and the source nodes (Track In, Track Audio): "bypass" on a channel means "bypass the
inserts", so the chain's effects go dry while the source keeps producing and the strip keeps passing
signal. `macroBypassState` reports over the same reduced member set, so a channel whose inserts are
all bypassed reads fully on. **Mute** has no such carve-out — muting a channel macro mutes the strip
too. The filter is `bypassFanOutMembers` in `GraphEditorMacroPortSplice.cpp`; see
[`docs/mixer/mixer.md`](../mixer/mixer.md#bypass-and-mute).

## Hosted plugin

Nothing macro-specific. Because the graph stays flat and macros are canvas presentation, a macro in
the plugin build behaves as it does in the app: it is state in the same `project.json` blob the
plugin already round-trips, and the plugin editor's `GraphEditor` renders cards and hulls
identically. No new host-mode branch.

The one existing rule that still applies: an over-wide hosted plugin is **refused, never truncated**,
and grouping one into a macro changes nothing about that.

## Related

- [`docs/macros/macros.md`](macros.md) — the model and the reason ports are proxy nodes.
- [`docs/macros/configure-io.md`](configure-io.md) — the dialog that adds, renames, reorders,
  recolours and reshapes a port.
- [`docs/macros/auto-ports.md`](auto-ports.md) — every automatic port creation and removal path.
- [`docs/macros/menu-and-membership.md`](menu-and-membership.md) — the macro menu and membership
  changes.
- [`docs/layout/cables.md`](../layout/cables.md) — cable enumeration and colouring.
- [`docs/modules/modulation.md`](../modules/modulation.md) — the logical-port API and poly-bus wires.
