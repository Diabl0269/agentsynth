// Concern: ShortcutManager's rebindable-action table -- the rows (id + category) behind
// getActionIds(). Kept out of ShortcutManager.h so adding an action does not recompile every file
// that includes that header.
#include "ShortcutManager.h"
#include "ShortcutManager/SnapNoteValueActions.h"
#include <algorithm>
#include <string_view>

namespace {

// Render to audio track (Shift+Cmd+R) sits after the linked-duplicate row; TimelineClipLaneArea resolves it.
template <class Entry>
void spliceRenderRow(std::vector<Entry>& rows) {
    const auto at = std::find_if(rows.begin(), rows.end(), [](const Entry& row) {
        return std::string_view(row.id) == "timelineClipDuplicateLinked";
    });
    rows.insert(at + 1, Entry{"timelineRenderToAudioTrack", ShortcutCategory::Timeline});
}

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

// A drum's own length: shorter and longer by a step, then by a bar, right after Play clip in the Drum Machine section.
template <class Entry>
void spliceDrumRowLengthRows(std::vector<Entry>& rows) {
    std::vector<Entry> lengthRows;
    for (const char* id :
         {"drumMachineRowShorter", "drumMachineRowLonger", "drumMachineRowShorterBar", "drumMachineRowLongerBar"})
        lengthRows.push_back({id, ShortcutCategory::DrumMachine});
    const auto at = std::find_if(rows.begin(), rows.end(),
                                 [](const Entry& row) { return std::string_view(row.id) == "drumMachinePlayClip"; });
    rows.insert(at + 1, lengthRows.begin(), lengthRows.end());
}

template <class Entry>
void spliceTimelineRows(std::vector<Entry>& rows) {
    spliceSnapRows(rows);
    spliceRenderRow(rows);
    spliceDrumRowLengthRows(rows);
}

// The positional dock keys sit right before the per-view show actions they replace as the digit owners: show the tab
// at a place of the tab order (default Cmd+1/2/3, so a reorder re-keys them), focus the main view (Cmd+0), and swap
// the tab at a place into the main area (default Opt+Cmd+1..3).
template <class Entry>
void spliceDockKeyRows(std::vector<Entry>& rows) {
    std::vector<Entry> dockRows;
    for (const char* id : {"showDockTab1", "showDockTab2", "showDockTab3", "focusMainView", "swapDockTab1ToMain",
                           "swapDockTab2ToMain", "swapDockTab3ToMain"})
        dockRows.push_back({id, ShortcutCategory::General});
    const auto at = std::find_if(rows.begin(), rows.end(),
                                 [](const Entry& row) { return std::string_view(row.id) == "toggleTimelinePanel"; });
    rows.insert(at, dockRows.begin(), dockRows.end());
}

// Set Song Key follows Return to Start, the last of the transport verbs.
template <class Entry>
void spliceSongKeyRow(std::vector<Entry>& rows) {
    const auto at = std::find_if(rows.begin(), rows.end(),
                                 [](const Entry& row) { return std::string_view(row.id) == "transportReturnToStart"; });
    rows.insert(at + 1, Entry{"setSongKey", ShortcutCategory::General});
}

template <class Entry>
void spliceRows(std::vector<Entry>& rows) {
    spliceTimelineRows(rows);
    spliceDockKeyRows(rows);
    spliceSongKeyRow(rows);
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
            {"toggleSoundBrowser", ShortcutCategory::General},
            // The ONE bottom-dock open/close toggle (docs/layout/chrome.md); the rows below only SHOW or SWAP a view.
            {"toggleBottomPanel", ShortcutCategory::General},
            {"toggleTimelinePanel", ShortcutCategory::General},
            {"toggleMixerPanel", ShortcutCategory::General},
            {"toggleMidiRemotePanel", ShortcutCategory::General},
            {"showCanvas", ShortcutCategory::General},
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
            {"openFeedback", ShortcutCategory::General},
            // Transport verbs as command-dispatched actions (docs/control/midi-remote.md#action-targets), UNBOUND by
            // default. "transportTogglePlayStop" has no row: an alias AppCommands resolves to "togglePlayback".
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
            {"renameSelectedModuleAlt", ShortcutCategory::Graph},
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
            {"timelineMonitorFocusedTrack", ShortcutCategory::Timeline},
            {"timelineToggleTrackAutomation", ShortcutCategory::Timeline}, // folds the focused row's lanes (bare A)
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
            // Linked copies of the selected clips (Shift+Cmd+D), resolved by TimelineClipLaneArea.
            {"timelineClipDuplicateLinked", ShortcutCategory::Timeline},
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
            // Drum machine -- consulted by DrumMachineComponent::keyPressed only.
            {"drumMachineToggleHit", ShortcutCategory::DrumMachine},
            {"drumMachineToggleAccent", ShortcutCategory::DrumMachine},
            {"drumMachineLouder", ShortcutCategory::DrumMachine},
            {"drumMachineSofter", ShortcutCategory::DrumMachine},
            {"drumMachinePlayClip", ShortcutCategory::DrumMachine},
            // Mixer -- consulted by MixerPanelComponent::keyPressed only.
            {"mixerToggleInserts", ShortcutCategory::Mixer},
            {"mixerToggleSends", ShortcutCategory::Mixer},
            {"mixerToggleEq", ShortcutCategory::Mixer},
            {"mixerEnterRows", ShortcutCategory::Mixer},
            {"mixerOpenEq", ShortcutCategory::Mixer},
            {"mixerToggleRowBypass", ShortcutCategory::Mixer},
            {"mixerMoveRowUp", ShortcutCategory::Mixer},
            {"mixerMoveRowDown", ShortcutCategory::Mixer},
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
        spliceRows(rows);
        return rows;
    }();
    return table;
}
