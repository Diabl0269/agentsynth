# Automatic Macro Ports

Every path that creates or removes a macro port without the user configuring one by hand: grouping a
selection whose cables cross the new boundary, dragging a cable across an expanded macro's hull,
ungrouping, deleting a port, and a port losing its last cable. The port model itself is
[`docs/macros/ports.md`](ports.md); the manual dialog is
[`docs/macros/configure-io.md`](configure-io.md).

All of these are Mono only — no automatic path infers Stereo or Poly-N from a cable's own fan — with
one exception: the grouping-time splice is the only source of `StereoCollapsed`, derived from the
internal jack the crossing cable lands on rather than from any user choice.

---

## The auto-port preference

`GraphEditor::MacroAutoPortPreference` is a **tri-state**, not a bool: `Unset` (ask on the next group
with a crossing cable), `AutoCreatePorts`, `LeaveCablesAsIs` — default `Unset`. It is persisted
through `juce::ApplicationProperties` by `PreferencesSettingsTab` under `"macroAutoCreatePorts"`, as
one of `"ask"`, `"auto"` or `"leave"`, with a `Macro auto-ports:` combo row in the Preferences tab
including "Always ask" to go back.

`GraphEditor::requestGroupSelectionIntoMacro()` — what `Cmd+G` (`groupOrToggleSelectionMacros`), the
right-click "Create Macro from N Modules" item and the drag-group canvas menu all call, instead of
`groupSelectionIntoMacro()` directly — shows `synth::ui::MacroAutoPortPromptDialog` **only** when the
preference is `Unset` AND the selection actually has a crossing connection. A grouping with nothing
to decide is never interrupted. The dialog offers "Create Ports" and "Leave Cables As Is" plus a
"Remember my choice" toggle, default on; the choice persists only when remember is checked and
applies once otherwise.

**Why the tri-state defaults to asking, unlike the toggles below.** This preference replaced
pre-existing *silent* behaviour — a crossing cable at group time always wired straight to the
interior member with no port — so asking once lets the user pick a side knowingly.

**The write alone is useless without a matching read.** The tri-state is restored at launch by
`PreferencesSettingsTab::loadMacroAutoPortPreference()`, called from `MainComponent`'s constructor —
the same startup-restore pass as `loadDualIOPerModuleOverrides()`. Without it, the write path and
`PreferencesSettingsTab::setGraphEditor()` only ever pushed the value once a Settings window had
been opened, so a fresh launch left the editor `Unset` and the modal re-asked every session.

**Escape behaves exactly like "Leave Cables As Is"** (`onChoice(false, remember=false)`). The user
already asked to group these modules, so Escape aborting the *whole* grouping would be the surprising
outcome; leaving the boundary cables as-is is the least surprising reading of "close or cancel
without creating a port". `remember` is forced `false` on the Escape path regardless of the toggle's
own state, so a reflexive Escape can never silently pin a permanent preference the way a deliberate
button click legitimately can. `juce::DialogWindow`'s own default Escape handling
(`setVisible(false)`) would instead close the window without ever calling `onChoice`, leaving
`requestGroupSelectionIntoMacro`'s `respond` callback unrun — no macro created and no status message
explaining why — which is why this dialog overrides it.

`GraphEditor::macroAutoPortModalForTest` replaces the real dialog launch with a callback a test
drives directly.

## Auto-creating ports when grouping

`GraphEditor::groupSelectionIntoMacro(bool autoCreatePorts)` takes the decision above;
`autoCreatePorts = false` is the original zero-port behaviour and is still the signature every
pre-existing caller compiles against.

**The crossing-cable gate reads live `NodeID`s, never uuids.** A module freshly dropped on the canvas
has no `"uuid"` property yet — one is assigned lazily on first save, or by `groupSelectionIntoMacro`
itself once it decides to proceed — so gating the check on resolvable uuids would silently
under-detect on the single most common real path: drop two never-saved modules, wire one to an
existing module, group immediately. `buildMacroPortCrossingPlan()` therefore takes
`std::vector<NodeID>` as its primary overload, with a thin uuid-resolving wrapper for
`groupSelectionIntoMacro`, which by the time it calls it has already assigned every member a uuid.
`selectionHasCrossingMacroCable` also mirrors `groupSelectionIntoMacro`'s own "already in a macro"
refusal (`macros.findByMember`) before computing a plan — a selection grouping will refuse outright
has nothing to decide, so the modal must not ask a question whose answer can never be applied.

**The crossing plan.** Before the macro exists, `buildMacroPortCrossingPlan()` reads
`graph.getConnections()` the same way `rebuildVisibleCables()` and `AudioEngine`'s routing
enumeration do — through `LogicalPort::role`, `visibleJackIndex`, `isPolyGroupHead` and
`polyVoiceSpan`, never a raw-channel guess — and groups every connection with exactly one endpoint
among the about-to-be members into a `MacroPortCrossingGroup`, keyed by **(internal node, direction,
visible jack)**. That key is the de-duplication rule: two cables landing on the same internal jack (a
collapsed stereo pair's two raw legs, or several external sources fanned into one jack) share ONE
port.

A group's shape is read off the jack's own head raw channel, never defaulted:

- `polyVoiceSpan > 1 && role == Audio` is **`StereoCollapsed`**. A span-2 head means the internal
  jack is COLLAPSED — one visible jack for both raw legs, the FX pattern every Delay and Reverb
  defaults to — so the port must present that same one jack, never the two-jack `Stereo` shape a
  hand-picked choice means. Grouping FX modules with one "Audio" jack each otherwise produced
  two-jack Stereo ports that mismatched the module they front.
- `polyVoiceSpan > 1` otherwise is **Poly-N** with that exact voice count.
- Anything else is **Mono**.
- A MIDI connection is its own group kind entirely, yielding `MacroMidiInlet`/`MacroMidiOutlet`,
  never an audio port.

A Dual-I/O-on module's Left and Right legs sit on two SEPARATE visible jacks rather than one span-2
jack (`polyVoiceSpan == 1` on each, so each starts life as its own Mono group), so a **second merge
pass** folds them into one two-jack `Stereo` group when a crossing connection reaches both — paired
via `ModuleBase::rightAudioLegChannel()`, never jack index 0 or 1
(`Source/Modules/CLAUDE.md`), which is what makes it correct for the split-block voice-module layout
this pass exists for.

**The splice.** `spliceMacroPorts()` then, for each group: disconnects every original
external-to-internal edge, constructs the port node with the derived shape and kind — named from the
internal module's own name plus `getInputPortLabel`/`getOutputPortLabel` at the jack it fronts
("Filter Cutoff", never "Input 2") — adds it as a macro member with a `MacroPort` entry, and
reconnects external to port to internal (or the reverse for an outlet) on exactly the raw channels the
original edges used.

**All of it runs inside `groupSelectionIntoMacro`'s own ONE `recordGraphAndMacroChange`
transaction** — `macros.add()` and every splice together — so a single `Cmd+Z` undoes the grouping
and every spliced port. Group-then-add-as-a-second-pass is deliberately rejected.

**Ordering: splice before `updateComponents()`.** `updateComponents()` is what lays out the card
jacks (`macroCardPortLayout`) and docks the expanded hull's port widgets (`dockMacroPortWidgets`)
against however many ports exist, so `groupSelectionIntoMacro` calls `spliceMacroPorts()` and then
`updateComponents()`, never the reverse. The card's own `macro.bounds` is seeded from the ORIGINAL,
pre-port selected members' bounds before any splicing happens, and `macroHullBounds()` and
`applyMacroCollapsed`'s hull-seeding union both exclude port members
([`docs/macros/ports.md`](ports.md#how-a-port-is-drawn)) — so there is nothing for the splice to
retroactively distort.

**A second caller: "Make channel".** `GraphEditor::makeChannelFromNode` boxes each new channel with
the same group-time pass (`buildMacroPortCrossingPlan` before `macros.add`, then
`spliceMacroPorts`), always creating ports regardless of the preference above — a shared module
reaching into the channel is exactly what a port is for — with ONE filter: **a group whose internal
node is the new Channel Strip and whose direction is outward is dropped**, so Strip to Master (and
Strip to a merge point's bus) stays a plain edge that `spliceMasterNode`'s Mix-versus-Direct
classification can see. The plan builders themselves are unchanged. See
[`docs/mixer/mixer.md`](../mixer/mixer.md#make-channel-and-shared-modules).

## A modulation cable through an attenuverter

`AudioEngine::addModRouting` always wraps a single-slot CV routing as source to attenuverter(ch0) to
destination, and **the attenuverter itself can never be a macro member**: it never gets a
`ModuleComponent` (`GraphEditor::updateComponents()` skips it outright), so it can never be part of a
canvas selection. A crossing connection whose EXTERNAL endpoint is the attenuverter therefore means
one of two different things, told apart by looking at the mod chain's OTHER real endpoint — the
attenuverter's other ch0 connection:

- **Both real endpoints are about to become members** (an ADSR and the VCA it drives, selected
  together). The attenuverter sitting nominally outside is then only an artefact of its own
  invisibility, not a real crossing, and splicing would spawn TWO spurious ports for a routing being
  grouped wholly inside the macro. Both edges are left un-ported, exactly like any other
  fully-internal connection.
- **The far endpoint is genuinely external**, or the chain is only half-wired. This IS a real
  crossing and does get a port.

**Why retargeting the attenuverter's own edge is safe.**
`AudioEngine::getModulationRoutings()`'s AttenuverterChain pass is keyed purely on the
ATTENUVERTER's own node identity — it walks every `AttenuverterModule` node and reads whichever
connections currently sit on its ch0 in and out, never what those connections point at — so moving
the attenuverter's edge onto the new port, with the attenuverter standing in as the "external" node
for the splice, does not desync that classification. The routing still appears in
`getActiveModRoutings()` (the mod matrix), now reporting the port as its source or destination,
exactly how any other boundary-crossing cable reports the port it passes through rather than the
member further inside. The modulation signal keeps flowing because the port is a pure pass-through,
and `buildVisibleCables()` keeps drawing the BOUNDARY segment as ONE `AttenuverterChain`-kind wire
(ModCV-coloured, carrying the knob) all the way to the port's own jack; collapsed, that lands on the
port's card jack via the same re-anchoring machinery every other ported cable uses. The wholly
INTERNAL leg (member to port, on the outlet's side of the chain) renders as an ordinary `Direct`
cable when the macro is EXPANDED, and is invisible while it is collapsed, like every cable wholly
inside a collapsed macro.

**The knob's hit-test must read the painted geometry, never re-derive it.**
`GraphEditor::getAttenuverterNodeAt` — the hit test both the drag-to-adjust and the
double-click-to-delete gestures share — once re-derived the chain's two real endpoints straight from
`graph.getConnections()` and `ModuleComponent::getPortCenter()` with the RAW channel index, never
`mapOutputChannel`/`mapInputChannel`'s visible-jack mapping, and with no collapsed-macro
re-anchoring. That is a second, independent geometry computation from the one
`buildVisibleCables()`/`paint()` use, which the class comment above `GraphEditor::CableId` exists
specifically to rule out. Once either endpoint became a macro port node the two computations diverged
by roughly 300 px in EVERY collapse state, including both macros fully expanded — the knob rendered
in the right place and clicking it did nothing. `getAttenuverterNodeAt` now searches
`buildVisibleCables()` for the `AttenuverterChain` cable and hit-tests its own `(p1+p2)/2` midpoint,
so it can never disagree with what is painted. This matters most when a chain crosses TWO macro
boundaries — an outlet port on one macro and an inlet port on another, with the attenuverter
sandwiched between them — which each splice already handles correctly on its own.

**The mod matrix's destination combo needs a display-only substitute.** `MacroInletModule`
deliberately declares no `getModulationTargets()`: `GraphEditor::connectPorts()` relies on that empty
list to keep a plain cable drop onto a Macro In's jack a plain connection, never auto-wrapped in a
fresh attenuverter. So a `ModMatrixComponent::ModRow` whose destination is a spliced port has nothing
to resolve its combo selection against and would render blank. Rather than give `MacroInletModule` a
real `ModulationTarget` — which would resurrect the auto-wrap problem for every ordinary cable drop —
`destinationCandidatesForCombo` substitutes a display-only synthetic target at channel 0 whenever the
module has none AND is a `MacroInletModule`. A spliced port from this path is always Mono with its one
active channel at 0, because the internal jack an `AttenuverterChain` lands on is never poly-fanned,
so channel 0 is the only candidate.

## Ungroup and direct deletion of a port

### Ungroup splices every port back out

**Ungrouping a macro removes every one of its port nodes and splices the ORIGINAL CABLE BACK** —
external reconnects straight to internal, exactly as it was before grouping — as part of the SAME
undo step that removes the `Macro` record. Group then Ungroup is a true round trip, which is the
property this rule mostly exists for. A macro's ORDINARY members are still never touched by ungroup;
only its ports are.

**This applies to every port, automatic and hand-added alike, and no provenance field exists.** A
port exists only to proxy a boundary, so once the boundary is gone a hand-named port is the same kind
of orphan as an automatic one, with no reason to treat them differently. The alternative — leaving a
port's fronting node behind as an ordinary ex-member — produced a stray "Macro In" or "Macro Out" box
sitting mid-signal-chain with no delete affordance of its own, since Configure I/O disappears the
instant the macro does (`promptConfigureMacroIO`'s first line is `macros.find(macroId)`).

**The splice-out algorithm, `GraphEditor::spliceOutMacroPort`, is the reverse of
`spliceMacroPorts`.** For the port node fronted by one `MacroPort`: read every connection currently
touching it and split into the set LANDING on it (the port's own channel is the destination, an "in"
edge) and the set LEAVING it (the port's own channel is the source, an "out" edge); then, for every
port channel shared between an in-edge and an out-edge, connect that in-edge's OTHER endpoint
directly to that out-edge's OTHER endpoint, on the exact raw channels each already carried — the full
**cross product**, so fan-in (two external sources sharing one inlet port, the "share ONE port" dedup
rule above) and fan-out reconnect completely, not just the first pair. **A port wired on only one
side, or neither, contributes no pairs and simply disappears** — there is nothing to reconnect.
Removing the node then drops its own now-superseded connections for free
(`juce::AudioProcessorGraph::removeNode` calls `disconnectNode` before erasing). It is
signal-preserving because a port is a verified pure per-channel pass-through on every shape
([`docs/macros/ports.md`](ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed)).

**Ungroup reaches the graph-change notification path.** It was once metadata-only (`MacroSet::remove`
alone) and never needed `GraphEditor::updateComponents()`'s post-mutation hooks. It now removes nodes
and rewires connections — a real graph edit — so it calls `updateComponents()` inside the SAME
`recordGraphAndMacroChange` transaction that does the splicing, exactly as `deleteSelection()` does.
That fires `onGraphStructureChanged` to `MainComponent::reconcileTimelineBindingsOnly()`, the seam
that keeps a timeline binding from surviving stale, keyed to a now-deleted node's uuid, into the next
audio-thread render pass (root `CLAUDE.md`, and the hook inventory in
[`docs/architecture_app_wiring.md`](../architecture_app_wiring.md)).

### A port node is directly deletable

A port widget's body refuses any LEFT click — no selection, no drag — because
`dockMacroPortWidgets()` repositions it every layout pass regardless, so a draggable port would just
snap back the moment the user let go. **A right click is the one exception**: it opens
`ModuleComponent::buildMacroPortContextMenu()` — "Delete Port" always, plus "Rename Port..." (a
lightweight `AlertWindow` through `GraphEditor::promptRenameMacroPort`, the quicker alternative to
opening the whole dialog to retype one name) and "Configure I/O..." when the port still resolves to a
live macro.

Deleting a port this way while its macro stays alive shares `spliceOutMacroPort` with ungroup
(`GraphEditor::deleteMacroPortNode`) — the cable is spliced back, never dropped — and **the macro
dissolves outright if this was its last member**, mirroring `MacroSet::removeMemberEverywhere`'s own
"zero members is not a meaningful state" rule.

Deleting a port from the Configure I/O dialog is the one path that drops the cable instead; see
[`docs/macros/configure-io.md`](configure-io.md#deleting-a-port-from-the-dialog).

## Ports on a cable drag

### Auto-creating a port on a drag

`GraphEditor::maybeAutoCreateMacroPortsForDrag`, called from `endConnectionDrag`. The module-jack
hit-test loop that resolves a completed drag to two real jacks — which also covers the knob-drop
fallback, from the same call site — runs this check before falling back to a plain `connectPorts()`.

**The endpoint-needs-a-port rule**, applied independently to each of the drag's two resolved
endpoints. An endpoint needs a NEW port on its own macro if and only if:

1. it resolves to an ORDINARY member of that macro (`memberIsPort()` false — a drag landing exactly
   on an EXISTING port's own jack never mints a second one), and
2. the OTHER endpoint is not a member of that same macro at all, ordinary member or port alike. "Not
   a member" is one `Macro::hasMember()` check, since a port's own uuid is always also a `members`
   entry.

So two members of the SAME macro dragged together wire straight through, and two members of DIFFERENT
macros each mint their own port — an outlet on the source's macro, an inlet on the destination's —
wired port-to-port for the boundary leg. When only one endpoint needs a port, the OTHER leg of the
final connection lands directly on the plain module (or on the existing port, if that is what it
resolved to), never a second port.

**A mod or CV-routed drag goes through the SAME mint-and-wire path as a plain audio drag** — no
separate scope cut and no mirrored CV-detection helper. `connectPorts()`'s own CV and mod-routing
detection (`resolvePolyLink`, single-voice, the destination's `getModulationTargets()`) naturally
wraps whichever leg's real endpoint is a genuine modulation-target parameter in a hidden
`AttenuverterModule` via `addModRouting()`, exactly as it does for a plain non-macro CV connection. A
freshly minted `MacroInletModule`/`MacroOutletModule` is never itself a modulation target, and the
attenuverter node can never be a macro member, so wiring member-to-port can never accidentally
attract the wrap; only the LAST leg wired — the one whose destination is the real modulation target —
is ever a candidate. An earlier cut that bailed out before minting anything for a CV drag was
unnecessarily narrow and was removed outright.

**Both sides of every minted port are wired**, unlike the collapsed-card cable-drop convenience,
which wires only the external side because a collapsed macro has no visible interior to wire to.
Here the macro is necessarily EXPANDED — both endpoints are real, visible `ModuleComponent`s, which a
collapsed macro's hidden members are not — so the interior leg is wired too.

**The whole mint-and-wire sequence is ONE `recordGraphAndMacroChange` transaction**: up to two new
port nodes, up to three `connectPorts(..., recordUndo=false)` calls and one `updateComponents()`, so
a single `Cmd+Z` undoes all of it together.

**`maybeAutoCreateMacroPortsForDrag` takes a trailing `recordUndo = true` parameter.** A caller
already inside its own undo transaction — the mixer's auto-channel hook in `endConnectionDrag`, which
must cover macro-port creation, the connection AND a possible new channel as ONE step — passes
`recordUndo=false` to fold this function's mutation into that outer transaction instead of opening a
nested one, and takes over calling `updateComponents()` itself once, after every mutation, rather than
getting one call per nested transaction. Every other call site keeps the default and behaves
identically to before. See
[`docs/mixer/mixer.md`](../mixer/mixer.md#auto-creating-a-channel-on-connect).

### Auto-deleting a port when its last cable goes

`GraphEditor::autoDeleteOrphanedMacroPort`. When a mutation drops a connection touching a macro port
node down to **zero** remaining connections, the port is spliced out (`spliceOutMacroPort`, the same
helper ungroup and direct deletion share) and the macro is dissolved too if that was its last member.
A port with one of its two legs still wired — or one of several fan-in or fan-out connections still
wired — survives; only reaching zero triggers the splice.

**Hooked at explicit user-gesture call sites, each checked BEFORE mutating so the right undo
transaction is chosen up front:**

- `GraphEditor::disconnectCable` — upgraded from its graph-only `recordStructuralChange` to
  `recordGraphAndMacroChange` ONLY when at least one of the cable's two logical endpoints
  (`cable.id.srcUid`/`dstUid` — the REAL endpoints even for an `AttenuverterChain` cable, never the
  hidden attenuverter itself) resolves to a macro port. An ordinary cable's removal is unchanged.
- `GraphEditor::disconnectPort` — the same treatment, upgraded only when the clicked jack's own node,
  or the far end of any connection about to be removed, resolves to a macro port.
- `deleteSelection`, `deleteModule` and `requestDeleteModule` — deleting an ORDINARY member can
  strand a *different* port cableless (the member was that port's only remaining connection), which
  neither call site above can catch, since neither the port nor its cable is directly involved in the
  gesture. `GraphEditor::macroPortDeletionNeighbors` is the batch-deletion counterpart: called BEFORE
  any node in the batch is removed from the graph, it walks the live connection list and returns
  every node OUTSIDE the deletion set that has a direct connection to a node INSIDE it. These three
  already always use `recordGraphAndMacroChange`, so there is no undo-transaction decision to make
  here — only which nodes to check once the batch removal completes. After `removeNode` runs for
  every id in the batch, `autoDeleteOrphanedMacroPort` runs once per captured candidate, the same
  self-checking primitive the other call sites use: it no-ops for anything that is not a live macro
  port, or that still has a connection surviving elsewhere.

**This is deliberately single-hop.** Splicing out a candidate can itself strand a second port that
was wired only to the first — two ports can be directly wired port-to-port for a cross-macro-boundary
crossing — and that second port is not chased. Nothing here cascades beyond one hop.

`modMatrix.clearRows()` needs no special interaction handling: it is `rows.clear(); repaint();`, a
blunt UI-state clear with no node scoping of its own, so it stays where it already runs (once, at the
top of each function's undo-transaction lambda) and tolerates being called ahead of an auto-delete
sweep.

### Both behaviours are plain on-off Preferences toggles, default ON

`GraphEditor::setAutoCreateMacroPortsOnDragEnabled` and
`setAutoDeleteMacroPortsOnLastCableEnabled`, persisted by `PreferencesSettingsTab` under
`"macroAutoCreatePortsOnDrag"` and `"macroAutoDeletePortsOnLastCable"`, each independent of the other
and of the grouping-time tri-state above.

**Why not a third tri-state.** These are brand-new automations shipped as the default behaviour, with
a plain escape hatch for the user who wants the wire-straight-through or leave-a-cableless-port
behaviour instead. There is no pre-existing silent default to protect an expectation of, which is the
one thing the grouping-time tri-state's "ask" default exists for. Off, `endConnectionDrag`'s
boundary-crossing branch and `disconnectCable`/`disconnectPort`'s zero-connections check are both
no-ops, reproducing the exact code paths from before these automations existed.

**Rendering is unaffected, confirmed rather than assumed.** A drag-minted port is the same
`MacroInletModule`/`MacroOutletModule` node, laid out by the same
`dockMacroPortWidgets()`/`macroCardPortLayout()` every other port uses, and the cable it is wired with
goes through the same `connectPorts()`/`buildVisibleCables()` path as any other connection. A
splice-out on auto-delete is the identical `spliceOutMacroPort` call direct deletion makes, so the
resulting graph and macro state is indistinguishable from a port deleted that way — nothing in
[`docs/macros/ports.md`](ports.md#cable-rendering-across-the-boundary)'s table changes.

## Related

- [`docs/macros/macros.md`](macros.md) — the macro model and why ports are proxy nodes.
- [`docs/macros/ports.md`](ports.md) — the port model, shapes and boundary rendering.
- [`docs/macros/configure-io.md`](configure-io.md) — the manual dialog.
- [`docs/macros/menu-and-membership.md`](menu-and-membership.md) — membership changes, which run the
  same crossing-plan machinery incrementally.
- [`docs/mixer/mixer.md`](../mixer/mixer.md) — "Make channel", the second caller of the grouping-time
  splice.
- [`docs/modules/modulation.md`](../modules/modulation.md) — mod routings and the attenuverter.
