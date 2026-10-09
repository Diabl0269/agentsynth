#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AppCommands {
// New enumerators are appended at the end so existing values never move (nothing persists a raw CommandID).
enum CommandIDs {
    openSettings = 0x100,
    savePreset,
    // Save with a chooser, always.
    saveProjectAs,
    // Legacy patch-only `.json` export; a side export, not "the project got saved".
    exportPatchOnly,
    // Offline audio bounce of the arrangement or loop range to WAV/AIFF.
    exportAudio,
    // Offline stem export, one file per mixer channel strip. Menu-only.
    exportStems,
    // Standard MIDI File export of the arrangement or loop range. Menu-only.
    exportMidi,
    // Menu-only patch open (a `.json`); asks whether to replace or add on top.
    openPreset,
    openProject,
    newPatch,
    undo,
    redo,
    toggleModMatrix,
    toggleMinimap,
    toggleAiPanel,
    autoArrange,
    // Group (smart: groups, or toggles already-grouped macros) / dissolve the selection's macro.
    groupSelection,
    ungroupSelection,
    // Toggles a macro between its collapsed card and expanded form.
    collapseMacro,
    toggleLibrary,
    selectAllModules,
    saveSnippet,
    copySelection,
    pasteSelection,
    duplicateSelection,
    cutSelection,
    // Repeat-with-a-count; only the two timeline surfaces act on it.
    repeatSelection,
    // Space play/stop; inactive when there is no transport to toggle.
    togglePlayback,
    // The one bottom-dock open/close toggle; the three below only show their tab.
    toggleBottomPanel,
    toggleTimelinePanel,
    toggleMixerPanel,
    toggleMidiRemotePanel,
    // Shows or hides the active bottom-panel tab's side pane.
    toggleSidePane,
    // Grid division set outright (Ctrl+Shift+1..8).
    snapSetWhole,
    snapSetHalf,
    snapSetQuarter,
    snapSetEighth,
    snapSetSixteenth,
    snapSetThirtySecond,
    snapSetSixtyFourth,
    snapSetHundredTwentyEighth,
    // Step the grid coarser/finer.
    snapCyclePrev,
    snapCycleNext,
    // Zoom, routed per focused surface.
    zoomInHorizontal,
    zoomOutHorizontal,
    zoomInVertical,
    zoomOutVertical,
    // Check for Updates menu item (macOS only; no chord).
    checkForUpdates,
    // Reopens the welcome screen overlay. Menu-only.
    showWelcomeScreen,
    // Shows the build-time "What's New" dialog. Menu-only.
    whatsNew,
    // Focus-region framework: Tab / Shift+Tab cycle open regions; the Focus* pair open then focus one.
    focusNextRegion,
    focusPrevRegion,
    focusTimeline,
    focusLibrary,
    // Opens the Library if closed and focuses its search field.
    focusLibrarySearch,
    // Opens the right-click menu of whatever holds keyboard focus.
    openContextMenu,
    // Selects Master (or Audio Output) and pans it into view.
    locateMaster,
    // Transport verbs as command targets; unbound by default.
    transportPlay,
    transportStop,
    // Toggles loop; the "timelineToggleLoop" surface key is unchanged.
    transportToggleLoop,
    // Same as the transport bar's record button, including the armed-track gate.
    transportRecord,
    // Same as the transport bar's metronome button.
    transportToggleMetronome,
    transportReturnToStart,
    // Cursor moves and loop-locator jumps; unbound by default.
    transportNudgeBackBeat,
    transportNudgeForwardBeat,
    transportNudgeBackBar,
    transportNudgeForwardBar,
    transportJumpToLoopStart,
    transportJumpToLoopEnd,
    // Jump to the next/previous timeline marker; unbound by default.
    transportJumpToNextMarker,
    transportJumpToPreviousMarker,
    // Opens the site's contribute page in the default browser. Menu-only.
    contribute,
    // Step the selection to the next/previous module on the canvas or track on the timeline.
    selectNextModule,
    selectPreviousModule,
    selectNextTrack,
    selectPreviousTrack,
    // Copies external samples/wavetables into the project, saves, optionally zips. Menu-only.
    collectAndArchive,
    // Folds every selected macro and, with the "pack" preference on, tidies their cards into a grid.
    foldAndPackMacros,
    // Shows the Timeline, focuses "+ Track" and opens its searchable menu.
    openAddTrackMenu,
    // On the one selected module card: opens the inline title editor / the "Replace with..." search.
    renameSelectedModule,
    replaceSelectedModule,
    // Opens the Send feedback window (the top bar's Feedback button). Appended, like every id above.
    openFeedback
};

/** What getCommandForAction() answers for a SURFACE action — an id that is rebindable and appears
 *  in the Settings list, but is consulted directly by a component's keyPressed() rather than
 *  dispatched through the command manager. Zero is juce::ApplicationCommandManager's own "not a
 *  command" value, so every existing `== 0` check keeps working; the name exists so call sites read
 *  as a deliberate check rather than as a magic number. */
inline constexpr juce::CommandID kNoCommand = 0;

/** Maps an action id to the command it dispatches, or kNoCommand for a SURFACE action (and any
 *  unknown id). Defined in AppCommands.cpp: a new command action is one branch there. */
juce::CommandID getCommandForAction(const juce::String& actionId);
} // namespace AppCommands
