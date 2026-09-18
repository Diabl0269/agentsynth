# Macros: AI Authorability & Implementation

The AI-authorability decision for Macro I/O, the P8-15 implementation tracker (§7 — the accurate
record of what actually exists as founder-review fixes land on top), and items explicitly out of
scope. The P8-12 presentation-only container and the decided I/O model overview are in
[`macros.md`](macros.md) (§1-4); port mechanics (node types, ordering, poly/stereo, cable
rendering, bypass/mute, menu entry points) are in [`macros_ports.md`](macros_ports.md) (§5).

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
`AIStateMapper/AIStateMapperInternal.h` is an explicit name set with a reason recorded against each entry, consulted
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
   `estimateModuleSize` in `GraphEditorDragDrop.cpp` has an entry for all four types, sized to
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

   **Founder-review fix G6: the module-count indicator counts modules, not port nodes.** A
   second founder pass grouped a Delay and a Reverb with a crossing cable on each side — two
   auto-created ports (§7 item 7) — and the collapsed card's "N modules" line read 4
   (`Macro::members.size()`, which correctly includes the two port nodes — that is what the
   invariant in item 1/§5.1 requires) instead of 2. A port is a real, load-bearing member, but it
   is a boundary jack the macro exposes, not a module the user put in the box; the two are
   different quantities, and every user-facing count/list must report the module one.
   `synth::Macro::memberIsPort()`/`moduleMemberCount()` (`MacroSet.h`) are the ONE place that
   exclusion now lives, and every presentation call site routes through them rather than
   re-deriving the filter: `MacroCardComponent::getModuleCountText()` (the card's count line —
   "N modules" with no ports configured, unchanged from before this fix; "N modules, M ports"
   otherwise, naming the port count rather than silently dropping it from view) and
   `GraphEditor::macroMemberNames()`/`macroMemberPreviews()` (feeding the tooltip's member-name
   list and the card's content-preview glyphs respectively — a port no longer appears in either).
   `Macro::members` itself, and the `MacroPort`/node it fronts, are completely untouched — bounds,
   group drag, bypass/mute fan-out, undo and serialization all keep reading `members` exactly as
   before; a test in `Tests/MacroAutoPortTests.cpp` pins that a port's uuid is still a member
   after this fix, guarding against a future "fix" that tries to make the count line up by
   removing ports from membership instead. An all-ports macro (no ordinary module members at
   all — an edge case `MacroPortWidgetTests.cpp` already exercises) reads "0 modules, N ports"
   and draws no preview glyphs; that is the honest answer, not a bug.

   This is presentation only, and does not change the collapsed card's fixed layout: the count
   line still lives in the same bottom-14px row (`kMacroCardHeight`'s reservation) it always did.
   For the port counts grouping normally produces (one crossing group per external connection,
   typically 1-3 ports per side — the founder's own scenario is one of each), the longer string
   stays well clear of the jack band above it (`kMacroCardJackBandTop`/`Bottom`) and of both
   port-name label columns beside it. A macro with enough ports on one side to push its
   bottom-most jack label down near the count row was already at the limit of that pre-existing
   layout budget before this fix (`kMacroCardJackBandBottom` sits only 4px above the count row's
   own top) — this fix does not create that crowding, it only means the text sharing that row
   with it is occasionally a few characters longer.
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

   **DONE (T152, founder review round 3, items 3.3/3.4): drag-to-reorder + per-port colour.**
   Every row now also has a small drag handle (left of the name field) that reorders it within its
   own direction group by an arbitrary number of slots in one gesture
   (`GraphEditor::reorderMacroPortToIndex`, backing `MacroPortConfigDialog::onReorderPortTo`) — the
   Up/Down glyph buttons from F1 above stayed in place at the time as the keyboard-accessible
   fallback (T153 depended on them staying reachable); **round 4 below removes them** once drag was
   confirmed working, replacing them with a Cmd+Up/Cmd+Down chord. `onReorderPortTo`'s index
   is scoped to the dragged row's own direction (there is no `isInput` argument it could carry a
   cross-direction move through at all), which is what makes "can't drag an input into the output
   section" structural rather than a value `reorderMacroPortToIndex` has to reject. Unlike
   `moveMacroPortOrder`'s adjacent swap, an arbitrary-distance move renumbers the whole group's
   `order` fields sequentially afterward — the only way to guarantee a gap-free order after landing
   anywhere in the list, not just one step over.

   Each row's kind-tinted left-edge bar is now also a real clickable swatch
   (`synth::ui::ColourPickerPopup`, the same favourites-shelf picker the timeline track header and
   Appearance tab's note swatches already use) that sets `MacroPort::colour` — left-click opens the
   picker (commits once, when it closes, not per drag tick — a live preview only repaints the
   swatch), right-click resets to the kind-tint default (`onChangePortColour(nodeUuid,
   std::nullopt)`). The colour flows through to `GraphEditor::macroCardPortLayout`'s
   `MacroCardPort::colour` too, so a coloured port's jack dot on the *collapsed* card matches the
   colour picked in the modal — unset still falls back to the same kind tint
   (`MacroCardComponent::paint`) it always used — and, since T162, the expanded docked widget
    (`ModuleComponent::paintMacroPortWidget`, via `ModuleComponent::resolveMacroPortJackColour`) now
    reads it there too, so an expanded widget and its own collapsed card read a coloured port's jack
    identically (before T162 the widget still drew the kind tint for a coloured port — the mismatch
     this task closes). See §5.2 for the `colour` field's persistence.

   **DONE (T153, founder review round 3, item 3 second half): keyboard accessibility.** Every
   real control in the Configure I/O modal already gets Tab/Return/Space for free from
   `juce::Button`/`juce::ComboBox`/`juce::TextEditor`'s own defaults, so the fixes needed were
   narrower than a full rewrite:
   - **Return commits** — already true for a row's rename/voices fields (F1 above); the "Add a
     port" name AND voices fields now do the same (`onReturnKey` triggers Add, matching a click).
   - **Escape closes the dialog** — wired as `MacroPortConfigDialog::keyPressed` (the bubble-up
     path, for whichever control doesn't itself consume the key) PLUS an explicit `onEscapeKey` on
     every `juce::TextEditor` in the dialog (a `TextEditor` consumes Escape itself before it would
     ever bubble — `TextEditorKeyMapper`/`consumeEscAndReturnKeys` — so relying on the bubble alone
     would silently do nothing while a name/voices field has focus). Escape follows the exact same
     path as clicking Close (`onRequestClose`) — including committing whatever rename/shape edit
     currently has focus (see the T150 note directly below for how that commit actually happens);
     Escape does not invent a separate "discard" semantic.
     `options.escapeKeyTriggersCloseButton = false` at both `promptConfigureMacroIO` and
     `showMacroAutoPortModal`'s launch sites makes the dialog's own override the ONE Escape route —
     `juce::DialogWindow`'s default (a `Button` shortcut on the native close button, a *different*
     dispatch path than the `keyPressed` bubble) would otherwise race it.
   - **Arrow-Up/Down moves focus between rows** — wired on the row's colour swatch and Delete
     glyph button only (`GlyphButton`/`PortColourSwatch::onVerticalArrow` ->
     `MacroPortConfigDialog::moveRowFocus`), never on the shape combo box or a text field, since
     those already have their own meaning for up/down (change the selected item; move the text
     cursor) that this must not shadow. Moves to the SAME control on the row immediately
     above/below (no wraparound past either end); crosses the input/output boundary freely — this
     is plain focus navigation, not a reorder.
   - **FIXED (T150, founder bug report): a rename typed right before Close didn't apply.**
     `nameEditor.onFocusLost` was the only path a rename reached `onRenamePort` through, and real
     `juce::TextEditor::focusLost()` posts an async command message rather than calling
     `onFocusLost` synchronously — Close's own `mouseDown` already grabs keyboard focus away from
     the name editor (queuing that async commit) before the old code fired `onRequestClose`
     directly and tore the dialog down, so the queued commit either never ran or ran too late,
     after the dialog already looked closed. **Fix:** every close path (Close button, all four
     `onEscapeKey` sites, the `keyPressed` Escape bubble) now funnels through one new
     `MacroPortConfigDialog::requestClose()`, which synchronously calls each row's
     `maybeCommitName()` before firing `onRequestClose` — no more racing an async message.
     `maybeCommitName()` mirrors `maybeCommitShape()`'s existing no-op-on-unchanged-text guard
     (`committedName_`), so calling it unconditionally on every row on every close is safe.
     Deliberately **not** a matching unconditional `maybeCommitShape()` sweep: `shapeBox` has no
     combo item of its own for `StereoCollapsed` (`comboIndexFromShape` maps it to the same id as
     `Stereo`, §5.3 above), so re-deriving "the current shape" from the combo at close time would
     silently reclassify every untouched `StereoCollapsed` port as `Stereo` — a real
     `changeMacroPortShape` delete+recreate — just from opening and closing the dialog. Voices only
     matters while a row shows Poly (never the lossy `StereoCollapsed` case), so `requestClose()`
     calls a narrower `maybeCommitVoicesOnClose()` (commits only when `voicesEditor.isVisible()`)
     instead of the full shape re-derivation.
   - **The known related gap: Esc on the macro auto-port boundary modal
     (`MacroAutoPortPromptDialog`) was a silent no-op** — `DialogWindow`'s default Escape handling
     (`setVisible(false)`) closed the window without ever calling `onChoice`, so
     `GraphEditor::requestGroupSelectionIntoMacro`'s `respond` callback never ran: no macro got
     created and no status message explained why (the caller had no way to know the modal was
     dismissed rather than answered). **Decision:** Escape now behaves exactly like clicking
     "Leave Cables As Is" (`onChoice(false, /*remember=*/false)`) — the least surprising reading of
     "close/cancel without creating a port, same as clicking away": the user already asked to
     group these modules (Cmd+G or the menu item got them here), so Escape aborting the *whole*
     grouping would be the surprising outcome, not leaving the boundary cables as-is. `remember` is
     always forced `false` on the Escape path regardless of the toggle's own state (defaults ON),
     so a reflexive Escape can never silently pin a permanent auto-port preference the way a
     deliberate button click legitimately can.
   - **Same sweep, other modals:** `ExportAudioDialog` had the identical `TextEditor`-swallows-
     Escape gap on its file-name field, PLUS a page-aware wrinkle the macro dialogs don't have —
     Escape while a bounce is actually rendering must fire `onCancelRender`, never
     `onRequestClose` (which would just hide the dialog while `BounceRunner` kept rendering
     unseen in the background); `ExportAudioDialog::keyPressed`/`handleEscapeRequested` route to
     whichever the current page's Cancel/Close button already does, and `MainComponent::
     promptExportAudio` sets the same `escapeKeyTriggersCloseButton = false`. `SignInDialog` was
     audited and needed no change — it has no `TextEditor`, and its destructor already calls
     `AccountService::cancelSignIn()` unconditionally to cover exactly this "closed by some path
     that skips my own Cancel button" case.

   **DONE (founder review round 4 — real-build testing of T152/T153): three fixes.**
   1. **Up/Down glyph buttons removed.** Now that drag-to-reorder was confirmed working live, the
      per-row Up/Down buttons were pure clutter. Keyboard-accessible reordering survives as a
      **Cmd+Up/Cmd+Down chord** on a row's remaining controls (colour swatch, Delete button) —
      `GlyphButton`/`PortColourSwatch::keyPressed` check the command modifier FIRST and fire
      `onReorderPort` (the same callback the buttons used), falling through to the pre-existing
      bare-arrow row-navigation check only when it isn't held. This matches the app's existing
      convention of the command modifier for editing-type actions (Cmd+D duplicate, Cmd+R repeat —
      docs/control/shortcuts.md) and is dialog-local key handling, not a `ShortcutManager` action.
   2. **Keyboard focus is now visible.** Diagnosed by reading `juce::Button::paint()`
      (`juce_Button.cpp`): it hands `paintButton()` only `isOver()`/`isDown()`, never keyboard-focus
      state, so `GlyphButton`/`PortColourSwatch` — both custom `paintButton` overrides — never drew
      anything different when Tab'd to. Both now check `hasKeyboardFocus(true)` and draw an accent
      outline, reusing `AppLookAndFeel::drawTextEditorOutline`/`drawComboBox`'s own "accent when
      focused, same border weight" convention rather than inventing a new style.
      `AppLookAndFeel::drawButtonBackground` got the identical fix, so every plain `juce::TextButton`
      app-wide (including this dialog's Add/Close buttons) now shows focus too —
      `LookAndFeel_V4`'s default drew none. Every repaint is the free one JUCE's own
      `Button::focusGained`/`focusLost` already trigger; no new timer (Source/UI/CLAUDE.md).
   3. **The "Add a port" panel's Add button/name field were invisible.** Diagnosed with a headless
      `createComponentSnapshot` PNG (docs/development/test-patterns.md's smoke-test pattern) before any fix was
      attempted: the panel's second content row (name field + Add button) was entirely absent from
      the render, while the first row (direction/kind/shape combos) painted fine. Root cause:
      `resized()`'s `addBlockArea` budgeted height for only 2 of the panel's 3 rows (label +
      newRow1), so newRow2 was squeezed to zero height — a layout bug, not colour/contrast. Fixed
      by computing the panel's height from all 3 rows in one `kAddBlockHeight` constant, used by
      both `resized()` and `idealDialogHeight()` so they can never drift apart again (the same
      "one function, never two" reasoning `layOutOrMeasureRows` already documents for the row
      list).
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
   been constructed) only when remember is checked, and applies once otherwise. The write alone is
   useless without a matching read: the tri-state is restored at launch by
   `PreferencesSettingsTab::loadMacroAutoPortPreference()` called from `MainComponent`'s
   constructor — the same startup-restore pass as `loadDualIOPerModuleOverrides()` and the two T148
   on/off automations above it — so a "Remember my choice" survives a relaunch instead of the
   modal re-asking every session (the pre-fix bug: the write path and
   `PreferencesSettingsTab::setGraphEditor()` only ever pushed the value once Settings opened, so
   a fresh launch left the editor `Unset`) (T147). Follows the
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

     **T144 (founder review round 3): the knob went dead when the chain crossed TWO macro
     boundaries, not one.** Grouping the source side into its own macro and the destination side
     into a SEPARATE macro leaves the attenuverter sandwiched between an outlet port on one macro
     and an inlet port on another — each splice above runs independently and correctly (the graph
     is genuinely `port -> atten -> port`, and `buildVisibleCables()`'s pass 1 draws it as one
     `AttenuverterChain` cable with the knob at its midpoint exactly as designed). The reported bug
     was never a gap in this design — the founder's three framings ("on the port widget", "in the
     mod matrix only", "a synthetic inline control") all presupposed the on-canvas cable-midpoint
     knob couldn't represent a two-macro crossing at all. It can, and does, once painted; what
     could not be clicked was `GraphEditor::getAttenuverterNodeAt`, the hit-test both the drag-to-
     adjust gesture and the double-click-to-delete gesture share. It re-derived the chain's two real
     endpoints straight from `graph.getConnections()` and `ModuleComponent::getPortCenter()` with
     the RAW channel index (never `mapOutputChannel`/`mapInputChannel`'s visible-jack mapping) and
     no collapsed-macro re-anchoring — a second, independent geometry computation from the one
     `buildVisibleCables()`/`paint()` actually use, in violation of the class comment above
     `GraphEditor::CableId` that exists specifically to rule this out. Once either endpoint became a
     macro port node the two computations diverged by roughly 300px, in EVERY collapse state
     including both macros fully expanded (measured directly against `buildVisibleCables()`'s own
     painted midpoint before this fix) — so the knob rendered in the right place and clicking it
     did nothing, matching "no amount knob and does not work at all" exactly.
     `getAttenuverterNodeAt` now searches `buildVisibleCables()` for the `AttenuverterChain` cable
     and hit-tests its own `(p1+p2)/2` midpoint, so it can never disagree with what is painted.
     `MacroAutoPortTests.cpp`'s `TwoMacroCrossingKnobHitTestMatchesPaintedGeometryInEveryCollapseState`
     pins the geometry across all three collapse states, and
     `TwoMacroCrossingKnobRespondsToARealMouseDrag` drives a real `mouseDown`/`mouseDrag`/`mouseUp`
     and a real `mouseDoubleClick` into `GraphEditor` itself to prove both gestures work end to end,
     not just that the NodeID lookup succeeds.

     **The mod matrix's own destination combo needed a matching fix.** `MacroInletModule`
     deliberately declares no `getModulationTargets()` (`GraphEditor::connectPorts()` relies on that
     empty list to keep a plain cable drop onto a Macro In's jack a plain connection, never
     auto-wrapped in a fresh attenuverter — `Tests/Macros/MacroPortFlow/MacroPortFlowCableDropTests.cpp`'s drop-a-cable tests pin
     that), so a `ModMatrixComponent::ModRow` whose destination is now a spliced port had nothing to
     resolve its combo selection against and would render blank. Rather than give
     `MacroInletModule` a real `ModulationTarget` (which would resurrect the auto-wrap problem for
     every ordinary cable drop, since `connectPorts()` reads that same list), `ModMatrixComponent`'s
     own combo-population (`destinationCandidatesForCombo`, `Source/UI/Graph/ModMatrixComponent.cpp`)
     substitutes a display-only synthetic target at channel 0 whenever the module has none AND is a
     `MacroInletModule` — a spliced port from this fix is always Mono with its one active channel at
     0 (the internal jack an `AttenuverterChain` lands on is never poly-fanned), so channel 0 is the
     only candidate. `Tests/UI/Graph/ModMatrixTests.cpp`'s
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

   **Second caller: "Make channel" (P9-3d/FRO25, [`mixer.md`](mixer.md) §5.8).**
   `GraphEditor::makeChannelFromNode` boxes each new channel with the same group-time pass
   (`buildMacroPortCrossingPlan` before `macros.add`, then `spliceMacroPorts`), always creating ports
   regardless of `MacroAutoPortPreference` — a shared module reaching into the channel is exactly
   what the port is for — with ONE filter: a group whose internal node is the new Channel Strip and
   whose direction is outward is dropped, so Strip -> Master (and Strip -> a merge point's bus) stays
   a plain edge that `spliceMasterNode`'s Mix/Direct classification can see. "Duplicate into Channel"
   joins its copy through `addSelectionToMacro`'s T138 incremental passes. The plan builders
   themselves are unchanged.

8. **DONE (founder-review fix G7): Ungroup removes the macro's ports, and a port node is
   directly deletable.** The item above shipped with an explicit open question for the founder —
   "what should Ungroup do with an auto-created port?" — recorded as **nothing**: the port's
   fronting node would survive ungrouping as an ordinary member, connections intact, becoming a
   stray "Macro In"/"Macro Out" box sitting mid-signal-chain with no delete affordance of its own
   (Configure I/O — the one surface that could remove it — disappears the instant the macro does,
   since `promptConfigureMacroIO`'s first line is `macros.find(macroId)`). A second founder review
   pass, hands-on with the shipped feature, answered it: *"ungroup leaves the macro input/output in
   place (They should be removed) and they cannot be removed."*

   **The decided rule.** Ungrouping a macro removes every one of its port nodes and splices the
   ORIGINAL CABLE BACK — external reconnects straight to internal, exactly as it was before
   grouping — as part of the SAME undo step that removes the `Macro` record. Group then Ungroup is
   now a true round trip, which is the property this fix is mostly for; §5.4's old "ungrouping does
   not drop a cable landing on one of its ports" passage above described the pre-G7 behaviour and
   has been rewritten to match. This applies to EVERY port, auto-created (item 7) and hand-added
   (item 3) alike — **no provenance field was added**, resolving concern (1) the open question
   raised: a port exists only to proxy a boundary, so once the boundary (the macro) is gone, a
   hand-named port is the same kind of orphan as an auto-created one, with no reason to treat them
   differently. Concern (2) — "it changes what Ungroup means for every macro, not just ones this
   feature touched" — is exactly the change the founder asked for.

   **The splice-out algorithm (`GraphEditor::spliceOutMacroPort`), the reverse of `spliceMacroPorts`
   (item 7).** For the port node fronted by one `MacroPort`: read every connection currently
   touching it, split into the set LANDING on it (the port's own channel is the destination — an
   "in" edge) and the set LEAVING it (the port's own channel is the source — an "out" edge), then
   for every port channel shared between an in-edge and an out-edge, connect that in-edge's OTHER
   endpoint directly to that out-edge's OTHER endpoint, on the exact raw channels each already
   carried — the full CROSS PRODUCT, so fan-in (two external sources sharing one inlet port, item
   7's own "share ONE port, not N" dedup rule) and fan-out reconnect completely, not just the first
   pair. A port wired on only one side (or neither) contributes no pairs and simply disappears —
   nothing to reconnect. Removing the node then drops its own now-superseded connections for free
   (`juce::AudioProcessorGraph::removeNode` calls `disconnectNode` before erasing).

   This is signal-preserving because `MacroInlet`/`MacroOutlet` and both MIDI variants are
   VERIFIED pure per-channel pass-throughs, not assumed to be: `mapInputChannel(c)` and
   `mapOutputChannel(c)` return the identical `LogicalPort` on both (§5.1/§5.3 — Mono, Stereo's
   split-block right leg, `StereoCollapsed`'s two-raw-channel single jack, and Poly-N's fanned bus
   all checked), so whatever enters the port on raw channel `c` is exactly what leaves it on raw
   channel `c`, on every shape a port can carry. A MIDI port's single "channel" is always
   `juce::AudioProcessorGraph::midiChannelIndex` on both sides, so the same cross-product logic
   applies unchanged with no MIDI-specific branch needed.

   **Ungroup reaches the graph-change notification path.** Before this fix, `ungroupSelection()`
   was metadata-only (`MacroSet::remove` alone) and never needed to call
   `GraphEditor::updateComponents()`'s post-mutation hooks for correctness. It now removes nodes
   and rewires connections — a real graph edit — so it calls `updateComponents()` inside the SAME
   `recordGraphAndMacroChange` transaction that does the splicing, exactly like `deleteSelection()`
   already does; that in turn fires `onGraphStructureChanged` ->
   `MainComponent::reconcileTimelineBindingsOnly()`, the seam that keeps a timeline binding from
   surviving stale, keyed to a now-deleted node's uuid, into the next audio-thread render pass
   (root `CLAUDE.md`, `docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step`). `MacroAutoPortTests.cpp`'s
   `ReachesTheGraphStructureChangedNotificationHook` pins this directly.

   **A port node is now directly deletable, right-click.** Before this fix,
   `ModuleComponent::mouseDown()` refused ANY click on a macro-port node's body (mirroring the
   Attenuverter's "no header, nothing to click" early return) — so a port had exactly one delete
   path, Configure I/O, which vanished with its macro. The early return now excludes the RIGHT
   button only: a port node's body right-click opens a NEW small menu
   (`ModuleComponent::buildMacroPortContextMenu`) — "Delete Port" always, plus "Rename Port..." (a
   lightweight `AlertWindow`, `GraphEditor::promptRenameMacroPort` — the quicker alternative to
   opening the whole Configure I/O modal to retype one name) and "Configure I/O..." when the port
   still resolves to a live macro. LEFT-click stays exactly as restrictive as before — no
   selection, no drag — because `dockMacroPortWidgets()` repositions this widget every layout pass
   regardless; a draggable port would just snap back the moment the user let go, a new broken-
   feeling gesture inside a fix meant to remove them. Deleting a port this way, while its macro
   stays alive, shares `spliceOutMacroPort` with ungroup
   (`GraphEditor::deleteMacroPortNode`) — the cable is spliced back, never dropped, and the macro
   dissolves outright if this was its last member (mirroring `MacroSet::removeMemberEverywhere`'s
   own "zero members is not a meaningful state" rule).

   **`GraphEditor::removeMacroPort()` (item 2's Configure I/O "delete this port" action) is
   deliberately UNCHANGED** — it still reuses the ordinary multi-select delete path and drops the
   cable rather than splicing it, pinned by
   `Tests/Macros/MacroPortFlow/MacroPortFlowEditTests.cpp`'s `RemoveDeletesTheNodeAndDropsThePort`. An explicit "delete this
   port" from a configuration dialog arguably SHOULD drop the cable rather than silently rewire the
   patch around it; `spliceOutMacroPort` is shared code, not shared semantics, and switching
   Configure I/O's own delete to splice-back too is a proposal for a future pass, not decided here.

9. **DONE (T148, founder-review item P8-29).** Auto-create a macro port on a boundary-crossing
   cable-DRAG (the counterpart to item 7's grouping-time splice), and auto-delete a port once its
   last cable is removed. Both are togglable in Preferences, independent of each other and of the
   tri-state `macroAutoPortPreference_` above — see the end of this item for why they are a plain
   on/off, not a third tri-state.

   **Auto-create (`GraphEditor::maybeAutoCreateMacroPortsForDrag`, called from
   `endConnectionDrag`).** The module-jack hit-test loop that resolves a completed drag to two real
   jacks (also covers the Serum-style knob-drop fallback — same call site) now runs this check
   before falling back to a plain `connectPorts()`. The **endpoint-needs-a-port rule**, applied
   independently to each of the drag's two resolved endpoints: an endpoint needs a NEW port on its
   own macro iff (a) it resolves to an ORDINARY member of that macro (`memberIsPort()` false — a
   drag landing exactly on an EXISTING port's own jack never mints a second one), and (b) the OTHER
   endpoint is not a member of that same macro at all, ordinary member or port alike — "not a
   member" is one `Macro::hasMember()` check, since a port's own uuid is always also a `members`
   entry (§5.1's invariant). Two members of the SAME macro dragged together therefore wire straight
   through, exactly as before this feature; two members of DIFFERENT macros each mint their own
   port (an outlet on the source's macro, an inlet on the destination's), wired port-to-port for the
   boundary leg. When only one endpoint needs a port, the OTHER leg of the final connection lands
   directly on the plain module (or the existing port, if that's what it resolved to) — never a
   second port. **Mono only** — the same scope cut `createMacroPortFromDroppedCable` (§5.3, item 3)
   already applies to its own cable-drop convenience; no inference from the dragged cable's poly/
   stereo fan. **A mod/CV-routed drag is handled by the SAME mint-and-wire path as a plain audio
   drag (T155, 2026-09-05)** — no separate scope cut, no mirrored CV-detection helper. The v1 cut
   (`connectionWouldRouteAsModulationCV`, which bailed out before minting anything) was live-tested
   and found unnecessarily narrow, so it was deleted outright. `connectPorts()`'s own existing
   CV/mod-routing detection (`resolvePolyLink` -> single-voice -> destination's
   `getModulationTargets()`, unchanged by T155) naturally wraps whichever leg's real endpoint is a
   genuine modulation-target parameter (Pitch, Cutoff, a knob-drop target, ...) in a hidden
   `AttenuverterModule` via `addModRouting()` — exactly as it already does for a plain, non-macro CV
   connection. A freshly-minted `MacroInletModule`/`MacroOutletModule` is never itself a modulation
   target (an existing invariant — the attenuverter node itself can also never be a macro member,
   §2/§7 item 7), so wiring `member<->port` can never accidentally attract the wrap; only the LAST
   leg wired — the one whose destination is the real modulation target — is ever a candidate, same
   as it always was. This exactly mirrors item 7's own G3 fix for the grouping-time path
   (`spliceMacroPorts`), which was never in scope-cut in the first place. **Wiring wires BOTH sides
   of every minted port** — unlike
   `createMacroPortFromDroppedCable`, which deliberately wires only the external side because a
   collapsed macro has no visible interior to wire to (§5.3); here the macro is necessarily
   EXPANDED (both endpoints are real, visible `ModuleComponent`s, which a collapsed macro's hidden
   members are not), so the interior leg is wired too. The whole mint-and-wire sequence — up to two
   new port nodes, up to three `connectPorts(..., recordUndo=false)` calls, one `updateComponents()`
   — runs inside ONE `recordGraphAndMacroChange` transaction, so a single Cmd+Z undoes all of it
   together, the same pattern `createMacroPortFromDroppedCable`/`spliceMacroPorts` already use.

   **`maybeAutoCreateMacroPortsForDrag` gained a trailing `recordUndo = true` parameter (T184,
   docs/mixer.md §5.2 / docs/mixer_implementation.md item 2).** A caller that is already inside its own undo transaction — T184's
   auto-channel hook in `endConnectionDrag`, which must cover macro-port creation, the connection,
   AND a possible new channel as ONE step — passes `recordUndo=false` to fold this function's own
   mutation into that outer transaction instead of opening a nested one, and takes over calling
   `updateComponents()` itself once, after every mutation, rather than getting one call per nested
   transaction. Every pre-existing call site is unaffected (the default keeps today's own
   `recordGraphAndMacroChange`-and-`updateComponents()` behaviour byte-identical).

   **Auto-delete (`GraphEditor::autoDeleteOrphanedMacroPort`, called from `disconnectCable` and
   `disconnectPort`).** When a mutation drops a connection touching a macro port node down to ZERO
   remaining connections, the port is spliced out (`spliceOutMacroPort`, the same helper
   `ungroupSelection()`/`deleteMacroPortNode()` already share) and the macro dissolved too if that
   was its last member — mirroring `MacroSet::removeMemberEverywhere`'s own "zero members is not a
   meaningful state" rule. A port with one of its two legs still wired (or one of several fan-in/
   fan-out connections still wired) survives; only reaching zero triggers the splice. **Hooked at
   exactly two explicit user-gesture call sites**, each checked BEFORE mutating so the right undo
   transaction is chosen up front:
     - `GraphEditor::disconnectCable` — upgraded from its existing graph-only
       `recordStructuralChange` to `recordGraphAndMacroChange` ONLY when at least one of the cable's
       two logical endpoints (`cable.id.srcUid`/`dstUid` — the REAL endpoints even for an
       `AttenuverterChain` cable, never the hidden attenuverter itself) resolves to a macro port; an
       ordinary cable's removal is byte-identical to before this feature.
     - `GraphEditor::disconnectPort` — same treatment, upgraded only when the clicked jack's own
       node or the far end of any connection about to be removed resolves to a macro port.
   **Also hooked (T154): `deleteSelection`/`deleteModule`/`requestDeleteModule`.** Deleting an
   ORDINARY member can strand a *different* port cableless (the member was that port's only
   remaining connection), which the original two call sites above can't catch since neither the
   port nor its cable is directly involved in the gesture. `GraphEditor::macroPortDeletionNeighbors`
   is the batch-deletion counterpart: called BEFORE any node in the batch is removed from the graph,
   it walks the live connection list and returns every node OUTSIDE the deletion set that has a
   direct connection to a node INSIDE it. `deleteSelection`/`deleteModule`/`requestDeleteModule`
   already always use `recordGraphAndMacroChange` (unlike `disconnectCable`/`disconnectPort`, which
   upgrade from `recordStructuralChange` only when a macro port is actually touched), so there is no
   undo-transaction decision to make here — only which nodes to check once the batch removal
   completes. After `removeNode` runs for every id in the batch, `autoDeleteOrphanedMacroPort` runs
   once per captured candidate, exactly the same self-checking primitive `disconnectCable`/
   `disconnectPort` already call: it no-ops for anything that isn't a live macro port, or that still
   has a connection surviving elsewhere. This is deliberately **single-hop**, matching the original
   two call sites' own scope — splicing out a candidate here can itself strand a second port that was
   wired only to the first (two ports can be directly wired port-to-port for a cross-macro-boundary
   crossing, see `maybeAutoCreateMacroPortsForDrag` above), and that second port is not chased;
   nothing in this feature cascades beyond one hop. `modMatrix.clearRows()` needed no
   special interaction handling for any of this — it's `rows.clear(); repaint();`, a blunt UI-state
   clear with no node-scoping of its own, so it stays exactly where it already ran (once, at the top
   of each function's undo-transaction lambda) and tolerates being called ahead of an auto-delete
   sweep with no changes.

   **Both behaviours are Preferences toggles, plain on/off, DEFAULT ON** —
   `GraphEditor::setAutoCreateMacroPortsOnDragEnabled`/`setAutoDeleteMacroPortsOnLastCableEnabled`,
   persisted by `PreferencesSettingsTab` under `"macroAutoCreatePortsOnDrag"` /
   `"macroAutoDeletePortsOnLastCable"` respectively, each independent of the other and of the
   grouping-time `macroAutoPortPreference_` tri-state above. Deliberately NOT a third tri-state:
   `macroAutoPortPreference_` defaults to "ask" because it replaced pre-existing SILENT behaviour
   (a crossing cable at group time used to always wire straight to the interior member with no
   port) and asking once lets a user pick a side knowingly; these two are brand-new automations the
   founder asked to ship as the default behaviour, with a plain escape hatch for the user who wants
   the pre-T148 wire-straight-through / leave-a-cableless-port behaviour instead — there is no
   pre-existing silent default to protect a user's expectation of. Off, `endConnectionDrag`'s
   boundary-crossing branch and `disconnectCable`/`disconnectPort`'s zero-connections check are
   both no-ops, reproducing the exact pre-T148 code paths.

   **Rendering is unaffected — confirmed, not assumed.** Both directions of this feature reuse
   existing port/cable machinery outright: a drag-minted port is the same `MacroInletModule`/
   `MacroOutletModule` node §5.1/§5.3 already describe, laid out by the same
   `dockMacroPortWidgets()`/`macroCardPortLayout()` every other port uses; the cable it's wired with
   goes through the same `connectPorts()`/`buildVisibleCables()` path as any other connection. A
   splice-out on auto-delete is the identical `spliceOutMacroPort()` call `deleteMacroPortNode()`
   already made, so the resulting graph and macro state (and therefore every render/anchor rule in
   §5.4) is indistinguishable from a port deleted that way today. Nothing in §5.4's table changes.

   **§5.3 update:** Mono-via-drag is now a SECOND route to an auto-created port alongside the
   group-time splice (item 7) — both are Mono-only, both read the drag/crossing's direction to
   decide inlet vs outlet, neither infers Stereo/Poly-N. Poly-N and Stereo remain reachable only
   from the Configure I/O modal (or, for `StereoCollapsed`, only from the grouping-time merge pass,
   §5.3's own bullet) — this item does not change that.

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

- `docs/layout/selection.md` · `docs/layout/cables.md` · `docs/layout/macro-cards.md` — selection,
  group drag, cable interaction, and the macro container's canvas behaviour
- `docs/architecture/architecture.md` — the flat graph, latency compensation, plugin state format
- [`docs/modules/modules.md`](modules/modules.md) / `Source/Modules/CLAUDE.md` — channel-count rules; [`docs/modules/fx-modules.md#stereo-io-dual-io-toggle`](modules/fx-modules.md#stereo-io-dual-io-toggle) — Dual I/O rules
- [`docs/modules/modulation.md`](modules/modulation.md) — logical-port API, poly-bus wires
- `docs/ai/patch-safety.md` — `validatePatch`, the untrusted path
- `docs/ai/patch-format.md` — reserved patch-format keys
