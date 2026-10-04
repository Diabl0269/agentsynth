// ShortcutManagerDefaults.cpp
//
// The factory default key for every action, one function per category. resetToDefaults()
// (ShortcutManager.h) clears the map and runs all four in category order.

#include "ShortcutManager.h"

void ShortcutManager::addGeneralDefaultBindings() {
    const bool isMac = defaultsPlatform == DefaultsPlatform::Mac;
    // ---- General ----
    bindings["openSettings"] = juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0);
    bindings["savePreset"] = juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0);
    // Cmd+Shift+S is the platform-standard Save As, so it belongs to saveProjectAs; saveSnippet
    // moved to Cmd+Opt+S (see addGraphDefaultBindings). Two command actions can never share a
    // chord, since MainComponent::keyPressed dispatches the FIRST bound action that has a
    // command, and the loser would be permanently dead. The only other 's' bindings are
    // savePreset (Cmd+S), saveSnippet (Cmd+Opt+S) and pianoRollToggleScaleFilter (bare Alt+S).
    bindings["saveProjectAs"] =
        juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    // Cmd+Shift+E, the same chord Logic/Ableton use for bounce/export. Free on both counts: no
    // other binding in this table uses 'e' with any modifier set, and no keyPressed() override
    // hardcodes a bare 'e' either.
    bindings["exportAudio"] =
        juce::KeyPress('e', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    // Cmd+Shift+P: 'p' appears nowhere else in this table under any modifier set -- the bare
    // 'p' is the timeline's loop-selection surface key, which carries no modifiers and is matched
    // with exact modifier equality, so it can never steal the chord, and no component keyPressed()
    // override hardcodes Cmd+Shift+P either. It forms a Cmd+Shift "export" pair with Export Audio
    // (Cmd+Shift+E); 'P' reads as "Patch".
    bindings["exportPatchOnly"] =
        juce::KeyPress('p', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    bindings["openProject"] = juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0);
    bindings["newPatch"] = juce::KeyPress('n', juce::ModifierKeys::commandModifier, 0);
    bindings["undo"] = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0);
    bindings["redo"] = juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    // Cmd+M on the Mac. Off the Mac Cmd IS Ctrl, and Ctrl+M belongs to Toggle Metronome (below), so
    // the matrix takes Ctrl+Alt+M there.
    bindings["toggleModMatrix"] = isMac ? juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0)
                                        : juce::KeyPress('m', otherPlatformModMatrixModifiers(), 0);
    // 'k' with plain Cmd is unused by any other binding (Cmd+, / S / O / N / Z / Shift+Z / M
    // / A / L / B, Shift+A, Shift+S) — safe to claim for the minimap toggle.
    bindings["toggleMinimap"] = juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0);
    // The platform-standard Select All owns the bare Cmd+A chord (every DAW reads it that
    // way), so the AI panel moves off it. On macOS it takes a REAL Ctrl+A — Ctrl is a distinct
    // physical modifier there, so the chord is free. On Windows/Linux JUCE's commandModifier
    // IS the Ctrl key, so Ctrl+A and Cmd+A would be the SAME chord (and would trip
    // EveryDefaultBindingIsUnique in Linux CI); those platforms take Cmd+Shift+A instead. One
    // of the very few per-platform defaults in this table — rebindable like everything else.
    bindings["toggleAiPanel"] =
        isMac ? juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0)
              : juce::KeyPress('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    bindings["toggleLibrary"] = juce::KeyPress('b', juce::ModifierKeys::commandModifier, 0);
    // 't' with plain Cmd is unused by any other binding (Cmd+, / S / O / N / Z / Shift+Z / M /
    // K / A / L / B, Shift+A, Shift+S, C / V / D) — safe to claim for the ONE bottom-dock
    // open/close toggle (this used to be toggleTimelinePanel's own chord before the
    // dock grew a single shared toggle — see migrateBottomPanelToggleKeys() below).
    bindings["toggleBottomPanel"] = juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0);
    // Cmd+1/2/3 -- "show this tab" (never closes the dock), in the dock's DEFAULT tab
    // order (Timeline, Mixer, Controllers); a drag-reorder keeps Cmd+N pointed at whichever tab
    // is now Nth (BottomDockComponent::permuteShortcutKeysForNewOrder). Free chords: no other
    // binding in this table uses a bare Cmd+digit.
    bindings["toggleTimelinePanel"] = juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0);
    bindings["toggleMixerPanel"] = juce::KeyPress('2', juce::ModifierKeys::commandModifier, 0);
    // The platform-standard Select All chord (Cubase, and every text field, read Cmd+A this
    // way); it routes per focused editor. The AI panel moved to Cmd+Shift+A to free it (above).
    bindings["selectAllModules"] = juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0);
    // The platform-standard trio. Safe to claim app-wide because JUCE's TextEditor consumes
    // Cmd+C/Cmd+V itself while it has focus, so these only reach the canvas when no text field
    // is being edited — see MainComponent::keyPressed, which is the sole dispatch point.
    bindings["copySelection"] = juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0);
    bindings["pasteSelection"] = juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0);
    bindings["duplicateSelection"] = juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0);
    // Cmd+X completes that trio — the platform-standard cut, free here on both counts: no other
    // binding in this table claims 'x' with any modifier set, and no component keyPressed()
    // override hardcodes a bare 'x' either (the panel-local letters are Q/L/P, the roll's is Q,
    // and the lane area's is P). Same TextEditor-consumes-it-first safety as Cmd+C/V above.
    bindings["cutSelection"] = juce::KeyPress('x', juce::ModifierKeys::commandModifier, 0);
    // Cmd+R for Repeat, Cubase/Logic's own binding for the same verb. Also free on both
    // counts — nothing in this table uses 'r', and no keyPressed() override matches an 'r'
    // (checked against the panel's Q/L/P, the roll's Q and the lane area's P).
    // Off the Mac Cmd IS Ctrl and Ctrl+R belongs to Record (below), so Repeat takes Ctrl+Shift+R there.
    bindings["repeatSelection"] = isMac ? juce::KeyPress('r', juce::ModifierKeys::commandModifier, 0)
                                        : juce::KeyPress('r', otherPlatformRepeatModifiers(), 0);
    // Bare spacebar, no modifiers — the platform DAW convention for play/stop. Safe to
    // claim app-wide for the same reason Cmd+C/V is: a focused juce::TextEditor consumes the
    // spacebar itself (types a space character) before it ever reaches MainComponent::
    // keyPressed, the sole dispatch point — see docs/control/shortcuts.md.
    bindings["togglePlayback"] = juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers, 0);
    // Cmd+= / Cmd+- : the platform zoom pair (every browser, every editor). Free on both counts
    // — '=' and '-' appear nowhere else in this table, and no component keyPressed() override
    // matches either character. Cmd+Shift is the VERTICAL axis, mirroring the wheel bindings the
    // timeline panel and the piano roll already ship (Cmd+wheel = horizontal, Cmd+Shift+wheel =
    // vertical), so the keyboard and the wheel teach the same modifier.
    //
    // Deliberately Cmd (not the Ctrl the snap block below uses): zoom is General — it routes to
    // whichever surface has focus, including the graph canvas — and a General action should use
    // the platform's own accelerator.
    bindings["zoomInHorizontal"] = juce::KeyPress('=', juce::ModifierKeys::commandModifier, 0);
    bindings["zoomOutHorizontal"] = juce::KeyPress('-', juce::ModifierKeys::commandModifier, 0);
    bindings["zoomInVertical"] =
        juce::KeyPress('=', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    bindings["zoomOutVertical"] =
        juce::KeyPress('-', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);

    // Tab/Shift+Tab cycle keyboard focus between the app's currently OPEN focus regions
    // (Library, Canvas, Timeline, AI Panel, Mod Matrix — see Source/UI/Layout/FocusRegion.h; a closed
    // region is skipped, never opened, by the cycle itself). Bare Tab is free to claim: this
    // codebase has never customized it (no KeyPress::tabKey/FocusTraverser/setExplicitFocusOrder
    // usage anywhere) — the only behaviour is JUCE's own generic
    // ComponentPeer::handleKeyPress fallback (a sibling-order focus jump with no notion of these
    // regions, triggered only when nothing else claims the key), which registering Tab here
    // deliberately supersedes with a well-defined region cycle.
    bindings["focusNextRegion"] = juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
    bindings["focusPrevRegion"] = juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
    // Direct-focus shortcuts OPEN a closed target region before focusing it (unlike Tab-cycling
    // above, which only ever visits what's already open). Cmd+Shift+M is deliberately NOT used
    // for Library: Cmd+M already owns "Toggle Mod Matrix", and 'm' went to "locateMaster"
    // (Graph, below) instead. Free on both
    // counts against the rest of this table: no other binding uses 't' or 'l' with Cmd+Shift
    // (toggleBottomPanel is bare Cmd+T; autoArrange is bare Cmd+L, a different category
    // entirely), and no component keyPressed() override hardcodes either chord.
    bindings["focusTimeline"] =
        juce::KeyPress('t', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    bindings["focusLibrary"] =
        juce::KeyPress('l', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    // Bare Cmd+F — free on both counts (no other binding uses 'f' with Cmd, and no
    // component keyPressed() override hardcodes it; the only existing 'f' binding is bare,
    // unmodified 'f' for timelineFollowPlayheadToggle, a different chord entirely). Cmd+F reads
    // as "find" the way it does in almost every app, which is exactly what this does.
    bindings["focusLibrarySearch"] = juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0);
    // The platform-standard "show the context menu" chord; nothing else binds F10.
    bindings["openContextMenu"] = juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0);

    // The transport family ships UNBOUND (bar Record and Metronome, below) -- an explicit invalid juce::KeyPress(), not
    // an absent map entry. saveToProperties() below indexes `bindings` with `.at()` for every
    // id in `actionIds`, so a genuinely absent entry throws (std::map::at) the moment anyone
    // ever persists; a present-but-invalid KeyPress is indistinguishable from "absent" to
    // every reader (getBinding()'s own not-found fallback is this exact same default-
    // constructed KeyPress) while keeping the map's invariant that every action id has an
    // entry.
    bindings["transportPlay"] = juce::KeyPress();
    bindings["transportStop"] = juce::KeyPress();
    bindings["transportToggleLoop"] = juce::KeyPress();
    // Record and Metronome are the two exceptions: a literal Ctrl+R / Ctrl+M on every platform (Option
    // types characters in text fields on the Mac, and Alt+letter is menu-mnemonic territory on
    // Windows). On the Mac Ctrl is a distinct physical key, so Cmd+R Repeat and Cmd+M Toggle Mod Matrix
    // are untouched. Off the Mac Ctrl is also Cmd, so those two moved (see their bindings above).
    bindings["transportRecord"] = juce::KeyPress('r', juce::ModifierKeys::ctrlModifier, 0);
    bindings["transportToggleMetronome"] = juce::KeyPress('m', juce::ModifierKeys::ctrlModifier, 0);
    bindings["transportReturnToStart"] = juce::KeyPress();
    // Cursor moves and loop jumps -- same unbound-by-default reasoning as above.
    bindings["transportNudgeBackBeat"] = juce::KeyPress();
    bindings["transportNudgeForwardBeat"] = juce::KeyPress();
    bindings["transportNudgeBackBar"] = juce::KeyPress();
    bindings["transportNudgeForwardBar"] = juce::KeyPress();
    bindings["transportJumpToLoopStart"] = juce::KeyPress();
    bindings["transportJumpToLoopEnd"] = juce::KeyPress();
    // Jump to the next/previous timeline marker -- same unbound-by-default reasoning.
    bindings["transportJumpToNextMarker"] = juce::KeyPress();
    bindings["transportJumpToPreviousMarker"] = juce::KeyPress();
    // Selection stepping -- unbound like the transport family, reachable from MIDI Remote.
    bindings["selectNextModule"] = juce::KeyPress();
    bindings["selectPreviousModule"] = juce::KeyPress();
    bindings["selectNextTrack"] = juce::KeyPress();
    bindings["selectPreviousTrack"] = juce::KeyPress();

    // "show the Controllers tab" -- see toggleTimelinePanel/toggleMixerPanel's own
    // comment above (the Cmd+1/2/3 family, permuted by a drag-reorder). Previously shipped
    // unbound; a returning user's saved unbound value carries forward unchanged, same
    // as toggleMixerPanel's old Cmd+Alt+M -- only toggleTimelinePanel's Cmd+T is migrated (see
    // migrateBottomPanelToggleKeys()), since only that one collides with the new
    // toggleBottomPanel default.
    bindings["toggleMidiRemotePanel"] = juce::KeyPress('3', juce::ModifierKeys::commandModifier, 0);
    // Cmd+Shift+B -- show or hide the active bottom-panel tab's side pane. Plain Cmd+B is the library
    // sidebar; the Shift chord is free on every platform.
    bindings["toggleSidePane"] =
        juce::KeyPress('b', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
}

void ShortcutManager::addGraphDefaultBindings() {
    // ---- Graph ----
    bindings["autoArrange"] = juce::KeyPress('l', juce::ModifierKeys::commandModifier, 0);
    // Cmd+Opt+S: Cmd+Shift+S is Save Project As. Modifier equality is exact, so Cmd+Alt+S can
    // never match the piano roll's bare Alt+S.
    bindings["saveSnippet"] =
        juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
    // Cmd+G / Cmd+Shift+G — the Cubase/Ableton convention for group/ungroup, and free on both
    // counts: 'g' appears nowhere else in this table, and no component keyPressed() override
    // matches it either.
    bindings["groupSelection"] = juce::KeyPress('g', juce::ModifierKeys::commandModifier, 0);
    bindings["ungroupSelection"] =
        juce::KeyPress('g', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    bindings["collapseMacro"] =
        juce::KeyPress('g', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
    // Cmd+Shift+M — the chord "focusTimeline"/"focusLibrary"'s own comment above earmarked
    // ("'m' is reserved for a future Mixer-focus shortcut") once a mixer existed. Master/Audio
    // Output locate is exactly that need in its canvas-only, pre-mixer-panel form (is the
    // eventual destination), so it claims the chord now rather than leaving it idle; free on
    // both counts, same as when it was reserved — no other binding in this table uses 'm' with
    // Cmd+Shift (bare Cmd+M is toggleModMatrix, a different chord entirely; bare unmodified 'm'
    // is timelineMuteFocusedTrack, Timeline category), and no component keyPressed() override
    // hardcodes it either.
    bindings["locateMaster"] =
        juce::KeyPress('m', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    // The canvas's card keys, exactly the defaults CanvasCardKeyboard falls back to with no manager.
    // No other Graph action uses an arrow or Return; the timeline's and the piano roll's bare arrows
    // are other categories, and those surfaces never hold focus together with the canvas.
    const auto none = juce::ModifierKeys::noModifiers;
    const auto alt = juce::ModifierKeys::altModifier;
    bindings["canvasSelectCardLeft"] = juce::KeyPress(juce::KeyPress::leftKey, none, 0);
    bindings["canvasSelectCardRight"] = juce::KeyPress(juce::KeyPress::rightKey, none, 0);
    bindings["canvasSelectCardUp"] = juce::KeyPress(juce::KeyPress::upKey, none, 0);
    bindings["canvasSelectCardDown"] = juce::KeyPress(juce::KeyPress::downKey, none, 0);
    bindings["canvasMoveCardLeft"] = juce::KeyPress(juce::KeyPress::leftKey, alt, 0);
    bindings["canvasMoveCardRight"] = juce::KeyPress(juce::KeyPress::rightKey, alt, 0);
    bindings["canvasMoveCardUp"] = juce::KeyPress(juce::KeyPress::upKey, alt, 0);
    bindings["canvasMoveCardDown"] = juce::KeyPress(juce::KeyPress::downKey, alt, 0);
    bindings["canvasEnterCard"] = juce::KeyPress(juce::KeyPress::returnKey, none, 0);
}

void ShortcutManager::addTimelineDefaultBindings() {
    // ---- Timeline ----
    // The bare-key DAW conventions. All three are already what TimelinePanelComponent's
    // keyPressed() hardcoded before it started resolving them through here, so the defaults are
    // a no-op for anyone who has already learned them. timelineSnapToggle is shared with the
    // piano roll on purpose: one binding, one key, whichever surface has focus.
    // J, not Q: Cubase's own snap key. Q is Cubase's *quantise*, which is what the piano roll
    // uses it for, so leaving snap on Q made one letter mean two different verbs depending on
    // which timeline surface had focus. Still shared with the roll — one binding, one key,
    // whichever surface has focus.
    bindings["timelineSnapToggle"] = juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToggleLoop"] = juce::KeyPress('l', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineLoopSelection"] = juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0);
    // F mirrors the transport strip's follow-playhead button, panel-scoped like J/L/P.
    bindings["timelineFollowPlayheadToggle"] = juce::KeyPress('f', juce::ModifierKeys::noModifiers, 0);
    // Holding Cmd+Left / Cmd+Right glides the cursor (TimelinePanelComponent). Nothing else binds an arrow with
    // Cmd: the timeline's own arrows are bare (clips) or Alt (move clip), the grid cycle is Ctrl+Shift, and the
    // category-scoped conflict check sees no other Timeline binding on these chords.
    bindings["timelineGlideBack"] =
        juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
    bindings["timelineGlideForward"] =
        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
    // Cubase's tool row (see synth::ui::EditTool for why 6 and 9 stay unclaimed). Bare
    // digits: category scoping is what makes that safe next to the Ctrl+Shift+digit grid block
    // below — and modifier equality is exact, so Ctrl+Shift+1 can never match a bare 1.
    bindings["timelineToolSelect"] = juce::KeyPress('1', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToolRange"] = juce::KeyPress('2', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToolSplit"] = juce::KeyPress('3', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToolGlue"] = juce::KeyPress('4', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToolErase"] = juce::KeyPress('5', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToolMute"] = juce::KeyPress('7', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineToolDraw"] = juce::KeyPress('8', juce::ModifierKeys::noModifiers, 0);
    // The Draw tool's shapes (synth::ui::DrawShape), Shift+1..6 in strip order: each picks Draw and the
    // shape. Stored as Shift plus the digit, never as the shifted glyph; keyPressMatches maps the '!'
    // macOS delivers back to '1'. No shipped version bound Shift+digit, so no saved setting shadows these.
    bindings["timelineShapeFree"] = juce::KeyPress('1', juce::ModifierKeys::shiftModifier, 0);
    bindings["timelineShapeLine"] = juce::KeyPress('2', juce::ModifierKeys::shiftModifier, 0);
    bindings["timelineShapeSine"] = juce::KeyPress('3', juce::ModifierKeys::shiftModifier, 0);
    bindings["timelineShapeTriangle"] = juce::KeyPress('4', juce::ModifierKeys::shiftModifier, 0);
    bindings["timelineShapeSaw"] = juce::KeyPress('5', juce::ModifierKeys::shiftModifier, 0);
    bindings["timelineShapeSquare"] = juce::KeyPress('6', juce::ModifierKeys::shiftModifier, 0);
    // Option+1 / Option+2: park the cursor on the left / right loop locator.
    //
    // A PLAIN Alt chord, deliberately NOT Ctrl+Shift+digit, and the reason is a real bug rather
    // than taste. These two briefly lived on Ctrl+Shift+1/2 — the chord the grid-set family
    // owns — and were dead in the app, because moving a DEFAULT binding does not migrate a
    // user's PERSISTED one: `saveToProperties` writes every action's key, so any install whose
    // settings had ever been saved still had `snapSetWhole`/`snapSetHalf` sitting on
    // Ctrl+Shift+1/2. `getActionsForKeyPress` then returned both ids for that chord, and
    // `MainComponent::keyPressed` takes "the first action bound to this key that HAS a command"
    // — so the stale grid COMMAND won and the locator jump never ran. Option+digit was never
    // bound to anything in any shipped version, so no persisted value can shadow it.
    //
    // Alt is also the one modifier family immune to the macOS shifted-character problem
    // `keyPressMatches` exists to work around: `charactersIgnoringModifiers` DOES ignore
    // Option, so Option+1 arrives carrying '1' and matches by key code directly. Stored as a
    // key code plus a modifier set, never as the glyph macOS delivers for Option+digit — the
    // same reason `pianoRollNavPrevNote` stores an arrow plus altModifier.
    bindings["timelineJumpToLocator1"] = juce::KeyPress('1', juce::ModifierKeys::altModifier, 0);
    bindings["timelineJumpToLocator2"] = juce::KeyPress('2', juce::ModifierKeys::altModifier, 0);
    // Bare m/s/r toggle Mute/Solo/Arm on whichever track header row currently holds
    // keyboard focus (TimelineTrackHeaderComponent::keyPressed) — the same J/L/P/F "bare letter,
    // panel-scoped surface action" convention as the block above, just resolved one component
    // deeper (the row itself, not the panel). Free on all three: no other binding in this table
    // uses a BARE (unmodified) m/s/r — 'm' elsewhere is always Cmd+M (toggleModMatrix), 's'
    // elsewhere is always Cmd/Cmd+Alt/Cmd+Shift/Alt/Ctrl+S, and 'r' elsewhere is always Cmd+R
    // (repeatSelection) — and modifier equality is exact, so none of those can ever match a bare
    // press. `timelineToolMute` (bare 7, "Mute Tool") is the one real naming-adjacency risk: it
    // is a different KEY (a digit, not a letter) so there is no binding collision, but the
    // Settings search matches DESCRIPTION text too, so these are named "Mute/Solo/Arm Focused
    // Track" rather than bare "Mute"/"Solo"/"Arm" to keep the two rows from reading as the same
    // feature in a filtered list.
    bindings["timelineMuteFocusedTrack"] = juce::KeyPress('m', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineSoloFocusedTrack"] = juce::KeyPress('s', juce::ModifierKeys::noModifiers, 0);
    bindings["timelineArmFocusedTrack"] = juce::KeyPress('r', juce::ModifierKeys::noModifiers, 0);
    // Bare A folds the focused row's automation lanes, the letter the header's automation button
    // used to carry. Free in every category: every other 'a' binding carries a modifier.
    bindings["timelineToggleTrackAutomation"] = juce::KeyPress('a', juce::ModifierKeys::noModifiers, 0);
    // Option+= / Option+- / Option+0 size the focused row, the zoom keys' shape on the one free
    // modifier: Option ignores the shifted-glyph problem, and no Option+=/-/0 is bound anywhere.
    bindings["timelineIncreaseTrackHeight"] = juce::KeyPress('=', juce::ModifierKeys::altModifier, 0);
    bindings["timelineDecreaseTrackHeight"] = juce::KeyPress('-', juce::ModifierKeys::altModifier, 0);
    bindings["timelineResetTrackHeight"] = juce::KeyPress('0', juce::ModifierKeys::altModifier, 0);
    // Cmd+D duplicates the focused track row. It shares the chord with the General "duplicateSelection"
    // on purpose: conflicts are per category, and the row's own keyPressed claims the key before the
    // command layer sees it, so Cmd+D elsewhere keeps its current meaning.
    bindings["timelineDuplicateFocusedTrack"] = juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0);
    // Clip keyboard mode: bare arrows step between clips (Right on a track header enters its
    // clips), Return opens the clip in its editor, Alt+Left/Right move it one grid step.
    // Piano-roll and mixer arrow keys live in other categories, so nothing here conflicts.
    const juce::ModifierKeys none, alt{juce::ModifierKeys::altModifier};
    bindings["timelineClipPrevious"] = juce::KeyPress(juce::KeyPress::leftKey, none, 0);
    bindings["timelineClipNext"] = juce::KeyPress(juce::KeyPress::rightKey, none, 0);
    bindings["timelineClipAbove"] = juce::KeyPress(juce::KeyPress::upKey, none, 0);
    bindings["timelineClipBelow"] = juce::KeyPress(juce::KeyPress::downKey, none, 0);
    bindings["timelineClipOpen"] = juce::KeyPress(juce::KeyPress::returnKey, none, 0);
    bindings["timelineClipMoveEarlier"] = juce::KeyPress(juce::KeyPress::leftKey, alt, 0);
    bindings["timelineClipMoveLater"] = juce::KeyPress(juce::KeyPress::rightKey, alt, 0);
    // Cmd+Backspace deletes the focused track row. Bare Backspace/Delete on clips and the canvas stay fixed keys
    // outside this table; the row claims only the Cmd chord, and only while it has focus.
    bindings["timelineDeleteFocusedTrack"] =
        juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0);
    // Cmd+Alt+Up / Down move the focused automation lane within its track, the way the canvas moves a card
    // with Alt+arrows. Unbound everywhere else: the only Cmd+Alt+arrow in the table would be this pair.
    const juce::ModifierKeys commandAlt{juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier};
    bindings["timelineMoveLaneUp"] = juce::KeyPress(juce::KeyPress::upKey, commandAlt, 0);
    bindings["timelineMoveLaneDown"] = juce::KeyPress(juce::KeyPress::downKey, commandAlt, 0);

    // REAL ctrlModifier, not commandModifier. On macOS the Ctrl+digit space is genuinely free
    // (Cmd+digit is reserved by hosts and by the native menu bar), which is what the user asked
    // for; on Windows/Linux juce::ModifierKeys::commandModifier IS ctrlModifier, so these read
    // as Ctrl+Shift+digit on every platform and the table needs no per-platform branch. The
    // digit key codes are what keeps them clear of the Cmd+Shift General bindings, which use
    // letters and the two zoom punctuation keys.
    //
    // These are back on their ORIGINAL Ctrl+Shift+digit home, which is also what every existing
    // install has persisted — see the locator note above for why briefly moving them to
    // Ctrl+Alt was the wrong half of the problem to solve.
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    bindings["snapSetWhole"] = juce::KeyPress('1', juce::ModifierKeys(ctrlShift), 0);
    bindings["snapSetHalf"] = juce::KeyPress('2', juce::ModifierKeys(ctrlShift), 0);
    bindings["snapSetQuarter"] = juce::KeyPress('3', juce::ModifierKeys(ctrlShift), 0);
    bindings["snapSetEighth"] = juce::KeyPress('4', juce::ModifierKeys(ctrlShift), 0);
    bindings["snapSetSixteenth"] = juce::KeyPress('5', juce::ModifierKeys(ctrlShift), 0);
    // 6/7/8 continue the row for the finer grid. Clear of everything on both counts:
    //  - Ctrl+Shift+digit appears nowhere else in this table (the Cmd+Shift General bindings are
    //    all letters plus the two zoom punctuation keys), so no binding-vs-binding conflict; and
    //  - the tool digits that share these key codes — bare 7 (Mute) and bare 8 (Draw); bare 6 is
    //    one of the three EditTool.h deliberately leaves unclaimed — carry NO modifiers, and
    //    modifier equality is exact on the binding side (keyPressMatches only normalizes the key
    //    CODE, never the modifier set), so Ctrl+Shift+7 can no more reach the Mute tool than
    //    Ctrl+Shift+1 could reach Select. The Option+digit locator pair above is a third
    //    modifier set on the same two key codes, and stays distinct for exactly the same reason.
    bindings["snapSetThirtySecond"] = juce::KeyPress('6', juce::ModifierKeys(ctrlShift), 0);
    bindings["snapSetSixtyFourth"] = juce::KeyPress('7', juce::ModifierKeys(ctrlShift), 0);
    bindings["snapSetHundredTwentyEighth"] = juce::KeyPress('8', juce::ModifierKeys(ctrlShift), 0);
    // Same modifier family as the eight above (they are the same verb, stepped instead of
    // absolute), on the horizontal arrows: coarser is left, finer is right, which matches the
    // snap combo reading coarsest-to-finest top-to-bottom. The piano roll's arrow bindings are
    // bare/Shift/Alt, so Ctrl+Shift is clear of all six of those as well.
    bindings["snapCyclePrev"] = juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(ctrlShift), 0);
    bindings["snapCycleNext"] = juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(ctrlShift), 0);
}

void ShortcutManager::addPianoRollDefaultBindings() {
    // ---- Piano roll ----
    // Exactly the defaults PianoRollComponent::keyPressed() falls back to when no manager is
    // installed — see its matchesAction(). Registering them here is what makes them rebindable;
    // MISSING one from this table would make that key INERT the moment a manager is installed,
    // which is what ShortcutManagerTest's surface-id tripwire exists to catch.
    bindings["pianoRollNudgeLeft"] = juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0);
    bindings["pianoRollNudgeRight"] = juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0);
    bindings["pianoRollTransposeUp"] = juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0);
    bindings["pianoRollTransposeDown"] = juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0);
    bindings["pianoRollTransposeOctaveUp"] =
        juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0);
    bindings["pianoRollTransposeOctaveDown"] =
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0);
    bindings["pianoRollNavPrevNote"] = juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0);
    bindings["pianoRollNavNextNote"] = juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0);
    // BARE Q IS QUANTISE in the piano roll (Cubase parity): on a note editor the most reachable
    // key belongs to the verb you use constantly, not to an on/off switch you set once a session.
    // Snap moved off Q to J — but as the SHARED "timelineSnapToggle" (which is J), not as a
    // piano-roll action of its own: two rebindable actions both labelled "Toggle Snap", both
    // defaulting to J and both flipping the same TimelineViewState flag would be a Settings list
    // the user cannot reason about. One binding, one key, whichever surface has focus, exactly as
    // before — only the key it lands on changed.
    bindings["pianoRollQuantise"] = juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0);
    // Alt+Q: the length twin of bare Q -- quantises note LENGTHS instead of note starts. Clear
    // of both bare Q and Option+Shift+Q below on every platform: modifier equality is exact, and
    // keyPressMatches never normalizes Alt out of a chord.
    bindings["pianoRollQuantiseLength"] = juce::KeyPress('q', juce::ModifierKeys::altModifier, 0);
    // Pitch quantise keeps Option+Shift+Q: still recognisably the same "Q family" verb, and one
    // chord away from anything destructive. Stored as a KEY CODE plus a modifier set, never as the
    // Unicode glyph macOS actually delivers for Option+letter ('œ' for Option+Q) — keyPressMatches
    // compares key codes, the same reason "pianoRollNavPrevNote" can store an arrow key plus
    // altModifier and still match.
    bindings["pianoRollQuantisePitches"] =
        juce::KeyPress('q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);
    // Option+S, the keyboard twin of the header's row-filter chip. Clear of "pianoRollToggleScalePanel"
    // (Ctrl+S, below) on every platform: modifier equality is exact, and Alt is Alt even on
    // Windows/Linux where JUCE's Cmd collapses onto Ctrl.
    bindings["pianoRollToggleScaleFilter"] = juce::KeyPress('s', juce::ModifierKeys::altModifier, 0);
    // Real ctrlModifier, deliberately NOT commandModifier — "savePreset" already owns Cmd+S, and
    // this toggle must never be that shortcut wearing a different hat. On macOS the two chords
    // are genuinely distinct physical keys. On Windows/Linux, where juce::ModifierKeys::
    // commandModifier IS ctrlModifier, a bare Ctrl+S while the roll has focus toggles the panel
    // INSTEAD of saving — the same "whichever surface has focus wins" contract
    // "timelineSnapToggle" already follows for its own bare 'q', and exactly why this is filed
    // under PianoRoll (a scoped category) rather than General: EveryDefaultBindingIsUnique only
    // checks within a category, by design.
    bindings["pianoRollToggleScalePanel"] = juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0);
    // Show / hide the velocity strip: a REAL Ctrl+V on macOS (Cmd+V is Paste), Cmd+Shift+V on
    // Windows/Linux, where Cmd IS Ctrl — the same per-platform split as "toggleAiPanel" above.
    bindings["pianoRollToggleVelocityLane"] =
        defaultsPlatform == DefaultsPlatform::Mac
            ? juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0)
            : juce::KeyPress('v', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);

    // Show / hide the mixer's Inserts, Sends and EQ rows while the mixer has focus -- the keyboard
    // twins of its toolbar toggles. On macOS a REAL Ctrl+letter, like "pianoRollToggleScalePanel":
    // Cmd+S is Save and Cmd+I/E are taken, while Ctrl is a distinct physical key there. On
    // Windows/Linux Cmd IS Ctrl, so Ctrl+S would shadow Save whenever the mixer is focused; those
    // platforms take Ctrl+Alt+letter instead.
    const auto mixerSectionMods =
        defaultsPlatform == DefaultsPlatform::Mac
            ? juce::ModifierKeys(juce::ModifierKeys::ctrlModifier)
            : juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier);
    bindings["mixerToggleInserts"] = juce::KeyPress('i', mixerSectionMods, 0);
    bindings["mixerToggleSends"] = juce::KeyPress('s', mixerSectionMods, 0);
    bindings["mixerToggleEq"] = juce::KeyPress('e', mixerSectionMods, 0);

    // Tab moves from the focused column into its send / insert rows (it falls through to the
    // app-wide region cycle when there is nothing to enter); bare E opens the focused column's EQ.
    // Return is not free for the EQ: it already selects the focused column on the canvas.
    bindings["mixerEnterRows"] = juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
    bindings["mixerOpenEq"] = juce::KeyPress('e', juce::ModifierKeys::noModifiers, 0);
    // Bare B bypasses the focused send or insert row; it is only read while a row holds the panel's row focus.
    bindings["mixerToggleRowBypass"] = juce::KeyPress('b', juce::ModifierKeys::noModifiers, 0);
}

// The card layout editor's list keys: Space shows or hides the focused control, Cmd+Up/Down moves it
// (the list's plain Up/Down move the focus), Return renames it. The panel is the only thing focused
// while it is open, so these share keys freely with the other categories.
void ShortcutManager::addLayoutEditorDefaultBindings() {
    bindings["layoutEditorToggleShown"] = juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers, 0);
    bindings["layoutEditorMoveUp"] = juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0);
    bindings["layoutEditorMoveDown"] = juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0);
    bindings["layoutEditorRename"] = juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::noModifiers, 0);
    // The on-card editor's nudge: an arrow moves the focused control 1 px, Shift+arrow 8 px.
    const auto arrow = [](int keyCode, bool big) {
        return juce::KeyPress(keyCode, big ? juce::ModifierKeys::shiftModifier : juce::ModifierKeys::noModifiers, 0);
    };
    bindings["layoutEditorNudgeLeft"] = arrow(juce::KeyPress::leftKey, false);
    bindings["layoutEditorNudgeRight"] = arrow(juce::KeyPress::rightKey, false);
    bindings["layoutEditorNudgeUp"] = arrow(juce::KeyPress::upKey, false);
    bindings["layoutEditorNudgeDown"] = arrow(juce::KeyPress::downKey, false);
    bindings["layoutEditorNudgeLeftBig"] = arrow(juce::KeyPress::leftKey, true);
    bindings["layoutEditorNudgeRightBig"] = arrow(juce::KeyPress::rightKey, true);
    bindings["layoutEditorNudgeUpBig"] = arrow(juce::KeyPress::upKey, true);
    bindings["layoutEditorNudgeDownBig"] = arrow(juce::KeyPress::downKey, true);
}
