// Concern: ShortcutManager's rebindable-action table -- the rows (id + category) behind
// getActionIds(). Kept out of ShortcutManager.h so adding an action does not recompile every file
// that includes that header.
#include "ShortcutManager.h"

/** One row per rebindable action: the persisted id, and the category that decides both its
 *  Settings section and its conflict scope. THE source of truth for both the id list and the
 *  categories.
 *
 *  ORDER IS LOAD-BEARING, twice over. It is getActionIds()' order, which ShortcutsSettingsTab
 *  indexes its rows by (and ShortcutsSettingsTabTests pins row i to ids[i]), and the categories
 *  must therefore stay CONTIGUOUS -- the tab draws one section header per run of same-category
 *  rows, so an id filed out of place would split its section in two. */
const std::vector<ShortcutManager::ActionEntry>& ShortcutManager::getActionTable() {
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
        // Previous/next tab of whichever tabbed surface is in front: the Settings window's tab
        // strip, or the main window's bottom dock. Cycles with wrap-around.
        {"tabPrevious", ShortcutCategory::General},
        {"tabNext", ShortcutCategory::General},
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
        // The canvas's card keys -- consulted by CanvasCardKeyboard::keyPressed only.
        {"canvasSelectCardLeft", ShortcutCategory::Graph},
        {"canvasSelectCardRight", ShortcutCategory::Graph},
        {"canvasSelectCardUp", ShortcutCategory::Graph},
        {"canvasSelectCardDown", ShortcutCategory::Graph},
        {"canvasMoveCardLeft", ShortcutCategory::Graph},
        {"canvasMoveCardRight", ShortcutCategory::Graph},
        {"canvasMoveCardUp", ShortcutCategory::Graph},
        {"canvasMoveCardDown", ShortcutCategory::Graph},
        {"canvasEnterCard", ShortcutCategory::Graph},
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
        // Layout Editor -- the card layout editor's list (CardLayoutEditorComponent), focused only
        // while its panel is open.
        {"layoutEditorToggleShown", ShortcutCategory::LayoutEditor},
        {"layoutEditorMoveUp", ShortcutCategory::LayoutEditor},
        {"layoutEditorMoveDown", ShortcutCategory::LayoutEditor},
        {"layoutEditorRename", ShortcutCategory::LayoutEditor},
    };
    return table;
}
