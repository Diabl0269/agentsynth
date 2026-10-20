// Concern: ShortcutManager's rebindable-action table -- the rows (id + category) behind
// getActionIds(). Kept out of ShortcutManager.h so adding an action does not recompile every file
// that includes that header.
#include "ShortcutManager.h"
#include "ShortcutManager/SnapNoteValueActions.h"
#include <algorithm>
#include <string_view>

namespace {

// The grid commands follow the other Timeline rows: the straight values, the triplet and dotted ones, then the cycle.
template <class Entry>
void spliceSnapRows(std::vector<Entry>& rows) {
    std::vector<Entry> snapRows;
    for (const char* id : {"snapSetWhole", "snapSetHalf", "snapSetQuarter", "snapSetEighth", "snapSetSixteenth",
                           "snapSetThirtySecond", "snapSetSixtyFourth", "snapSetHundredTwentyEighth"})
        snapRows.push_back({id, ShortcutCategory::Timeline});
    for (const auto& action : snapNoteValueActions())
        snapRows.push_back({action.actionId, ShortcutCategory::Timeline});
    snapRows.push_back({"snapCyclePrev", ShortcutCategory::Timeline});
    snapRows.push_back({"snapCycleNext", ShortcutCategory::Timeline});
    const auto at = std::find_if(rows.begin(), rows.end(),
                                 [](const Entry& row) { return std::string_view(row.id) == "pianoRollNudgeLeft"; });
    rows.insert(at, snapRows.begin(), snapRows.end());
}

} // namespace

/** One row per rebindable action: the persisted id, and the category that decides both its
 *  Settings section and its conflict scope. THE source of truth for both the id list and the
 *  categories.
 *
 *  ORDER IS LOAD-BEARING, twice over. It is getActionIds()' order, which ShortcutsSettingsTab
 *  indexes its rows by (and ShortcutsSettingsTabTests pins row i to ids[i]), and the categories
 *  must therefore stay CONTIGUOUS -- the tab draws one section header per run of same-category
 *  rows, so an id filed out of place would split its section in two. */
const std::vector<ShortcutManager::ActionEntry>& ShortcutManager::getActionTable() {
    static const std::vector<ActionEntry> table = [] {
        std::vector<ActionEntry> rows{
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
            // The ONE bottom-dock open/close toggle (docs/layout/chrome.md); the rows below only SHOW or SWAP a view.
            {"toggleBottomPanel", ShortcutCategory::General},
            // Show a view: default Cmd+1/2/3/4, one fixed key per view (a tab reorder never re-keys them).
            {"toggleTimelinePanel", ShortcutCategory::General},
            {"toggleMixerPanel", ShortcutCategory::General},
            {"toggleMidiRemotePanel", ShortcutCategory::General},
            {"showCanvas", ShortcutCategory::General},
            // Swap a view into the main area: default Opt+Cmd+1..4, the digit of the view's own show key.
            {"swapTimelineToMain", ShortcutCategory::General},
            {"swapMixerToMain", ShortcutCategory::General},
            {"swapMidiRemoteToMain", ShortcutCategory::General},
            {"swapCanvasToMain", ShortcutCategory::General},
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
            {"openAddTrackMenu", ShortcutCategory::General},
            // Opens the Send feedback window from anywhere (Cmd+Shift+F), the top bar's Feedback button.
            {"openFeedback", ShortcutCategory::General},
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
            {"foldAndPackMacros", ShortcutCategory::Graph},
            {"locateMaster", ShortcutCategory::Graph},
            {"renameSelectedModule", ShortcutCategory::Graph},
            {"replaceSelectedModule", ShortcutCategory::Graph},
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
            {"timelineFollowModeCycle", ShortcutCategory::Timeline},
            {"timelineGlideBack", ShortcutCategory::Timeline},
            {"timelineGlideForward", ShortcutCategory::Timeline},
            {"timelineToolSelect", ShortcutCategory::Timeline},
            {"timelineToolRange", ShortcutCategory::Timeline},
            {"timelineToolSplit", ShortcutCategory::Timeline},
            {"timelineToolGlue", ShortcutCategory::Timeline},
            {"timelineToolErase", ShortcutCategory::Timeline},
            {"timelineToolMute", ShortcutCategory::Timeline},
            {"timelineToolDraw", ShortcutCategory::Timeline},
            {"timelineShapeFree", ShortcutCategory::Timeline},
            {"timelineShapeLine", ShortcutCategory::Timeline},
            {"timelineShapeSine", ShortcutCategory::Timeline},
            {"timelineShapeTriangle", ShortcutCategory::Timeline},
            {"timelineShapeSaw", ShortcutCategory::Timeline},
            {"timelineShapeSquare", ShortcutCategory::Timeline},
            {"timelineShapeMenu", ShortcutCategory::Timeline},
            {"timelineJumpToLocator1", ShortcutCategory::Timeline},
            {"timelineJumpToLocator2", ShortcutCategory::Timeline},
            {"timelineMuteFocusedTrack", ShortcutCategory::Timeline},
            {"timelineSoloFocusedTrack", ShortcutCategory::Timeline},
            {"timelineArmFocusedTrack", ShortcutCategory::Timeline},
            // Folds the focused track header's automation lanes open/closed (bare A).
            {"timelineToggleTrackAutomation", ShortcutCategory::Timeline},
            // The focused track header's own row height (Option+= / Option+- / Option+0).
            {"timelineIncreaseTrackHeight", ShortcutCategory::Timeline},
            {"timelineDecreaseTrackHeight", ShortcutCategory::Timeline},
            {"timelineResetTrackHeight", ShortcutCategory::Timeline},
            // Duplicate / copy / paste from a track row (Cmd+D / Cmd+C / Cmd+V, the General actions' chords).
            {"timelineDuplicateFocusedTrack", ShortcutCategory::Timeline},
            {"timelineCopyFocusedTracks", ShortcutCategory::Timeline},
            {"timelineSelectAllTracks", ShortcutCategory::Timeline},
            {"timelinePasteTracks", ShortcutCategory::Timeline},
            // Shows the focused track's module on the canvas, and opens or closes an instrument's window (Ctrl+E).
            {"timelineShowFocusedTrackModule", ShortcutCategory::Timeline},
            // Opens or closes the focused plugin track's own window (Ctrl+Cmd+E; Ctrl+Alt+E off the Mac).
            {"timelineToggleFocusedTrackPluginWindow", ShortcutCategory::Timeline},
            {"timelineClipPrevious", ShortcutCategory::Timeline},
            {"timelineClipNext", ShortcutCategory::Timeline},
            {"timelineClipAbove", ShortcutCategory::Timeline},
            {"timelineClipBelow", ShortcutCategory::Timeline},
            {"timelineClipOpen", ShortcutCategory::Timeline},
            {"timelineClipMoveEarlier", ShortcutCategory::Timeline},
            {"timelineClipMoveLater", ShortcutCategory::Timeline},
            // The focused automation lane up or down within its track (Cmd+Alt+Up / Down).
            {"timelineMoveLaneUp", ShortcutCategory::Timeline},
            {"timelineMoveLaneDown", ShortcutCategory::Timeline},
            // Deletes the focused track header's track, after a question that can be turned off (Cmd+Backspace).
            {"timelineDeleteFocusedTrack", ShortcutCategory::Timeline},
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
            {"mixerToggleRowBypass", ShortcutCategory::Mixer},
            // Layout Editor -- the card layout editor's list (CardLayoutEditorComponent) and the on-card
            // editor's nudge keys (CardLayoutOnCardEditor), live only while an editor is open.
            {"layoutEditorToggleShown", ShortcutCategory::LayoutEditor},
            {"layoutEditorMoveUp", ShortcutCategory::LayoutEditor},
            {"layoutEditorMoveDown", ShortcutCategory::LayoutEditor},
            {"layoutEditorRename", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeLeft", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeRight", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeUp", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeDown", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeLeftBig", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeRightBig", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeUpBig", ShortcutCategory::LayoutEditor},
            {"layoutEditorNudgeDownBig", ShortcutCategory::LayoutEditor},
            {"layoutEditorRemoveControl", ShortcutCategory::LayoutEditor},
        };
        spliceSnapRows(rows);
        return rows;
    }();
    return table;
}
