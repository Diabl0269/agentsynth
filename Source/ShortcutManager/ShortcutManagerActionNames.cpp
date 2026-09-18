// Concern: ShortcutManager::getActionDescription's action-id -> display-name table. Split out of
// ShortcutManager.h (FRO125) purely to keep that header under the 1,000-line cap
// (scripts/file-size-baseline.txt) as new actions are added -- see the header's own doc comment
// on the declaration.
#include "ShortcutManager.h"

juce::String ShortcutManager::getActionDescription(const juce::String& actionId) {
    if (actionId == "openSettings")
        return "Open Settings";
    if (actionId == "savePreset")
        return "Save Preset";
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
        return "Locate Master";
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
    if (actionId == "toggleMixerPanel")
        return "Toggle Mixer Panel";
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
    // FRO125: the transport family (docs/control/midi-remote.md#action-targets). "Play"/"Stop" name the direction
    // outright; the alias reuses "Toggle Playback" verbatim since it IS togglePlayback's command
    // (see AppCommands::getCommandForAction) and must read as the same action, not a rival one.
    if (actionId == "transportPlay")
        return "Play";
    if (actionId == "transportStop")
        return "Stop";
    if (actionId == "transportTogglePlayStop")
        return "Toggle Playback";
    if (actionId == "transportToggleLoop")
        return "Toggle Looping";
    if (actionId == "transportRecord")
        return "Record";
    if (actionId == "transportToggleMetronome")
        return "Toggle Metronome";
    if (actionId == "transportReturnToStart")
        return "Return to Start";
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
    // T161: deliberately NOT "Mute"/"Solo"/"Arm" — the Settings tab's search matches this text,
    // and "timelineToolMute" is already "Mute Tool" (a bare-7 edit-tool mode, unrelated). Spelling
    // out "Focused Track" keeps the two from reading as the same feature in a filtered list.
    if (actionId == "timelineMuteFocusedTrack")
        return "Mute Focused Track";
    if (actionId == "timelineSoloFocusedTrack")
        return "Solo Focused Track";
    if (actionId == "timelineArmFocusedTrack")
        return "Arm Focused Track";
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
    return actionId;
}
