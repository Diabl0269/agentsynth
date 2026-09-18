# Macro Menu and Membership

Where a macro's own actions are reachable from, and every way a macro's membership changes: the menu
items, a Cmd-drag across the hull border, and the collapse control on an expanded hull. The port
machinery every membership change runs is [`docs/macros/auto-ports.md`](auto-ports.md).

---

## The macro menu's entry points

`GraphEditor::buildMacroMenu` is the ONE builder for a macro's own actions — Expand and Collapse,
Rename, Change Colour, Configure I/O, Bypass and Mute, Save as Snippet, Ungroup, Add Selection to
Macro, Remove from Macro, Delete Macro and Modules. Three sites reach it, all funnelling through it
rather than keeping their own copies:

1. **The collapsed card's own right-click** (`MacroCardComponent::showContextMenu`). It passes its
   own inline-rename callback as `buildMacroMenu`'s `renameAction` override, which the other two
   sites have no card to host.
2. **Right-clicking empty canvas inside an expanded macro's hull** (`GraphEditor::mouseDown` through
   `macroHullAt()`), reachable without collapsing first.
3. **Right-clicking a MEMBER MODULE itself** (`ModuleComponent::buildModuleContextMenu`) — appends
   `buildMacroMenu()`'s items as a "Macro: `<name>`" submenu on the module's own right-click menu,
   when and only when the clicked module resolves to a macro via `MacroSet::findByMember()`. A module
   in no macro sees no change to its menu at all. This site deliberately does **not** pre-select the
   macro at click time: a member module's own right-click retargets the *module* selection, so its
   own Copy, Duplicate and Delete Module items act on what was clicked, and macro-wide selection
   happens only if and when a macro submenu item is invoked.

## Ungroup and Save as Snippet act on the selection

`buildMacroMenu()`'s "Ungroup" and "Save as Snippet..." act on the **current selection**
(`ungroupSelection()` / `onSaveSnippetRequested`), not on the macro id the menu was built for, so
**`buildMacroMenu()` itself selects that macro** (`selectMacro(macroId, false)`) immediately before
running either one, rather than leaning on each caller to have pre-selected it.

**Why this is a behaviour rule and not plumbing.** Only entry point 2 ever pre-selected the macro.
Without the rule, right-clicking one collapsed card while *other* macros' cards were also selected
and choosing "Ungroup" dissolved every selected macro rather than the one right-clicked — and
invoking "Ungroup" from a member's macro submenu while a *different* macro's module was also selected
dissolved both. It now acts only on the macro whose menu was opened, which is what a user reading
"Ungroup" off that card's own menu expects.

Entry point 2's own `selectMacro()` call is redundant as a result but is left in place;
`GraphEditor::mouseDown`'s own comment calls it load-bearing rather than cosmetic, because `mouseUp`
deliberately preserves whatever was selected on a right-click so the canvas menu's Paste keeps
working.

## Adding to and removing from a macro

`GraphEditor::addSelectionToMacro` and `removeSelectionFromMacro` are the incremental counterparts to
`groupSelectionIntoMacro`/`ungroupSelection` for a macro that already exists, offered by
`buildMacroMenu` as two items right after "Ungroup". Before them, the only way to change who is in a
macro was to ungroup it — dissolving the record — and group again.

- **"Add Selection to Macro" is computed from a candidate list captured BEFORE either call site's own
  forced reselect, never from the selection `buildMacroMenu` sees when it runs.** Both the collapsed
  card's right-click and the expanded hull's empty-space right-click call `selectMacro(macroId,
  false)` before showing the menu at all (the card or hull highlights what is about to be acted on),
  so by the time `buildMacroMenu` runs "the current selection" is already just this macro's own
  members and any external batch the user picked before right-clicking is gone. Reading the live
  selection inside `buildMacroMenu` therefore shipped the item silently unreachable from BOTH real
  entry points, caught only by live GUI testing — every test up to that point called
  `buildMacroMenu()` directly with `setSelectedNodes()` already set, which never exercises the real
  `mouseDown`-and-reselect sequence a live click goes through.
  `buildMacroMenu(macroId, renameAction, addCandidateSelection)` takes an optional third parameter, a
  `const std::vector<NodeID>*`; both call sites capture `getSelectedNodes()` themselves one line
  before their own reselect and pass the address through. **"Remove from Macro" is unaffected and
  still reads the CURRENT live selection** — correct either way, since after either reselect it
  equals the macro's own members, which is exactly what removal should see. The member-submenu graft
  passes no override and falls back to the live selection, since its own
  retarget-if-not-already-selected never destroys an external batch the same way.
- **The forced reselect is skipped whenever there was ANY prior selection at all.** Both call sites
  check `priorSelection.empty()` before calling `selectMacro`/`isMacroSelected`. Any non-empty prior
  selection, addable or not, survives untouched along with its selection border; the reselect fires
  only for the genuinely empty case, which is what lets "Remove Selection from Macro" and "Ungroup"
  find the macro's own members via live selection when nothing else was selected. Narrower conditions
  were tried and were both wrong: gating on "the prior selection has something addable" fixed a user
  seeing no cue for what "Add Selection to Macro" was about to insert, but left the symmetric bug
  that selecting a **partial subset of the macro's own members**, meaning to remove just that subset,
  still force-reselected every member — because a pure-member subset has nothing addable either.
- **A top-level "Remove from Macro" item sits on a member module's own menu**, alongside the nested
  "Macro: `<name>`" submenu's copy of the same verb, because the nested one was correct but
  undiscoverable: a user's first instinct right-clicking a module they want out of a macro is to
  right-click the module, not to open its macro's submenu first.
  `ModuleComponent::buildModuleContextMenu` grafts it next to "Expand/Collapse Macro", calling
  `GraphEditor::removeNodeFromMacro(nodeId)`, which resolves that node's own uuid and calls
  `removeSelectionFromMacro(macro->id, {uuid})` directly, **never the live selection** — so it always
  acts on exactly the module whose card was right-clicked, regardless of what else is selected. It is
  only reachable for an ordinary member: `mouseDown` routes a port module (`isMacroPortType`) to
  `buildMacroPortContextMenu()` before `buildModuleContextMenu` is ever built, so
  `macroForNode(nodeId) != nullptr` here always means a real member.
- **Each item is omitted, not shown disabled, when it would have nothing to do** — mirroring "Mute
  Macro"'s own precedent of omitting a command that can only ever no-op. "Add Selection to Macro"
  needs the captured selection to contain at least one uuid not already a member of THIS macro;
  "Remove from Macro" needs at least one ordinary, non-port member of THIS macro in it. The label
  pluralises ("Remove Selection from Macro") when more than one member is being removed.
- **Ports are never pulled out of `members` by either path.** A port's uuid in the selection is
  skipped by `removeSelectionFromMacro`, not merely hidden from the count, since a port has its own
  delete affordance and removing one through this generic path would desync the "every port's
  `nodeUuid` is a member" invariant `MacroSet::fromVar` enforces on load.
- **One `recordGraphAndMacroChange` undo step per gesture**, matching every other macro mutation —
  covering the port splices and the membership change together, not as two separate steps.

## Incremental port splicing on a membership change

A membership change can make the crossing set change in **both directions at once**, which the plain
creation-time `buildMacroPortCrossingPlan` was never built to see past a full "inside" set passed in
one shot. Three members, each scoped to exactly one direction of one operation, do it instead:

- **`buildMacroPortCrossingPlanForNewMembers(macroId, addedUuids)`** — the plan for
  `addSelectionToMacro`. Computes `buildMacroPortCrossingPlan` over (existing ordinary members plus
  `addedUuids`), then keeps only groups whose internal node is one of `addedUuids` — a group fronting
  an already-established member is a pre-existing un-ported crossing this add did not create, and is
  left alone — and drops any edge whose external endpoint is one of the macro's OWN existing ports,
  since that crossing is not new either.
- **`macroPortsThatBecomeInteriorOnAdd(macroId, addedUuids)`** — the reverse direction. If the joining
  member was already wired straight into one of the macro's EXISTING ports from outside, that port's
  job is now redundant (both its ends are interior) and it must be spliced back OUT into a plain
  direct connection, or the join leaves a pointless double port (port to new port to joining member).
  It returns only ports where *every* remaining connection's other side is now interior; **a port
  with even one connection to a node that stays genuinely external is never returned**, since
  splicing it out would silently drop that cable — `spliceOutMacroPort`'s "wired on only one side,
  so it disappears" behaviour is safe only when the whole macro is dissolving alongside it.
- **`buildMacroPortCrossingPlanForRemovedMembers(macroId, removedUuids)`** — the plan for
  `removeSelectionFromMacro`. Computes `buildMacroPortCrossingPlan` over the REMAINING ordinary
  members (existing minus `removedUuids`, still excluding the macro's own ports), then keeps only
  edges whose external endpoint is actually one of `removedUuids` — otherwise a remaining member's
  pre-existing, already-ported connection would look like a brand-new crossing too, since its port is
  excluded from the "inside" set like any other port, and would get double-ported.

`buildMacroPortCrossingPlan`'s own `memberUids.size() < 2` early return was removed: the incremental
callers legitimately need a crossing plan for a one-member "inside" set — removing one of a macro's
two ordinary members leaves exactly one remaining member whose newly external cable still needs a port
— and the loop itself was always correct for any size.

**Ordering matters in both directions.** `addSelectionToMacro` splices new ports in, THEN splices
redundant ones out, both inside the same `recordGraphAndMacroChange` transaction as the membership
change. `removeSelectionFromMacro` splices new ports in BEFORE the membership removal, because
`spliceMacroPorts` needs `macros.find(macroId)` to still resolve and `removeMemberEverywhere` can
dissolve the macro record outright if the removal drops its last member — doing the splice first
means that dissolve, if it happens, always lands after the boundary is already correct.

Without this, removing a module from a macro left its cable correctly rendered as an un-ported
boundary crossing but with no real port, unlike what a from-scratch grouping would have given it.
Configure I/O remains the manual way to add or reshape a port beyond what either automatic path
covers.

## Cmd drag across a hull border

**Cmd-drag a module across an EXPANDED macro's hull border to add it to, or remove it from, that
macro** — the drag itself decides membership, with no separate confirmation step. Before this the
only routes were the menu items above and ungroup-then-regroup, both explicit and several clicks away
from the drag the user is already doing.

**The query, `MacroGroupController::macroDragJoinOrLeaveTarget`** (declared beside `macroHullBounds`
and `macroHullAt`), is given the dragged node's id and its CENTRE — not its top-left — in canvas
coordinates, and answers with a macro id or empty:

- **JOIN** — the dragged node is in no macro: test its centre against the plain `macroHullAt()`,
  which already only considers EXPANDED macros (a collapsed one is never a candidate, since its
  members are hidden `ModuleComponent`s that cannot be dragged at all) and picks the smallest hull
  when more than one overlaps.
- **LEAVE** — the dragged node IS a member of macro X: test its centre against
  `macroHullBoundsExcluding(X, ownUuid)`. This variant exists because `macroHullBounds()` is a LIVE
  union of member bounds, so a member dragged OUTWARD keeps inflating its own hull's union and could
  never test as outside, and so could never leave. One consequence worth knowing: for a two-member
  macro, `macroHullBoundsExcluding` reduces to just the OTHER member's own footprint, so almost any
  Cmd-drag of either member reads as outside it — by design, not a bug; the wider the macro, the more
  room a member has to move before crossing out. With three or more members the exit distance is
  direction-dependent but never unbounded: three members in a horizontal row is the worst case (the
  two outer members keep the excluding hull wide along the row), and leaving is still reachable, just
  further along the row, and short in the perpendicular direction.

**Membership is checked FIRST, so one continuous drag never does both.** A member dragged out of macro
A directly into macro B's hull LEAVES A and does not also join B — `macroDragJoinOrLeaveTarget`
returns the LEAVE target as soon as it finds one and never falls through to test JOIN against B in the
same call. Joining B is a second, separate drag, started from outside any macro.

**The gesture mirrors `ModuleComponent`'s existing Ctrl deferred classification exactly**
(`ctrlTogglePending`/`ctrlPressSelection`): Cmd-press arms a `cmdReparentPending` flag and
`cmdPressSelection`, collapses the selection onto the pressed module, and falls through to arm the
drag like a plain click would; Cmd-click with no movement completes as the deferred additive-select
toggle, exactly like Ctrl's. `GraphContentComponent::paint()` reads
`GraphEditor::getMacroDragCandidateId()` and draws the SAME dashed hull stroke heavier and fully
opaque for whichever macro is the live candidate, rather than inventing a second visual language for
"about to change".

**Whether a drag can reparent at all is a THIRD, separate flag — `reparentArmed` — not derived from
`ctrlTogglePending || cmdReparentPending`.** That OR seemed safe (one of the two is always true
exactly when a drag might cross a hull) and was wrong: on macOS Ctrl and Cmd are genuinely distinct
keys, so a plain Ctrl-drag — the shipped insert-between gesture, unrelated to membership — also sets
`ctrlTogglePending`, and the OR silently let it reparent whenever it happened to cross a hull,
compounding two gestures nobody asked to combine. `reparentArmed` is set to
`e.mods.isCommandDown()` alone, unconditionally, at the top of `mouseDown`'s modifier chain, before
the `isCtrlDown()`/`isCommandDown()` branches, so a click or drag with neither modifier reaches them
with it already false. The platform matrix that falls out:

| Platform | Gesture | `reparentArmed` | Result |
|---|---|---|---|
| macOS | Cmd-drag | true | reparent (join or leave) only |
| macOS | Ctrl-drag | false — `isCommandDown()` is false, distinct keys | insert-between only, unchanged |
| Windows and Linux | Ctrl-drag, which IS Cmd-drag (`commandModifier` is `ctrlModifier`) | true | **both**: insert-between AND reparent, if the drag crosses a hull |

`mouseDrag` recomputes the candidate on every tick but ONLY while `reparentArmed`, via
`GraphEditor::updateMacroDragCandidate`, which stores it in `macroDragCandidateId_`
(`getMacroDragCandidateId()` is the public accessor).

**`reparentArmed` is re-derived on every `mouseDrag` tick for a SINGLE-module drag, not latched once
at `mouseDown`.** Sampling only at press time meant the user had to already be holding the modifier
before grabbing the module, with no way to discover the gesture since the candidate highlight never
lit up. `mouseDrag` re-reads the live modifier each tick, gated on `!isSelectionDragActive()`: a
multi-selection group drag keeps whatever `mouseDown` latched for its whole gesture — reparenting one
member out of a group drag is ambiguous, in both which macro and which dragged module, and stays out
of scope — but an ordinary single-module drag can have the modifier pressed or released mid-gesture
and see the candidate highlight arm or clear immediately.

**The painted hull for the macro a drag is pulling a member OUT of shrinks away from that member
immediately, not only once LEAVE actually arms.** `macroHullBounds()` is a live union, so painting it
directly for the module's own about-to-be-left macro made the outline chase the module as it was
dragged out — pulling a member toward the edge of a two-member macro looked like it was growing the
hull to stay around it, making "remove from macro" look impossible. `GraphEditor` tracks
`macroDragDraggedNodeId_`, set and cleared by the exact same
`updateMacroDragCandidate`/`clearMacroDragCandidate` calls as `macroDragCandidateId_` (one lifetime,
not two) and set FIRST, unconditionally, before the candidate itself is computed — so the shrink
starts on the very first tick, before any LEAVE candidate has armed.
`GraphEditor::paintedMacroHullBounds(macroId)` is what `paintExpandedMacroHulls` calls instead of
`macroHullBounds` directly: it returns `macroHullBoundsExcluding` for the macro the dragged module
currently belongs to, and the ordinary live `macroHullBounds` for every other macro, including one
the drag might JOIN, which by definition is not the dragged module's current macro. **Hit-testing
(`macroHullAt`, used by click-to-select and the hull's right-click menu) is unaffected and keeps
using `macroHullBounds` — this substitution is paint-only.**

**The Windows and Linux arbitration happens at `mouseUp`, and only decides WHICH of the two gestures
a reparent-armed drag ends up as, never WHETHER one can fire** — that is `reparentArmed`'s job,
decided at press. If the last candidate `mouseDrag` computed is non-empty, `mouseUp` reparents;
otherwise it falls through to the plain finalize path, which carries Ctrl's own insert-between
behaviour (`SmartConnectionEngine` samples `isInsertModifierDown()` live, from inside
`finalizeModuleDrag`). Because Windows and Linux cannot express "Ctrl but not Cmd" at all, a
Ctrl-drag there that crosses a cable AND a hull in the same gesture performs BOTH. That is a genuine
platform limitation, not a bug — there is no way to offer the two gestures as separately addressable
without a second, unrelated modifier.

**One undo step, and it must reuse the mousedown-time graph capture.**
`GraphEditor::finalizeMacroMembershipDrag` is modelled on `finalizeMacroCardDrag`: one lambda runs the
ordinary `finalizeModuleDrag` and then the membership mutation, and the whole lambda goes to ONE
`recordGraphAndMacroChange` call. `addSelectionToMacro`/`removeSelectionFromMacro` both take a
trailing `recordUndo = true` parameter for this — `recordUndo=false` skips their own
`recordGraphAndMacroChange` and runs the mutation directly, so the outer finalize owns the one
transaction.

The graph "before" half of that transaction is deliberately NOT `recordGraphAndMacroChange`'s own
default fresh `graphToJSON()` capture: it is whatever `ModuleComponent::mouseDown`'s
`captureBeforeState()` stashed, handed back via `AppUndoManager::takeCapturedGraphBeforeState()` and
passed as the `graphBeforeOverride` parameter. **This is load-bearing.**
`ModuleComponent::moved()` fires on EVERY position change, including every live `mouseDrag` tick, and
unconditionally writes the module's current, mid-drag, unsnapped position into its graph node's
properties right then — not only once at finalize. By the time `finalizeMacroMembershipDrag` runs, a
fresh capture would already see that contaminated mid-drag position as "before", and the combined undo
would land the module wherever the live drag last released it rather than at its true pre-drag
position. Consuming the mousedown-time capture is what every ordinary body drag already relies on via
the plain `captureBeforeState()`/`pushSnapshotFromCapture()` pair. `ModuleComponent::mouseUp` must
NOT also call `pushSnapshotFromCapture()` on the reparent path — that would push a SECOND,
position-only undo step from the same capture — and it does not need to discard it either, since
`finalizeMacroMembershipDrag` consumes it via `takeCapturedGraphBeforeState()`, which clears it as a
side effect.

A crossing drag reuses the incremental membership path wholesale
([Incremental port splicing](#incremental-port-splicing-on-a-membership-change)), so it gets exactly
the same auto-port creation and splice-out-when-interior behaviour as the menu-driven add and remove,
with no new port logic of its own.

## The expanded hull's collapse button

`GraphEditor::macroCollapseButtonBounds()` is the ONE definition of a small square hit zone at the
RIGHT end of the same top-of-hull row `macroChipBounds()` occupies at the left end — the two together
read as one row-spanning control, chip on the left for drag and rename, button on the right for
collapse.

**Why it exists.** The collapsed card has always drawn a visible expand chevron
(`MacroCardComponent::getExpandButtonBounds`) precisely because the right-click "Expand" item alone
left nothing on the card that *looked* clickable. An EXPANDED macro had the same gap on the other side
of the toggle: its only routes back to collapsed were the right-click menu and an undocumented
double-click on the name chip (`GraphEditor::mouseDoubleClick`'s `macroChipAt` branch, which renames
rather than collapses), and the chip itself, while mouse-interactive, reads as a drag handle.

It is painted as a filled `juce::Path` triangle, not a text glyph, for the same reason the card's own
chevron is — `check-nonascii-literals.test.sh` rejects a chevron character outright — pointing UP,
the opposite direction from the card's expand chevron, so the pair reads as opposite ends of one
toggle. `macroCollapseButtonAt()` is the matching hit test, called from `GraphEditor::mouseDown`
**before** the chip check: the two rectangles never actually overlap (the chip is a short label plus a
fixed pad, comfortably clear of the hull's right edge for any real macro), but the button is still
checked first and returns immediately so it can never be shadowed by the chip's own drag gesture even
under a future geometry change. Both are empty, like `macroChipBounds()`, whenever
`macroHullBounds()` is — collapsed, unknown or unresolvable — so the button never appears on a
collapsed card, which has its own chevron, or for a macro with nothing to draw.

**The click goes through `setMacroCollapsed(macroId, true)`** — the exact same call
`buildMacroMenu`'s "Collapse" item and the collapsed card's own chevron both use, so there is only
ever one code path that can flip a macro's collapsed state, and it is one
`recordGraphAndMacroChange` undo step regardless of which affordance triggered it. The click does not
touch selection, matching the card's own chevron handler, so clicking it while other things are
selected leaves that selection alone.

## Testing a context menu headlessly

Every one of these gestures is tested through the real `mouseDown` entry point rather than by calling
the menu builder cold, because testing the layer beneath `mouseDown()` cannot catch a broken hit test
— and a direct-call test can hide a feature that is unreachable in the live app, which is exactly how
"Add Selection to Macro" shipped unreachable.

Driving a real right-click into `ModuleComponent::mouseDown()` calls
`juce::PopupMenu::showMenuAsync()`, which opens a genuine popup and, on a headless Linux CI runner,
segfaults inside `juce::PopupMenu::HelperClasses::MenuWindow::getParentArea` — surviving on macOS and
Windows is what makes this pass locally and fail only in CI. Both `ModuleComponent` and
`MacroCardComponent` therefore carry the same seam `TimelineTrackHeaderComponent` already uses for
its own non-headless picker: a `showContextMenuHook_` member, defaulting to the real
`showMenuAsync()` call, that the right-button branch calls instead of showing the menu directly, plus
a public `setShowContextMenuHookForTest()` a test uses to capture the menu the real gesture built
without ever opening it. That is strictly stronger than asserting against a second, separately-built
menu: the test inspects the exact menu object the click itself produced. A test also guards the wiring
itself, so a future revert to a direct `showMenuAsync()` call fails there first rather than only as an
unexplained Linux-only segfault.

## Related

- [`docs/macros/macros.md`](macros.md) — the macro model.
- [`docs/macros/ports.md`](ports.md) — the port model and the hull's own bounds rules.
- [`docs/macros/auto-ports.md`](auto-ports.md) — the crossing-plan machinery these paths reuse.
- [`docs/macros/configure-io.md`](configure-io.md) — the Configure I/O dialog this menu opens.
- [`docs/layout/selection.md`](../layout/selection.md) — selection and group drag.
- [`docs/shortcuts.md`](../shortcuts.md) — `Cmd+G` and the modifier conventions.
