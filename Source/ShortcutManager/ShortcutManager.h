#pragma once

#include "AppCommands.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

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
enum class ShortcutCategory { General, Graph, Timeline, PianoRoll, Mixer };

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
        migrateSaveAsChordSwap(*settings);
        migrateBottomPanelToggleKeys(*settings);
    }

    /** One-shot: Save Project As and Save Snippet swapped chords (Save As took the standard
     *  Cmd+Shift+S). saveToProperties() persists every action's key, so an install that ever saved
     *  its settings still holds the OLD pair and would keep Save As on Cmd+Opt+S with the snippet
     *  command shadowing the standard chord. Swap only when BOTH still hold the old defaults, so a
     *  user who rebound either one keeps their choice; the flag stops it re-firing if they later
     *  rebind back to the old pair on purpose. */
    void migrateSaveAsChordSwap(juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_saveAsCmdShiftS";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const auto cmdShiftS =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        const auto cmdOptS =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
        if (bindings["saveSnippet"] == cmdShiftS && bindings["saveProjectAs"] == cmdOptS) {
            bindings["saveSnippet"] = cmdOptS;
            bindings["saveProjectAs"] = cmdShiftS;
        }
    }

    /** One-shot, same shape as migrateSaveAsChordSwap above. The bottom-dock toggle used to
     *  be toggleTimelinePanel's own Cmd+T; a saved install still holds "shortcut_toggleTimelinePanel"
     *  = Cmd+T (saveToProperties() persists every action, not only rebound ones), which would
     *  otherwise collide with the new toggleBottomPanel action's own Cmd+T default -- both keyed to
     *  the same chord, resolved arbitrarily by getActionsForKeyPress()'s "first in actionIds order"
     *  rule. Move Cmd+T onto toggleBottomPanel and give toggleTimelinePanel its own new default
     *  (Cmd+1) instead, but ONLY when toggleTimelinePanel still holds the OLD default -- a user who
     *  rebound it keeps their choice, and toggleBottomPanel then just claims Cmd+T fresh (it is
     *  absent from every saved file, so it already has its own default with no rebind needed). */
    void migrateBottomPanelToggleKeys(juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_bottomPanelCmdT";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const auto cmdT = juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0);
        if (bindings["toggleTimelinePanel"] == cmdT) {
            bindings["toggleBottomPanel"] = cmdT;
            bindings["toggleTimelinePanel"] = juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0);
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

    /** Rebuilds `bindings` from scratch. Split by category (General/Graph/Timeline/Piano roll)
     *  into the addXDefaultBindings() helpers below purely to keep this function itself under the
     *  file's function-size cap -- the category split already existed as `// ---- X ----` comments
     *  in a single function; this promotes it to real structure. */
    void resetToDefaults() {
        bindings.clear();
        addGeneralDefaultBindings();
        addGraphDefaultBindings();
        addTimelineDefaultBindings();
        addPianoRollDefaultBindings();
    }

    // The per-category default bindings resetToDefaults() assembles (ShortcutManagerDefaults.cpp).
    void addGeneralDefaultBindings();
    void addGraphDefaultBindings();
    void addTimelineDefaultBindings();
    void addPianoRollDefaultBindings();

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

    // Display text for `actionId`'s row in Settings -> Keyboard Shortcuts and the MIDI Remote
    // action picker (docs/control/midi-remote.md#action-targets). Out-of-line in ShortcutManagerActionNames.cpp,
    // one `if` per action id, in getActionTable()'s own order -- kept off this header to leave
    // the 1,000-line file-size cap (scripts/file-size-baseline.txt) headroom for new actions.
    static juce::String getActionDescription(const juce::String& actionId);

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
        case ShortcutCategory::Mixer:
            return "Mixer";
        case ShortcutCategory::General:
            break;
        }
        return "General";
    }

    /** Sections are drawn in this order, and getActionIds() is grouped the same way — see
     *  getActionTable(). */
    static const std::vector<ShortcutCategory>& getCategoryOrder() {
        static const std::vector<ShortcutCategory> order{ShortcutCategory::General, ShortcutCategory::Graph,
                                                         ShortcutCategory::Timeline, ShortcutCategory::PianoRoll,
                                                         ShortcutCategory::Mixer};
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
            {"exportPatchOnly", ShortcutCategory::General},
            {"openProject", ShortcutCategory::General},
            {"newPatch", ShortcutCategory::General},
            {"undo", ShortcutCategory::General},
            {"redo", ShortcutCategory::General},
            {"toggleModMatrix", ShortcutCategory::General},
            {"toggleMinimap", ShortcutCategory::General},
            {"toggleAiPanel", ShortcutCategory::General},
            {"toggleLibrary", ShortcutCategory::General},
            // The ONE bottom-dock open/close toggle (docs/layout/chrome.md) -- opens or
            // closes the whole dock, reopening on whichever tab was last active. The three rows
            // below are no longer toggles themselves; each just SHOWS its tab (opening the dock if
            // needed) -- see their own comments.
            {"toggleBottomPanel", ShortcutCategory::General},
            // "show the Timeline/Mixer/Controllers tab" -- default Cmd+1/2/3, in the bottom
            // dock's default tab order. A drag-reorder of the tab strip PERMUTES these three
            // bindings so Cmd+N keeps naming the tab now in position N (BottomDockComponent::
            // permuteShortcutKeysForNewOrder) -- never a user's own rebind away from the Cmd+digit
            // convention, which the permute leaves alone (same guard shape as
            // migrateSaveAsChordSwap below).
            {"toggleTimelinePanel", ShortcutCategory::General},
            {"toggleMixerPanel", ShortcutCategory::General},
            {"toggleMidiRemotePanel", ShortcutCategory::General},
            {"toggleSidePane", ShortcutCategory::General},
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
            {"focusNextRegion", ShortcutCategory::General},
            {"focusPrevRegion", ShortcutCategory::General},
            {"focusTimeline", ShortcutCategory::General},
            {"focusLibrary", ShortcutCategory::General},
            {"focusLibrarySearch", ShortcutCategory::General},
            {"openContextMenu", ShortcutCategory::General},
            // Transport verbs promoted to command-dispatched actions (the prerequisite for
            // docs/control/midi-remote.md#action-targets) -- deliberately UNBOUND by default (see resetToDefaults()),
            // unlike every other row above. They exist as command/MIDI-Remote targets first; a
            // user may still rebind one in Settings. "transportTogglePlayStop" is not here: it is
            // a pure alias id resolved by AppCommands::getCommandForAction straight to
            // "togglePlayback" (which keeps its own row, and its Space binding, unchanged).
            {"transportPlay", ShortcutCategory::General},
            {"transportStop", ShortcutCategory::General},
            {"transportToggleLoop", ShortcutCategory::General},
            {"transportRecord", ShortcutCategory::General},
            {"transportToggleMetronome", ShortcutCategory::General},
            {"transportReturnToStart", ShortcutCategory::General},
            // Cursor moves and loop jumps, unbound by default like the transport verbs above.
            {"transportNudgeBackBeat", ShortcutCategory::General},
            {"transportNudgeForwardBeat", ShortcutCategory::General},
            {"transportNudgeBackBar", ShortcutCategory::General},
            {"transportNudgeForwardBar", ShortcutCategory::General},
            {"transportJumpToLoopStart", ShortcutCategory::General},
            {"transportJumpToLoopEnd", ShortcutCategory::General},
            // Jump to the next/previous timeline marker, unbound by default.
            {"transportJumpToNextMarker", ShortcutCategory::General},
            {"transportJumpToPreviousMarker", ShortcutCategory::General},
            // Selection stepping, unbound by default.
            {"selectNextModule", ShortcutCategory::General},
            {"selectPreviousModule", ShortcutCategory::General},
            {"selectNextTrack", ShortcutCategory::General},
            {"selectPreviousTrack", ShortcutCategory::General},
            // Graph — the verbs that mean nothing on any other surface.
            {"autoArrange", ShortcutCategory::Graph},
            {"saveSnippet", ShortcutCategory::Graph},
            {"groupSelection", ShortcutCategory::Graph},
            {"ungroupSelection", ShortcutCategory::Graph},
            {"collapseMacro", ShortcutCategory::Graph},
            {"locateMaster", ShortcutCategory::Graph},
            // Timeline — the panel's own keys (consulted by TimelinePanelComponent /
            // TimelineClipLaneArea) plus the grid commands, which act on the shared snap value.
            {"timelineSnapToggle", ShortcutCategory::Timeline},
            {"timelineToggleLoop", ShortcutCategory::Timeline},
            {"timelineLoopSelection", ShortcutCategory::Timeline},
            {"timelineFollowPlayheadToggle", ShortcutCategory::Timeline},
            {"timelineToolSelect", ShortcutCategory::Timeline},
            {"timelineToolRange", ShortcutCategory::Timeline},
            {"timelineToolSplit", ShortcutCategory::Timeline},
            {"timelineToolGlue", ShortcutCategory::Timeline},
            {"timelineToolErase", ShortcutCategory::Timeline},
            {"timelineToolMute", ShortcutCategory::Timeline},
            {"timelineToolDraw", ShortcutCategory::Timeline},
            {"timelineJumpToLocator1", ShortcutCategory::Timeline},
            {"timelineJumpToLocator2", ShortcutCategory::Timeline},
            {"timelineMuteFocusedTrack", ShortcutCategory::Timeline},
            {"timelineSoloFocusedTrack", ShortcutCategory::Timeline},
            {"timelineArmFocusedTrack", ShortcutCategory::Timeline},
            // Folds the focused track header's automation lanes open/closed (bare A).
            {"timelineToggleTrackAutomation", ShortcutCategory::Timeline},
            {"timelineClipPrevious", ShortcutCategory::Timeline},
            {"timelineClipNext", ShortcutCategory::Timeline},
            {"timelineClipAbove", ShortcutCategory::Timeline},
            {"timelineClipBelow", ShortcutCategory::Timeline},
            {"timelineClipOpen", ShortcutCategory::Timeline},
            {"timelineClipMoveEarlier", ShortcutCategory::Timeline},
            {"timelineClipMoveLater", ShortcutCategory::Timeline},
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
            {"pianoRollQuantiseLength", ShortcutCategory::PianoRoll},
            {"pianoRollQuantisePitches", ShortcutCategory::PianoRoll},
            {"pianoRollToggleScalePanel", ShortcutCategory::PianoRoll},
            {"pianoRollToggleVelocityLane", ShortcutCategory::PianoRoll},
            {"pianoRollToggleScaleFilter", ShortcutCategory::PianoRoll},
            // Mixer -- consulted by MixerPanelComponent::keyPressed only.
            {"mixerToggleInserts", ShortcutCategory::Mixer},
            {"mixerToggleSends", ShortcutCategory::Mixer},
            {"mixerToggleEq", ShortcutCategory::Mixer},
            {"mixerEnterRows", ShortcutCategory::Mixer},
            {"mixerOpenEq", ShortcutCategory::Mixer},
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

    // A ShortcutManager is usually a MainComponent-owned member that outlives every UI
    // surface holding a raw pointer to it, but a test that declares one as a LOCAL after the
    // component under test gets the opposite lifetime — the manager destructs first (reverse
    // declaration order) and a component whose OWN destructor unconditionally dereferences its
    // `shortcuts_` pointer (TimelinePanelComponent, which removeChangeListener()s itself) is a
    // heap-use-after-free the moment it destructs afterwards. Weak-referenceable so a holder that
    // cares (see TimelinePanelComponent::shortcutsWeak_) can guard its teardown against exactly
    // that instead of trusting every call site to remember an explicit setShortcutManager(nullptr)
    // before scope exit — the idiom the test files use, but which is a test-authoring convention, not a
    // compiler-enforced guarantee.
    JUCE_DECLARE_WEAK_REFERENCEABLE(ShortcutManager)

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
