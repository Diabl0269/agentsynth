#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace AppCommands {
enum CommandIDs {
    openSettings = 0x100,
    savePreset,
    // Save-with-a-chooser, always — the explicit escape hatch from savePreset's "resave silently
    // to the remembered bundle" default. Rebindable (Cmd+Opt+S) — see resetToDefaults().
    saveProjectAs,
    // Legacy patch-only export: writes a plain `.json` via GraphEditor::savePreset directly, never
    // touching currentBundleDir_ or the window title. NOT rebindable — same treatment as
    // checkForUpdates below (no action id string, no default binding, no Settings-list row).
    exportPatchOnly,
    // Offline audio bounce (BounceExporter/BounceRunner) - the whole arrangement or the current
    // loop range, rendered to WAV/AIFF. Rebindable (Cmd+Shift+E default) - see resetToDefaults().
    exportAudio,
    openPreset,
    newPatch,
    undo,
    redo,
    toggleModMatrix,
    toggleMinimap,
    toggleAiPanel,
    autoArrange,
    toggleLibrary,
    selectAllModules,
    saveSnippet,
    copySelection,
    pasteSelection,
    duplicateSelection,
    cutSelection,
    // Repeat-with-a-count. Only the two timeline surfaces implement it (see
    // MainComponent::performRepeatSelection) but the command is registered unconditionally, the
    // same way togglePlayback below is, so the Settings shortcut list and ShortcutManager's
    // tripwire tests cover it in every build configuration.
    repeatSelection,
    // Space play/stop. Always registered (getCommandInfo reports it inactive when there is no
    // transport to toggle) so ShortcutManager's tripwire tests (unique default, description,
    // command mapping) cover it unconditionally.
    togglePlayback,
    toggleTimelinePanel,
    // ---- Grid division, set outright (Ctrl+Shift+1..8) ----
    // Eight commands rather than one parameterised command because juce::ApplicationCommandManager
    // has no notion of an argument: a menu row and a key binding are per-command, so "set the grid
    // to 1/8" has to BE a command to be rebindable or to appear in the shortcut list at all.
    snapSetWhole,
    snapSetHalf,
    snapSetQuarter,
    snapSetEighth,
    snapSetSixteenth,
    // The finer half of the row, APPENDED here (never interleaved) so the three ids read in the same
    // coarse-to-fine order as the digits they bind to. Nothing persists a raw juce::CommandID — the
    // shortcut table keys off the action id STRING — so appending is free even though it renumbers
    // every enumerator below.
    snapSetThirtySecond,
    snapSetSixtyFourth,
    snapSetHundredTwentyEighth,
    // Step the grid coarser/finer (see TimelinePanelComponent::cycleSnapValue).
    snapCyclePrev,
    snapCycleNext,
    // ---- Zoom, routed per focused surface (see MainComponent::resolveEditSurface) ----
    zoomInHorizontal,
    zoomOutHorizontal,
    zoomInVertical,
    zoomOutVertical,
    // Not user-rebindable (no ShortcutManager actionId/binding) — Sparkle's own convention is a
    // plain "Check for Updates…" menu item with no keyboard shortcut. macOS only; see
    // Source/Update/UpdateManager.h.
    checkForUpdates
};

/** What getCommandForAction() answers for a SURFACE action — an id that is rebindable and appears
 *  in the Settings list, but is consulted directly by a component's keyPressed() rather than
 *  dispatched through the command manager. Zero is juce::ApplicationCommandManager's own "not a
 *  command" value, so every existing `== 0` check keeps working; the name exists so call sites read
 *  as a deliberate check rather than as a magic number. */
inline constexpr juce::CommandID kNoCommand = 0;

inline juce::CommandID getCommandForAction(const juce::String& actionId) {
    if (actionId == "openSettings")
        return openSettings;
    if (actionId == "savePreset")
        return savePreset;
    if (actionId == "saveProjectAs")
        return saveProjectAs;
    if (actionId == "exportAudio")
        return exportAudio;
    if (actionId == "openPreset")
        return openPreset;
    if (actionId == "newPatch")
        return newPatch;
    if (actionId == "undo")
        return undo;
    if (actionId == "redo")
        return redo;
    if (actionId == "toggleModMatrix")
        return toggleModMatrix;
    if (actionId == "toggleMinimap")
        return toggleMinimap;
    if (actionId == "toggleAiPanel")
        return toggleAiPanel;
    if (actionId == "autoArrange")
        return autoArrange;
    if (actionId == "toggleLibrary")
        return toggleLibrary;
    if (actionId == "selectAllModules")
        return selectAllModules;
    if (actionId == "saveSnippet")
        return saveSnippet;
    if (actionId == "copySelection")
        return copySelection;
    if (actionId == "pasteSelection")
        return pasteSelection;
    if (actionId == "duplicateSelection")
        return duplicateSelection;
    if (actionId == "cutSelection")
        return cutSelection;
    if (actionId == "repeatSelection")
        return repeatSelection;
    if (actionId == "togglePlayback")
        return togglePlayback;
    if (actionId == "toggleTimelinePanel")
        return toggleTimelinePanel;
    if (actionId == "snapSetWhole")
        return snapSetWhole;
    if (actionId == "snapSetHalf")
        return snapSetHalf;
    if (actionId == "snapSetQuarter")
        return snapSetQuarter;
    if (actionId == "snapSetEighth")
        return snapSetEighth;
    if (actionId == "snapSetSixteenth")
        return snapSetSixteenth;
    if (actionId == "snapSetThirtySecond")
        return snapSetThirtySecond;
    if (actionId == "snapSetSixtyFourth")
        return snapSetSixtyFourth;
    if (actionId == "snapSetHundredTwentyEighth")
        return snapSetHundredTwentyEighth;
    if (actionId == "snapCyclePrev")
        return snapCyclePrev;
    if (actionId == "snapCycleNext")
        return snapCycleNext;
    if (actionId == "zoomInHorizontal")
        return zoomInHorizontal;
    if (actionId == "zoomOutHorizontal")
        return zoomOutHorizontal;
    if (actionId == "zoomInVertical")
        return zoomInVertical;
    if (actionId == "zoomOutVertical")
        return zoomOutVertical;
    // Every SURFACE action lands here — see kNoCommand.
    return kNoCommand;
}
} // namespace AppCommands

/** Which part of the app an action belongs to. Two jobs, and it is worth being explicit that they
 *  are the same list for a reason:
 *
 *  1. The Settings tab groups its rows into collapsible sections by category, so a user hunting for
 *     "the piano roll's transpose key" has one place to look instead of a 49-row flat list.
 *  2. Conflict detection is SCOPED to a category (see ShortcutManager::getConflictingAction). The
 *     surfaces never have keyboard focus at the same time, so a bare P meaning "loop the selection"
 *     in the timeline is not in competition with a bare P anywhere else — reporting that as a
 *     collision would force the bare-key DAW conventions (Q/L/P, the tool digits, the arrow keys)
 *     into modifier combinations nobody uses.
 *
 *  General is the residual, and deliberately wide: anything routed by
 *  MainComponent::resolveEditSurface() (copy/paste/cut/duplicate/repeat/select-all, and both zoom
 *  pairs) is General because it means something on EVERY surface — one key, whichever editor has
 *  focus. Graph holds only the verbs that have no meaning anywhere else. */
enum class ShortcutCategory { General, Graph, Timeline, PianoRoll };

// Public juce::ChangeBroadcaster so MULTIPLE surfaces can each react to a rebind independently —
// TimelinePanelComponent's tool-strip/snap/follow tooltips and (in a future cached-tooltip surface)
// anyone else, all via addChangeListener(this)/removeChangeListener(this), unsubscribing in their
// destructors since a ShortcutManager (owned by MainComponent) outlives them. This is DELIBERATELY
// additive alongside the pre-existing single-slot `onBindingsChanged` callback below (which
// MainComponent's own ctor already claims for updateCommandShortcuts()) rather than replacing it —
// a second listener assigning onBindingsChanged would silently clobber MainComponent's own
// subscription, since a bare std::function has exactly one slot.
class ShortcutManager : public juce::ChangeBroadcaster {
public:
    ShortcutManager() {
        for (const auto& entry : getActionTable())
            actionIds.add(entry.id);
        resetToDefaults();
    }

    void loadFromProperties(juce::ApplicationProperties& props) {
        appProperties = &props;
        auto* settings = props.getUserSettings();
        if (settings == nullptr)
            return;

        for (auto& actionId : actionIds) {
            auto key = "shortcut_" + actionId;
            if (settings->containsKey(key))
                bindings[actionId] = parseKeyPress(settings->getValue(key));
        }
    }

    void saveToProperties() {
        // Persistence is opt-in (no appProperties/no user-settings file is a legal, permanent
        // state — see loadFromProperties), but a binding that just changed in memory is real
        // either way, so the notification below is UNCONDITIONAL: a caller with nothing to persist
        // to disk still has every live listener told about the change, rather than being silently
        // skipped alongside the persistence it never asked for.
        if (appProperties != nullptr) {
            if (auto* settings = appProperties->getUserSettings()) {
                for (auto& actionId : actionIds)
                    settings->setValue("shortcut_" + actionId, encodeKeyPress(bindings.at(actionId)));
                appProperties->saveIfNeeded();
            }
        }
        if (onBindingsChanged)
            onBindingsChanged();
        // Synchronous on purpose: binding edits only ever happen on the message thread (the
        // Settings tab), and the subscribers rebuild tooltip strings — an async post would leave a
        // window where a just-rebound key shows its old hint, and makes headless tests
        // non-deterministic (nothing pumps the queue mid-test).
        sendSynchronousChangeMessage();
    }

    juce::KeyPress getBinding(const juce::String& actionId) const {
        auto it = bindings.find(actionId);
        return it != bindings.end() ? it->second : juce::KeyPress();
    }

    /** EVERY action bound to `key`, in getActionIds() order. Plural because one keypress can now
     *  legitimately name more than one action: a bare Left arrow is the piano roll's nudge AND
     *  nothing else, but the general shape ("a surface action and a command action could share a
     *  key across categories") is exactly what category-scoped conflict checking permits. Callers
     *  that dispatch commands walk this and take the first entry with a real command — see
     *  MainComponent::keyPressed. */
    juce::StringArray getActionsForKeyPress(const juce::KeyPress& key) const {
        juce::StringArray matches;
        for (const auto& actionId : actionIds)
            if (keyPressMatches(getBinding(actionId), key))
                matches.add(actionId);
        return matches;
    }

    /** THE one "did this keystroke fire that binding?" rule, shared by this class and by every
     *  component that resolves a surface action for itself (PianoRollComponent::matchesAction,
     *  TimelinePanelComponent::matchesAction). True when:
     *
     *   - the two are equal — key code compared case-insensitively, modifiers compared EXACTLY,
     *     which is what keeps Left / Shift+Left / Alt+Left three separate bindings and what keeps
     *     Ctrl+Shift+1 from ever matching a bare 1; OR
     *   - BOTH sides carry Shift and their key codes share a US-layout unshifted base — '!' and '1'
     *     are one physical key, so Ctrl+Shift+'!' fires the binding stored as Ctrl+Shift+'1'.
     *
     *  An invalid binding (an action the user cleared, or an id this build has never heard of)
     *  matches nothing.
     *
     *  WHY THE SECOND BRANCH EXISTS — the bug it fixes. On macOS,
     *  juce_NSViewComponentPeer_mac.mm's getKeyCodeFromEvent() derives a KeyPress's key code from
     *  `[ev charactersIgnoringModifiers]`, and its own comment concedes: "Unfortunately,
     *  charactersIgnoringModifiers does not ignore the shift key" — it compensates ONLY by
     *  upper-casing letters. So a Shift-chorded digit or symbol reaches keyPressed carrying the
     *  SHIFTED character as its key code: Ctrl+Shift+1 arrives as KeyPress('!', ctrl|shift) and
     *  never equalled the stored '1'. That killed the entire Ctrl+Shift+digit grid block AND both
     *  Cmd+Shift zoom keys (Cmd+Shift+'=' arrives as '+') in the real app, while every headless test
     *  stayed green — a test builds KeyPress('1', mods) directly and never goes through the peer.
     *
     *  LIMITATION, stated plainly: the table below is the US ANSI layout. Doing this properly needs
     *  the platform VIRTUAL key code, which identifies the physical key independently of layout and
     *  which juce::KeyPress does not carry — the peer has already collapsed the event to a character
     *  before any of our code sees it. On a layout where Shift+3 is not '#' (UK, French, German…)
     *  the affected chord falls back to exact match, i.e. exactly the behaviour it had before this
     *  function existed: nothing gets worse, and the layouts covered are the overwhelming majority.
     *  Bare (unshifted) keys and every letter are unaffected on every layout. */
    static bool keyPressMatches(const juce::KeyPress& binding, const juce::KeyPress& pressed) {
        if (!binding.isValid())
            return false;
        // Modifiers are never normalized — only the key CODE is. That is what keeps the bare tool
        // digits clear of the Ctrl+Shift grid commands they share key codes with.
        if (binding.getModifiers() != pressed.getModifiers())
            return false;
        if (towlower(binding.getKeyCode()) == towlower(pressed.getKeyCode()))
            return true;
        if (!binding.getModifiers().isShiftDown() || !pressed.getModifiers().isShiftDown())
            return false;
        return usLayoutUnshiftedBase(binding.getKeyCode()) == usLayoutUnshiftedBase(pressed.getKeyCode());
    }

    juce::String getActionForKeyPress(const juce::KeyPress& key) const {
        const auto matches = getActionsForKeyPress(key);
        return matches.isEmpty() ? juce::String() : matches[0];
    }

    // Broadcasts synchronously (see saveToProperties for why sync): the MUTATION is what the
    // tooltip subscribers care about, and a caller that rebinds without persisting (tests, any
    // future programmatic rebind) must still refresh them. A Settings-tab rebind therefore fires
    // listeners twice (here and in saveToProperties) — the refresh is idempotent and cheap.
    void setBinding(const juce::String& actionId, const juce::KeyPress& key) {
        bindings[actionId] = key;
        sendSynchronousChangeMessage();
    }

    /** The action already using `key` IN THE SAME CATEGORY as `actionId`, or an empty string.
     *
     *  Category-scoped on purpose (see ShortcutCategory): two surfaces that can never hold keyboard
     *  focus simultaneously may share a key, and the bare-key DAW conventions depend on it — the
     *  timeline's Q/L/P and tool digits, and the piano roll's arrows, would otherwise all read as
     *  collisions with each other and with any future surface. WITHIN a category the check is as
     *  strict as it ever was: a second General Cmd+X still reports the first one, which is what the
     *  Settings tab's auto-swap acts on.
     *
     *  An unknown `actionId` is treated as General, so a stray id can never silently claim a key
     *  that a real General action already owns. */
    juce::String getConflictingAction(const juce::String& actionId, const juce::KeyPress& key) const {
        const auto category = getCategory(actionId);
        for (auto& [otherId, binding] : bindings) {
            if (otherId == actionId || getCategory(otherId) != category)
                continue;
            if (bindingsCollide(binding, key))
                return otherId;
        }
        return {};
    }

    void resetToDefaults() {
        bindings.clear();

        // ---- General ----
        bindings["openSettings"] = juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0);
        bindings["savePreset"] = juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0);
        // Cmd+Opt+S, deliberately NOT Cmd+Shift+S (that chord is "saveSnippet" — two command
        // actions can never share a chord, since MainComponent::keyPressed dispatches the FIRST
        // bound action that has a command, and the loser would be permanently dead). Cmd+Opt+S is
        // free: the only other 's' bindings are savePreset (Cmd+S), saveSnippet (Cmd+Shift+S), and
        // pianoRollToggleScaleFilter (bare Alt+S) — and modifier equality is exact, so Cmd+Alt+S can
        // never match bare Alt+S.
        bindings["saveProjectAs"] =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
        // Cmd+Shift+E, the same chord Logic/Ableton use for bounce/export. Free on both counts: no
        // other binding in this table uses 'e' with any modifier set, and no keyPressed() override
        // hardcodes a bare 'e' either.
        bindings["exportAudio"] =
            juce::KeyPress('e', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        bindings["openPreset"] = juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0);
        bindings["newPatch"] = juce::KeyPress('n', juce::ModifierKeys::commandModifier, 0);
        bindings["undo"] = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0);
        bindings["redo"] =
            juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        bindings["toggleModMatrix"] = juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0);
        // 'k' with plain Cmd is unused by any other binding (Cmd+, / S / O / N / Z / Shift+Z / M
        // / A / L / B, Shift+A, Shift+S) — safe to claim for the minimap toggle (issue #159).
        bindings["toggleMinimap"] = juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0);
        // The platform-standard Select All owns the bare Cmd+A chord (every DAW reads it that
        // way), so the AI panel moves off it. On macOS it takes a REAL Ctrl+A — Ctrl is a distinct
        // physical modifier there, so the chord is free. On Windows/Linux JUCE's commandModifier
        // IS the Ctrl key, so Ctrl+A and Cmd+A would be the SAME chord (and would trip
        // EveryDefaultBindingIsUnique in Linux CI); those platforms take Cmd+Shift+A instead. One
        // of the very few per-platform defaults in this table — rebindable like everything else.
#if JUCE_MAC
        bindings["toggleAiPanel"] = juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0);
#else
        bindings["toggleAiPanel"] =
            juce::KeyPress('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
#endif
        bindings["toggleLibrary"] = juce::KeyPress('b', juce::ModifierKeys::commandModifier, 0);
        // 't' with plain Cmd is unused by any other binding (Cmd+, / S / O / N / Z / Shift+Z / M /
        // K / A / L / B, Shift+A, Shift+S, C / V / D) — safe to claim for the timeline panel
        // toggle.
        bindings["toggleTimelinePanel"] = juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0);
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
        bindings["repeatSelection"] = juce::KeyPress('r', juce::ModifierKeys::commandModifier, 0);
        // Bare spacebar, no modifiers — the platform DAW convention for play/stop. Safe to
        // claim app-wide for the same reason Cmd+C/V is: a focused juce::TextEditor consumes the
        // spacebar itself (types a space character) before it ever reaches MainComponent::
        // keyPressed, the sole dispatch point — see docs/shortcuts.md.
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

        // ---- Graph ----
        bindings["autoArrange"] = juce::KeyPress('l', juce::ModifierKeys::commandModifier, 0);
        bindings["saveSnippet"] =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);

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
        // Cubase's tool row (see synth::ui::EditTool for why 2, 6 and 9 stay unclaimed). Bare
        // digits: category scoping is what makes that safe next to the Ctrl+Shift+digit grid block
        // below — and modifier equality is exact, so Ctrl+Shift+1 can never match a bare 1.
        bindings["timelineToolSelect"] = juce::KeyPress('1', juce::ModifierKeys::noModifiers, 0);
        bindings["timelineToolSplit"] = juce::KeyPress('3', juce::ModifierKeys::noModifiers, 0);
        bindings["timelineToolGlue"] = juce::KeyPress('4', juce::ModifierKeys::noModifiers, 0);
        bindings["timelineToolErase"] = juce::KeyPress('5', juce::ModifierKeys::noModifiers, 0);
        bindings["timelineToolMute"] = juce::KeyPress('7', juce::ModifierKeys::noModifiers, 0);
        bindings["timelineToolDraw"] = juce::KeyPress('8', juce::ModifierKeys::noModifiers, 0);
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

        // ---- Piano roll ----
        // Exactly the defaults PianoRollComponent::keyPressed() falls back to when no manager is
        // installed — see its matchesAction(). Registering them here is what makes them rebindable;
        // MISSING one from this table would make that key INERT the moment a manager is installed,
        // which is what ShortcutManagerTest's surface-id tripwire exists to catch.
        bindings["pianoRollNudgeLeft"] = juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0);
        bindings["pianoRollNudgeRight"] = juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0);
        bindings["pianoRollTransposeUp"] = juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0);
        bindings["pianoRollTransposeDown"] =
            juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0);
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
        // Pitch quantise keeps Option+Shift+Q: still recognisably the same "Q family" verb, and one
        // chord away from anything destructive. Stored as a KEY CODE plus a modifier set, never as the
        // Unicode glyph macOS actually delivers for Option+letter ('œ' for Option+Q) — keyPressMatches
        // compares key codes, the same reason "pianoRollNavPrevNote" can store an arrow key plus
        // altModifier and still match.
        bindings["pianoRollQuantisePitches"] = juce::KeyPress(
            'q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);
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
    }

    static juce::String keyPressToDisplayString(const juce::KeyPress& key) {
        juce::String result;
        auto mods = key.getModifiers();

#if JUCE_MAC
        if (mods.isCommandDown())
            result += "Cmd + ";
        if (mods.isCtrlDown())
            result += "Ctrl + ";
#else
        if (mods.isCtrlDown())
            result += "Ctrl + ";
#endif
        if (mods.isAltDown())
            result += "Alt + ";
        if (mods.isShiftDown())
            result += "Shift + ";

        auto keyCode = key.getKeyCode();
        if (keyCode >= 'a' && keyCode <= 'z')
            result += juce::String::charToString(static_cast<juce::juce_wchar>(keyCode - 32));
        else if (keyCode >= 'A' && keyCode <= 'Z')
            result += juce::String::charToString(static_cast<juce::juce_wchar>(keyCode));
        else if (keyCode == ',')
            result += ",";
        else if (keyCode == '.')
            result += ".";
        else if (keyCode == '/')
            result += "/";
        else if (keyCode == ';')
            result += ";";
        else if (keyCode == '\'')
            result += "'";
        else if (keyCode == '[')
            result += "[";
        else if (keyCode == ']')
            result += "]";
        else if (keyCode == '-')
            result += "-";
        else if (keyCode == '=')
            result += "=";
        else if (keyCode == juce::KeyPress::spaceKey)
            result += "Space";
        // The extended keys, which are NOT characters: JUCE encodes them above 0x10000
        // (extendedKeyModifier), so charToString would render a stray glyph rather than a name. The
        // arrows earn their place here because six piano-roll actions and both grid-cycle commands
        // bind to them, and the Settings tab's search matches against this very string.
        else if (keyCode == juce::KeyPress::leftKey)
            result += "Left";
        else if (keyCode == juce::KeyPress::rightKey)
            result += "Right";
        else if (keyCode == juce::KeyPress::upKey)
            result += "Up";
        else if (keyCode == juce::KeyPress::downKey)
            result += "Down";
        else
            result += juce::String::charToString(static_cast<juce::juce_wchar>(keyCode));

        return result;
    }

    static juce::String getActionDescription(const juce::String& actionId) {
        if (actionId == "openSettings")
            return "Open Settings";
        if (actionId == "savePreset")
            return "Save Preset";
        if (actionId == "saveProjectAs")
            return "Save Project As";
        if (actionId == "exportAudio")
            return "Export Audio";
        if (actionId == "openPreset")
            return "Open Preset";
        if (actionId == "newPatch")
            return "New Patch";
        if (actionId == "undo")
            return "Undo";
        if (actionId == "redo")
            return "Redo";
        if (actionId == "toggleModMatrix")
            return "Toggle Mod Matrix";
        if (actionId == "toggleMinimap")
            return "Toggle Minimap";
        if (actionId == "toggleAiPanel")
            return "Toggle AI Panel";
        if (actionId == "autoArrange")
            return "Auto Arrange";
        if (actionId == "toggleLibrary")
            return "Toggle Module Library";
        // Kept as "selectAllModules" (both the actionId string and the AppCommands name) so a
        // binding a user already persisted under that key keeps working — the label is what
        // widened when the command grew per-surface routing, not the identity.
        if (actionId == "selectAllModules")
            return "Select All in Focused Editor";
        if (actionId == "saveSnippet")
            return "Save Selection as Snippet";
        if (actionId == "copySelection")
            return "Copy Selected Modules";
        if (actionId == "pasteSelection")
            return "Paste Modules";
        if (actionId == "duplicateSelection")
            return "Duplicate Selected Modules";
        if (actionId == "cutSelection")
            return "Cut Selection";
        if (actionId == "repeatSelection")
            return "Repeat Selection";
        if (actionId == "togglePlayback")
            return "Toggle Playback";
        if (actionId == "toggleTimelinePanel")
            return "Toggle Timeline Panel";
        if (actionId == "zoomInHorizontal")
            return "Zoom In";
        if (actionId == "zoomOutHorizontal")
            return "Zoom Out";
        if (actionId == "zoomInVertical")
            return "Zoom In Vertically";
        if (actionId == "zoomOutVertical")
            return "Zoom Out Vertically";
        if (actionId == "timelineSnapToggle")
            return "Toggle Snap";
        if (actionId == "timelineToggleLoop")
            return "Toggle Looping";
        if (actionId == "timelineLoopSelection")
            return "Loop the Selection";
        if (actionId == "timelineFollowPlayheadToggle")
            return "Toggle Follow Playhead";
        if (actionId == "timelineToolSelect")
            return "Select Tool";
        if (actionId == "timelineToolSplit")
            return "Split Tool";
        if (actionId == "timelineToolGlue")
            return "Glue Tool";
        if (actionId == "timelineToolErase")
            return "Erase Tool";
        if (actionId == "timelineToolMute")
            return "Mute Tool";
        if (actionId == "timelineToolDraw")
            return "Draw Tool";
        // "Locator 1"/"Locator 2" rather than "loop start"/"loop end": the two are the same pair of
        // numbers, and every DAW that has this key calls them locators.
        if (actionId == "timelineJumpToLocator1")
            return "Jump to Locator 1";
        if (actionId == "timelineJumpToLocator2")
            return "Jump to Locator 2";
        // Labelled with the same note values the snap combo shows ("1", "1/2", …) rather than
        // "Whole"/"Half", so the shortcut list and the selector name the grid identically.
        if (actionId == "snapSetWhole")
            return "Set Grid to 1";
        if (actionId == "snapSetHalf")
            return "Set Grid to 1/2";
        if (actionId == "snapSetQuarter")
            return "Set Grid to 1/4";
        if (actionId == "snapSetEighth")
            return "Set Grid to 1/8";
        if (actionId == "snapSetSixteenth")
            return "Set Grid to 1/16";
        if (actionId == "snapSetThirtySecond")
            return "Set Grid to 1/32";
        if (actionId == "snapSetSixtyFourth")
            return "Set Grid to 1/64";
        if (actionId == "snapSetHundredTwentyEighth")
            return "Set Grid to 1/128";
        if (actionId == "snapCyclePrev")
            return "Grid Coarser";
        if (actionId == "snapCycleNext")
            return "Grid Finer";
        if (actionId == "pianoRollNudgeLeft")
            return "Nudge Notes Left";
        if (actionId == "pianoRollNudgeRight")
            return "Nudge Notes Right";
        if (actionId == "pianoRollTransposeUp")
            return "Transpose Up a Semitone";
        if (actionId == "pianoRollTransposeDown")
            return "Transpose Down a Semitone";
        if (actionId == "pianoRollTransposeOctaveUp")
            return "Transpose Up an Octave";
        if (actionId == "pianoRollTransposeOctaveDown")
            return "Transpose Down an Octave";
        if (actionId == "pianoRollNavPrevNote")
            return "Select Previous Note";
        if (actionId == "pianoRollNavNextNote")
            return "Select Next Note";
        if (actionId == "pianoRollQuantise")
            return "Quantise Selected Notes";
        if (actionId == "pianoRollQuantisePitches")
            return "Quantise Note Pitches to Scale";
        if (actionId == "pianoRollToggleScaleFilter")
            return "Show Only Scale Notes";
        if (actionId == "pianoRollToggleScalePanel")
            return "Toggle Scale Panel";
        return actionId;
    }

    /** The category `actionId` belongs to. An id this build has never heard of answers General,
     *  which is the conservative choice: General is the widest conflict scope, so an unknown id can
     *  never quietly duplicate a real app-wide binding. */
    static ShortcutCategory getCategory(const juce::String& actionId) {
        for (const auto& entry : getActionTable())
            if (actionId == entry.id)
                return entry.category;
        return ShortcutCategory::General;
    }

    static juce::String getCategoryName(ShortcutCategory category) {
        switch (category) {
        case ShortcutCategory::Graph:
            return "Graph Editor";
        case ShortcutCategory::Timeline:
            return "Timeline";
        case ShortcutCategory::PianoRoll:
            return "Piano Roll";
        case ShortcutCategory::General:
            break;
        }
        return "General";
    }

    /** Sections are drawn in this order, and getActionIds() is grouped the same way — see
     *  getActionTable(). */
    static const std::vector<ShortcutCategory>& getCategoryOrder() {
        static const std::vector<ShortcutCategory> order{ShortcutCategory::General, ShortcutCategory::Graph,
                                                         ShortcutCategory::Timeline, ShortcutCategory::PianoRoll};
        return order;
    }

    /** The ids in `category`, in getActionIds() order (stable — it is the table's order). */
    static juce::StringArray getActionIdsInCategory(ShortcutCategory category) {
        juce::StringArray ids;
        for (const auto& entry : getActionTable())
            if (entry.category == category)
                ids.add(entry.id);
        return ids;
    }

    static juce::KeyPress parseKeyPress(const juce::String& encoded) {
        auto parts = juce::StringArray::fromTokens(encoded, ":", "");
        if (parts.size() == 2)
            return juce::KeyPress(parts[0].getIntValue(), juce::ModifierKeys(parts[1].getIntValue()), 0);
        return {};
    }

    static juce::String encodeKeyPress(const juce::KeyPress& key) {
        return juce::String(key.getKeyCode()) + ":" + juce::String(key.getModifiers().getRawFlags());
    }

    const juce::StringArray& getActionIds() const { return actionIds; }

    std::function<void()> onBindingsChanged;

private:
    /** One row per rebindable action: the persisted id, and the category that decides both its
     *  Settings section and its conflict scope. THE source of truth for both the id list and the
     *  categories — a new action is one line here plus a default binding, a description and (for a
     *  command action) an AppCommands entry.
     *
     *  ORDER IS LOAD-BEARING, twice over. It is getActionIds()' order, which ShortcutsSettingsTab
     *  indexes its rows by (and ShortcutsSettingsTabTests pins row i to ids[i]), and the categories
     *  must therefore stay CONTIGUOUS — the tab draws one section header per run of same-category
     *  rows, so an id filed out of place would split its section in two. */
    struct ActionEntry {
        const char* id;
        ShortcutCategory category;
    };

    static const std::vector<ActionEntry>& getActionTable() {
        static const std::vector<ActionEntry> table{
            // General — app-wide, plus everything routed per focused surface.
            {"openSettings", ShortcutCategory::General},
            {"savePreset", ShortcutCategory::General},
            {"saveProjectAs", ShortcutCategory::General},
            {"exportAudio", ShortcutCategory::General},
            {"openPreset", ShortcutCategory::General},
            {"newPatch", ShortcutCategory::General},
            {"undo", ShortcutCategory::General},
            {"redo", ShortcutCategory::General},
            {"toggleModMatrix", ShortcutCategory::General},
            {"toggleMinimap", ShortcutCategory::General},
            {"toggleAiPanel", ShortcutCategory::General},
            {"toggleLibrary", ShortcutCategory::General},
            {"toggleTimelinePanel", ShortcutCategory::General},
            {"selectAllModules", ShortcutCategory::General},
            {"copySelection", ShortcutCategory::General},
            {"pasteSelection", ShortcutCategory::General},
            {"duplicateSelection", ShortcutCategory::General},
            {"cutSelection", ShortcutCategory::General},
            {"repeatSelection", ShortcutCategory::General},
            {"togglePlayback", ShortcutCategory::General},
            {"zoomInHorizontal", ShortcutCategory::General},
            {"zoomOutHorizontal", ShortcutCategory::General},
            {"zoomInVertical", ShortcutCategory::General},
            {"zoomOutVertical", ShortcutCategory::General},
            // Graph — the two verbs that mean nothing on any other surface.
            {"autoArrange", ShortcutCategory::Graph},
            {"saveSnippet", ShortcutCategory::Graph},
            // Timeline — the panel's own keys (consulted by TimelinePanelComponent /
            // TimelineClipLaneArea) plus the grid commands, which act on the shared snap value.
            {"timelineSnapToggle", ShortcutCategory::Timeline},
            {"timelineToggleLoop", ShortcutCategory::Timeline},
            {"timelineLoopSelection", ShortcutCategory::Timeline},
            {"timelineFollowPlayheadToggle", ShortcutCategory::Timeline},
            {"timelineToolSelect", ShortcutCategory::Timeline},
            {"timelineToolSplit", ShortcutCategory::Timeline},
            {"timelineToolGlue", ShortcutCategory::Timeline},
            {"timelineToolErase", ShortcutCategory::Timeline},
            {"timelineToolMute", ShortcutCategory::Timeline},
            {"timelineToolDraw", ShortcutCategory::Timeline},
            {"timelineJumpToLocator1", ShortcutCategory::Timeline},
            {"timelineJumpToLocator2", ShortcutCategory::Timeline},
            {"snapSetWhole", ShortcutCategory::Timeline},
            {"snapSetHalf", ShortcutCategory::Timeline},
            {"snapSetQuarter", ShortcutCategory::Timeline},
            {"snapSetEighth", ShortcutCategory::Timeline},
            {"snapSetSixteenth", ShortcutCategory::Timeline},
            {"snapSetThirtySecond", ShortcutCategory::Timeline},
            {"snapSetSixtyFourth", ShortcutCategory::Timeline},
            {"snapSetHundredTwentyEighth", ShortcutCategory::Timeline},
            {"snapCyclePrev", ShortcutCategory::Timeline},
            {"snapCycleNext", ShortcutCategory::Timeline},
            // Piano roll — consulted by PianoRollComponent::keyPressed only.
            {"pianoRollNudgeLeft", ShortcutCategory::PianoRoll},
            {"pianoRollNudgeRight", ShortcutCategory::PianoRoll},
            {"pianoRollTransposeUp", ShortcutCategory::PianoRoll},
            {"pianoRollTransposeDown", ShortcutCategory::PianoRoll},
            {"pianoRollTransposeOctaveUp", ShortcutCategory::PianoRoll},
            {"pianoRollTransposeOctaveDown", ShortcutCategory::PianoRoll},
            {"pianoRollNavPrevNote", ShortcutCategory::PianoRoll},
            {"pianoRollNavNextNote", ShortcutCategory::PianoRoll},
            {"pianoRollQuantise", ShortcutCategory::PianoRoll},
            {"pianoRollQuantisePitches", ShortcutCategory::PianoRoll},
            {"pianoRollToggleScalePanel", ShortcutCategory::PianoRoll},
            {"pianoRollToggleScaleFilter", ShortcutCategory::PianoRoll},
        };
        return table;
    }

    /** BINDING-vs-BINDING equality, for conflict detection only — case-insensitive on the key code
     *  and EXACT on the modifiers. Deliberately NOT keyPressMatches: that function's shifted-symbol
     *  normalization exists to rescue a real KEYSTROKE from the macOS peer (see its comment), and
     *  applying it here would merge two different STORED chords — a user who deliberately put one
     *  action on Cmd+Shift+'=' and another on Cmd+Shift+'+' would be told they collide, and the
     *  Settings tab's auto-swap would then quietly steal one of them. An invalid binding (an action
     *  the user cleared) collides with nothing. */
    static bool bindingsCollide(const juce::KeyPress& binding, const juce::KeyPress& key) {
        return binding.isValid() && towlower(binding.getKeyCode()) == towlower(key.getKeyCode()) &&
               binding.getModifiers() == key.getModifiers();
    }

    /** The US-ANSI unshifted character on the same physical key as `keyCode`: the digit row, plus
     *  the two punctuation keys the zoom pair uses. Anything else — every letter included, since the
     *  macOS peer already upper-cases those and key-code comparison is case-insensitive — comes back
     *  unchanged, and so do the extended keys (arrows and friends live above 0x10000).
     *
     *  keyPressMatches folds BOTH of its arguments through this, which is what makes the
     *  normalization bidirectional: a binding stored WITH the shifted character still matches a
     *  press that arrives as the base character. That direction is not hypothetical — the Settings
     *  tab records the juce::KeyPress it is handed, so every chord a user rebound on macOS while
     *  this bug was live was persisted as the shifted glyph. */
    static int usLayoutUnshiftedBase(int keyCode) {
        switch (keyCode) {
        case '!':
            return '1';
        case '@':
            return '2';
        case '#':
            return '3';
        case '$':
            return '4';
        case '%':
            return '5';
        case '^':
            return '6';
        case '&':
            return '7';
        case '*':
            return '8';
        case '(':
            return '9';
        case ')':
            return '0';
        case '+':
            return '=';
        case '_':
            return '-';
        default:
            return keyCode;
        }
    }

    std::map<juce::String, juce::KeyPress> bindings;
    juce::ApplicationProperties* appProperties = nullptr;

    // Built from getActionTable() in the constructor, so the order and the categories can never
    // drift apart.
    juce::StringArray actionIds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutManager)
};

/** Display text for `actionId`'s CURRENT binding — the shared helper every tooltip that names a
 *  rebindable key routes through, so a rebind can never leave a tooltip showing a stale key.
 *
 *  - `manager` non-null: uses its LIVE binding. An unset/cleared binding is a real state (the user
 *    deliberately removed the key) and returns an EMPTY string — a tooltip must not claim a key
 *    that does nothing.
 *  - `manager` null: uses `fallback` — the same "no manager installed -> the component's own
 *    hardcoded default" contract every surface's keyPressed()/matchesAction() already follows.
 *
 *  Formatting reuses ShortcutManager::keyPressToDisplayString (the "Ctrl + X" / "Shift + X" family
 *  this app's tooltips already show everywhere via synth::ui::formatShortcutHint) with ONE
 *  deliberate change: a BARE, unmodified letter renders lowercase ("q", "f", "l", "p") rather than
 *  upper — every one of this app's single-letter DAW-convention keys (Q/L/P/F, the tool digits) is
 *  conventionally shown lowercase, and keyPressToDisplayString's upper-casing exists for the
 *  modifier-chord case, not the bare-letter one. Anything carrying a modifier, or a non-letter key
 *  (an arrow, a digit, space…), is returned exactly as keyPressToDisplayString spells it. */
inline juce::String shortcutHintFor(const ShortcutManager* manager, const juce::String& actionId,
                                    const juce::KeyPress& fallback) {
    const juce::KeyPress key = manager != nullptr ? manager->getBinding(actionId) : fallback;
    if (!key.isValid())
        return {};

    const auto display = ShortcutManager::keyPressToDisplayString(key);
    const auto code = key.getKeyCode();
    const bool bareLetter =
        key.getModifiers() == juce::ModifierKeys() && ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z'));
    return bareLetter ? display.toLowerCase() : display;
}

/** Bare display name for the platform's primary modifier key ("Cmd" on macOS, "Ctrl" everywhere
 *  else) — the same #if JUCE_MAC ShortcutManager::keyPressToDisplayString uses above, pulled out
 *  for copy that names the modifier on its own rather than as part of a rebindable KeyPress (e.g.
 *  "hold Cmd while scrolling to zoom" — a mouse-wheel gesture, not an action in the shortcuts
 *  table, so shortcutHintFor doesn't apply). A hardcoded "Cmd" in a tooltip or label is wrong on
 *  every non-Mac build; route it through this instead. */
inline juce::String platformCommandKeyName() {
#if JUCE_MAC
    return "Cmd";
#else
    return "Ctrl";
#endif
}
