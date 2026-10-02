// Concern: ShortcutManager::getActionDescription's action-id -> display-name table. Split out of
// ShortcutManager.h purely to keep that header under the 1,000-line cap
// (scripts/file-size-baseline.txt) as new actions are added -- see the header's own doc comment
// on the declaration.
#include "ShortcutManager.h"

namespace {

// The transport family, and the selection-stepping actions, split out of getActionDescription to keep
// that function under the function-size cap. Empty when `actionId` is not one of them.
juce::String transportAndSelectionActionName(const juce::String& actionId) {
    // The transport family (docs/control/midi-remote.md#action-targets). "Play"/"Stop" name the direction
    // outright; the alias reuses togglePlayback's "Play / Stop" verbatim since it IS togglePlayback's
    // command (see AppCommands::getCommandForAction) and must read as the same action, not a rival
    // one. The label sorts next to "Play" and "Stop" in the MIDI Remote action picker.
    if (actionId == "transportPlay")
        return "Play";
    if (actionId == "transportStop")
        return "Stop";
    if (actionId == "transportTogglePlayStop")
        return "Play / Stop";
    if (actionId == "transportToggleLoop")
        return "Toggle Looping";
    if (actionId == "transportRecord")
        return "Record";
    if (actionId == "transportToggleMetronome")
        return "Toggle Metronome";
    if (actionId == "transportReturnToStart")
        return "Return to Start";
    // Cursor moves and loop-locator jumps.
    if (actionId == "transportNudgeBackBeat")
        return "Move Cursor Back (Beat)";
    if (actionId == "transportNudgeForwardBeat")
        return "Move Cursor Forward (Beat)";
    if (actionId == "transportNudgeBackBar")
        return "Move Cursor Back (Bar)";
    if (actionId == "transportNudgeForwardBar")
        return "Move Cursor Forward (Bar)";
    if (actionId == "transportJumpToLoopStart")
        return "Jump to Loop Start";
    if (actionId == "transportJumpToLoopEnd")
        return "Jump to Loop End";
    // Jump to the next/previous timeline marker relative to the current position.
    if (actionId == "transportJumpToNextMarker")
        return "Jump to Next Marker";
    if (actionId == "transportJumpToPreviousMarker")
        return "Jump to Previous Marker";
    // Hold-to-glide cursor moves (Timeline category).
    if (actionId == "timelineGlideBack")
        return "Glide Cursor Back (Hold)";
    if (actionId == "timelineGlideForward")
        return "Glide Cursor Forward (Hold)";
    // Selection stepping.
    if (actionId == "selectNextModule")
        return "Select Next Module";
    if (actionId == "selectPreviousModule")
        return "Select Previous Module";
    if (actionId == "selectNextTrack")
        return "Select Next Track";
    if (actionId == "selectPreviousTrack")
        return "Select Previous Track";
    return {};
}

// The Mixer category, split out of getActionDescription to keep that function under the function-size
// cap. Empty when `actionId` is not a Mixer action.
juce::String mixerActionName(const juce::String& actionId) {
    if (actionId == "mixerToggleInserts")
        return "Show or Hide Mixer Inserts";
    if (actionId == "mixerToggleSends")
        return "Show or Hide Mixer Sends";
    if (actionId == "mixerToggleEq")
        return "Show or Hide Mixer EQ";
    if (actionId == "mixerEnterRows")
        return "Enter Mixer Send and Insert Rows";
    if (actionId == "mixerOpenEq")
        return "Open Mixer EQ";
    if (actionId == "mixerToggleRowBypass")
        return "Bypass Mixer Send or Insert";
    return {};
}

// The card layout editor's list keys. Empty when `actionId` is not one of them.
juce::String layoutEditorActionName(const juce::String& actionId) {
    if (actionId == "layoutEditorToggleShown")
        return "Show or Hide the Control on the Card";
    if (actionId == "layoutEditorMoveUp")
        return "Move the Control Up";
    if (actionId == "layoutEditorMoveDown")
        return "Move the Control Down";
    if (actionId == "layoutEditorRename")
        return "Rename the Control";
    return {};
}

// The canvas's card keys (CanvasCardKeyboard). Empty when `actionId` is not one of them.
juce::String canvasCardActionName(const juce::String& actionId) {
    if (actionId == "canvasSelectCardLeft")
        return "Select Card to the Left";
    if (actionId == "canvasSelectCardRight")
        return "Select Card to the Right";
    if (actionId == "canvasSelectCardUp")
        return "Select Card Above";
    if (actionId == "canvasSelectCardDown")
        return "Select Card Below";
    if (actionId == "canvasMoveCardLeft")
        return "Move Selected Cards Left";
    if (actionId == "canvasMoveCardRight")
        return "Move Selected Cards Right";
    if (actionId == "canvasMoveCardUp")
        return "Move Selected Cards Up";
    if (actionId == "canvasMoveCardDown")
        return "Move Selected Cards Down";
    if (actionId == "canvasEnterCard")
        return "Enter Selected Card";
    return {};
}

// The actions on the focused track header row, split out of getActionDescription to keep that
// function under the function-size cap.
juce::String focusedTrackActionName(const juce::String& actionId) {
    // Deliberately NOT "Mute"/"Solo"/"Arm" — the Settings tab's search matches this text,
    // and "timelineToolMute" is already "Mute Tool" (a bare-7 edit-tool mode, unrelated). Spelling
    // out "Focused Track" keeps the two from reading as the same feature in a filtered list.
    if (actionId == "timelineMuteFocusedTrack")
        return "Mute Focused Track";
    if (actionId == "timelineSoloFocusedTrack")
        return "Solo Focused Track";
    if (actionId == "timelineArmFocusedTrack")
        return "Arm Focused Track";
    if (actionId == "timelineToggleTrackAutomation")
        return "Show/Hide Track Automation";
    if (actionId == "timelineIncreaseTrackHeight")
        return "Increase Track Height";
    if (actionId == "timelineDecreaseTrackHeight")
        return "Decrease Track Height";
    if (actionId == "timelineResetTrackHeight")
        return "Reset Track Height";
    return {};
}

// The timeline's Draw shapes (Shift+1..6), split out of getActionDescription for the function-size cap.
// Free is "Free Draw Shape" so it does not read as a free-standing verb in a filtered list.
juce::String drawShapeActionName(const juce::String& actionId) {
    if (actionId == "timelineShapeFree")
        return "Free Draw Shape";
    if (actionId == "timelineShapeLine")
        return "Line Shape";
    if (actionId == "timelineShapeSine")
        return "Sine Shape";
    if (actionId == "timelineShapeTriangle")
        return "Triangle Shape";
    if (actionId == "timelineShapeSaw")
        return "Saw Shape";
    if (actionId == "timelineShapeSquare")
        return "Square Shape";
    return {};
}

} // namespace

juce::String ShortcutManager::getActionDescription(const juce::String& actionId) {
    if (actionId == "openSettings")
        return "Open Settings";
    if (actionId == "savePreset")
        return "Save Project";
    if (actionId == "saveProjectAs")
        return "Save Project As";
    if (actionId == "exportAudio")
        return "Export Audio";
    if (actionId == "exportPatchOnly")
        return "Export Patch Only";
    if (actionId == "openProject")
        return "Open Project";
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
    if (actionId == "groupSelection")
        return "Group / Toggle Macro";
    if (actionId == "ungroupSelection")
        return "Ungroup Macro";
    if (actionId == "collapseMacro")
        return "Collapse / Expand Macro";
    if (actionId == "locateMaster")
        return "Go to Output";
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
        return "Play / Stop";
    if (actionId == "toggleBottomPanel")
        return "Toggle Bottom Panel";
    if (actionId == "toggleTimelinePanel")
        return "Show Timeline Tab";
    if (actionId == "toggleMixerPanel")
        return "Show Mixer Tab";
    if (actionId == "toggleMidiRemotePanel")
        return "Show Controllers Tab";
    if (actionId == "toggleSidePane")
        return "Show/Hide Side Pane";
    if (actionId == "zoomInHorizontal")
        return "Zoom In";
    if (actionId == "zoomOutHorizontal")
        return "Zoom Out";
    if (actionId == "zoomInVertical")
        return "Zoom In Vertically";
    if (actionId == "zoomOutVertical")
        return "Zoom Out Vertically";
    if (actionId == "focusNextRegion")
        return "Focus Next Region";
    if (actionId == "focusPrevRegion")
        return "Focus Previous Region";
    if (actionId == "focusTimeline")
        return "Focus Timeline";
    if (actionId == "focusLibrary")
        return "Focus Library";
    if (actionId == "focusLibrarySearch")
        return "Focus Library Search";
    if (actionId == "openContextMenu")
        return "Open Context Menu";
    if (const auto name = canvasCardActionName(actionId); name.isNotEmpty())
        return name;
    if (const auto name = transportAndSelectionActionName(actionId); name.isNotEmpty())
        return name;
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
    if (actionId == "timelineToolRange")
        return "Range Tool";
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
    if (const auto name = drawShapeActionName(actionId); name.isNotEmpty())
        return name;
    // "Locator 1"/"Locator 2" rather than "loop start"/"loop end": the two are the same pair of
    // numbers, and every DAW that has this key calls them locators.
    if (actionId == "timelineJumpToLocator1")
        return "Jump to Locator 1";
    if (actionId == "timelineJumpToLocator2")
        return "Jump to Locator 2";
    if (const auto name = focusedTrackActionName(actionId); name.isNotEmpty())
        return name;
    // Clip keyboard mode. "Next Clip" also carries a track header into that track's clips.
    if (actionId == "timelineClipPrevious")
        return "Previous Clip";
    if (actionId == "timelineClipNext")
        return "Next Clip";
    if (actionId == "timelineClipAbove")
        return "Clip on Track Above";
    if (actionId == "timelineClipBelow")
        return "Clip on Track Below";
    if (actionId == "timelineClipOpen")
        return "Open Clip in Editor";
    if (actionId == "timelineClipMoveEarlier")
        return "Move Clip Earlier by One Grid Step";
    if (actionId == "timelineClipMoveLater")
        return "Move Clip Later by One Grid Step";
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
    if (actionId == "pianoRollQuantiseLength")
        return "Quantise Selected Note Lengths";
    if (actionId == "pianoRollQuantisePitches")
        return "Quantise Note Pitches to Scale";
    if (actionId == "pianoRollToggleScaleFilter")
        return "Show Only Scale Notes";
    if (actionId == "pianoRollToggleScalePanel")
        return "Toggle Scale Panel";
    if (actionId == "pianoRollToggleVelocityLane")
        return "Show or Hide Velocity Strip";
    if (const auto mixer = mixerActionName(actionId); mixer.isNotEmpty())
        return mixer;
    if (const auto editor = layoutEditorActionName(actionId); editor.isNotEmpty())
        return editor;
    return actionId;
}
