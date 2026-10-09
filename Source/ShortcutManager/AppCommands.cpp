// Concern: AppCommands::getCommandForAction's action-id -> CommandID mapping. Kept out of
// AppCommands.h so adding a command action does not recompile every file that includes that header.
#include "AppCommands.h"

namespace AppCommands {
/** Maps an action id to the command it dispatches. Rationale per command, for whoever adds or
 *  changes a mapping below.
 *
 *  Commands with NO action id (menu-only: no chord, no Settings row, no ShortcutManager
 *  actionId/binding):
 *  - exportStems: offline stem export (StemExporter/StemRunner), one file per mixer channel strip,
 *    same range/format options as exportAudio; sits immediately after Export Audio in the File menu.
 *  - exportMidi: Standard MIDI File export of the arrangement or the current loop range
 *    (MidiClipFile); sits after Export Stems; needs no render, so it is never gated.
 *  - openPreset: a menu-only patch (`.json`) open. Two menu entry points open a file: a whole
 *    `.agsproj` project (openProject, the rebindable Cmd+O open) and a plain `.json` patch
 *    (openPreset). Loading a patch asks whether to replace the current one or add the loaded one on
 *    top of it.
 *  - checkForUpdates: not user-rebindable -- Sparkle's own convention is a plain "Check for
 *    Updates..." menu item with no keyboard shortcut. macOS only; see Source/Update/UpdateManager.h.
 *  - showWelcomeScreen: reopens the welcome screen overlay. Unlike checkForUpdates, registered
 *    unconditionally in every build (see MainComponent::getAllCommands) -- it needs no OS
 *    integration, only ownedAudioEngine != nullptr (never registered at all on the plugin path).
 *    Same menu-only, no-chord treatment as checkForUpdates.
 *  - whatsNew: build-time, no-network "What's New" dialog (the root CMakeLists.txt's WhatsNewData.h
 *    generation and MainComponent::showWhatsNewDialog). Same unconditional-registration, no-chord
 *    treatment as showWelcomeScreen.
 *  - contribute: opens the site's contribute page (branding::kContributeUrl) in the default browser
 *    -- no dialog, no prompt, no analytics event. Menu-only like showWelcomeScreen/whatsNew and
 *    registered unconditionally. Appended last, so no existing enumerator's value moves.
 *  - collectAndArchive: copies every external sample/wavetable into the project, saves, and
 *    optionally zips it (MainComponentCollectArchive.cpp). Menu-only like exportMidi. Appended last,
 *    so no existing enumerator's value moves.
 *
 *  Commands WITH an action id:
 *  - saveProjectAs: save-with-a-chooser, always -- the explicit escape hatch from savePreset's
 *    "resave silently to the remembered bundle" default. Rebindable (Cmd+Shift+S); see
 *    resetToDefaults().
 *  - exportPatchOnly: legacy patch-only export; writes a plain `.json` via GraphEditor::savePreset
 *    directly, never touching currentBundleDir_ or the window title -- a SIDE export, not "the
 *    project got saved". Rebindable, with a Cmd+Shift+P default (see resetToDefaults()).
 *  - exportAudio: offline audio bounce (BounceExporter/BounceRunner) -- the whole arrangement or
 *    the current loop range, rendered to WAV/AIFF. Rebindable (Cmd+Shift+E default); see
 *    resetToDefaults().
 *  - groupSelection / ungroupSelection: wrap/unwrap the selection in a Macro container.
 *    groupSelection (Cmd+G) is smart (GraphEditor::groupOrToggleSelectionMacros): it groups the
 *    selection into a new macro when none of it is already grouped, and otherwise toggles the
 *    touched macro(s) collapsed/expanded -- the same verb as collapseMacro, minus the explicit
 *    binding. ungroupSelection stays its own command: dissolving a macro is never something grouping
 *    or toggling should ever do as a side effect.
 *  - collapseMacro: collapses an expanded macro back to its card, or expands a collapsed one -- a
 *    TOGGLE, not a collapse/expand pair like group/ungroup. A menu row has no notion of a label that
 *    depends on what is selected right now, so this keeps a single static label ("Collapse / Expand
 *    Macro", see getActionDescription): the label never has to guess which way the toggle is about
 *    to go, so one command covers both directions.
 *  - foldAndPackMacros: folds every selected macro and, when the "packMacrosOnCollapse" preference is on, packs the
 *    folded cards into a grid; when every selected macro is already folded it expands them all instead. Appended
 *    last, so no existing enumerator's value moves.
 *  - repeatSelection: repeat-with-a-count. Only the two timeline surfaces implement it (see
 *    MainComponent::performRepeatSelection) but the command is registered unconditionally, the same
 *    way togglePlayback is, so the Settings shortcut list and ShortcutManager's tripwire tests cover
 *    it in every build configuration.
 *  - togglePlayback: Space play/stop. Always registered (getCommandInfo reports it inactive when
 *    there is no transport to toggle) so ShortcutManager's tripwire tests (unique default,
 *    description, command mapping) cover it unconditionally.
 *  - toggleBottomPanel: the ONE bottom-dock open/close toggle (see ShortcutManager.h's comment on
 *    the action id). toggleTimelinePanel, toggleMixerPanel and toggleMidiRemotePanel each just SHOW
 *    their tab, never close the dock.
 *  - toggleSidePane: shows or hides the active bottom-panel tab's side pane
 *    (docs/layout/side-pane.md).
 *  - snapSet*: the grid division set outright (Ctrl+Shift+1..8). Eight commands rather than one
 *    parameterised command because juce::ApplicationCommandManager has no notion of an argument: a
 *    menu row and a key binding are per-command, so "set the grid to 1/8" has to BE a command to be
 *    rebindable or to appear in the shortcut list at all. The finer half of the row
 *    (thirty-second, sixty-fourth, hundred-twenty-eighth) is appended after the coarser ones, never
 *    interleaved, so the ids read in the same coarse-to-fine order as the digits they bind to.
 *    Nothing persists a raw juce::CommandID -- the shortcut table keys off the action id STRING --
 *    so appending is free even though it renumbers every enumerator after it.
 *  - snapCyclePrev / snapCycleNext: step the grid coarser/finer (see
 *    TimelinePanelComponent::cycleSnapValue).
 *  - zoom*: routed per focused surface (see MainComponent::resolveEditSurface).
 *  - focusNextRegion / focusPrevRegion / focusTimeline / focusLibrary: the focus-region framework
 *    (see Source/UI/Layout/FocusRegion.h and docs/control/shortcuts.md). Tab and Shift+Tab cycle
 *    keyboard focus between whichever of the app's regions are currently OPEN (Library/Canvas/
 *    Timeline/AI Panel/Mod Matrix); the two Focus* commands open their target region first if it is
 *    closed, then focus it. All four are General, like every other command-dispatched action routed
 *    through MainComponent::keyPressed's existing loop.
 *  - focusLibrarySearch: opens the Library (if closed) and grabs focus on its search field
 *    specifically, rather than the region root focusLibrary lands on -- see
 *    ModuleLibraryComponent::focusSearchField and docs/control/shortcuts.md's Focus regions section
 *    for why those are two different destinations. Same General/command-dispatched treatment as the
 *    other three Focus* actions.
 *  - openContextMenu: opens the right-click menu of whatever holds keyboard focus
 *    (KeyboardContextMenu.h).
 *  - locateMaster: selects Master (falling back to Audio Output when there is no Master yet) and
 *    pans it into view -- the canvas-only answer to auto-arrange or a drag leaving either node
 *    anywhere (GraphEditor::locateMasterOrOutput). Appended per the snapSet rule (nothing persists
 *    a raw juce::CommandID); filed under Graph in the action table, alongside autoArrange, since it
 *    means nothing off the canvas.
 *  - openAddTrackMenu / renameSelectedModule / replaceSelectedModule: appended per the snapSet rule. The
 *    first shows the Timeline, focuses "+ Track" and opens its searchable menu (General); the other two act
 *    on the single selected module card (Graph) -- the inline title editor and the "Replace with..." search.
 *  - transportPlay / transportStop: transport verbs promoted to command-dispatched actions -- the
 *    prerequisite docs/control/midi-remote.md#action-targets asks for, since a MIDI Remote action
 *    target invokes a juce::CommandID. Filed under General (docs/control/shortcuts.md).
 *    Deliberately unbound by default (see resetToDefaults()) -- these exist to be command/MIDI-Remote
 *    targets, not new default keyboard shortcuts; togglePlayback's own Space binding is untouched.
 *    There is no transportTogglePlayStop enumerator: that action id is a pure alias resolved here
 *    straight to togglePlayback, so the persisted "togglePlayback" keybinding and its command id
 *    never move.
 *  - transportToggleLoop: command-dispatches the SAME setLoop(...,!looping) verb
 *    "timelineToggleLoop" already performs as a surface-resolved action -- the surface key keeps
 *    working unchanged (this function still answers kNoCommand for "timelineToggleLoop" itself).
 *  - transportRecord: routes through TimelineTransportBar's own record button, so it reaches the
 *    exact same MainComponent armed-track gate onRecordToggled does -- see
 *    MainComponentCommandTable.cpp.
 *  - transportToggleMetronome: routes through the transport bar's own metronome button, so its
 *    persisted "timelineMetronomeEnabled" state stays authoritative.
 *  - transportNudge* / transportJumpToLoopStart / transportJumpToLoopEnd: cursor moves and
 *    loop-locator jumps, command-dispatched so a MIDI Remote action target (or a keyboard shortcut)
 *    can fire them. Unbound by default like the transport family; appended after
 *    transportReturnToStart, never interleaved, so persisted ids stay stable.
 *  - transportJumpToNextMarker / transportJumpToPreviousMarker: jump to the next/previous timeline
 *    marker relative to the current position. Same command-dispatched, unbound-by-default reasoning
 *    as the transport nudge family; appended after transportJumpToLoopEnd, never interleaved.
 *  - selectNextModule / selectPreviousModule / selectNextTrack / selectPreviousTrack: step the
 *    selection to the next/previous module on the graph canvas or the next/previous track on the
 *    timeline. Two pairs rather than one focus-routed pair: a controller press must land the same
 *    way wherever the last mouse click was, and moving track focus itself changes which surface
 *    resolveEditSurface() reports. Appended last so no existing enumerator's value moves. */
juce::CommandID getCommandForAction(const juce::String& actionId) {
    if (actionId == "openSettings")
        return openSettings;
    if (actionId == "savePreset")
        return savePreset;
    if (actionId == "saveProjectAs")
        return saveProjectAs;
    if (actionId == "exportAudio")
        return exportAudio;
    if (actionId == "exportPatchOnly")
        return exportPatchOnly;
    if (actionId == "openProject")
        return openProject;
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
    if (actionId == "groupSelection")
        return groupSelection;
    if (actionId == "ungroupSelection")
        return ungroupSelection;
    if (actionId == "collapseMacro")
        return collapseMacro;
    if (actionId == "foldAndPackMacros")
        return foldAndPackMacros;
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
    if (actionId == "toggleBottomPanel")
        return toggleBottomPanel;
    if (actionId == "toggleTimelinePanel")
        return toggleTimelinePanel;
    if (actionId == "toggleMixerPanel")
        return toggleMixerPanel;
    if (actionId == "toggleMidiRemotePanel")
        return toggleMidiRemotePanel;
    if (actionId == "toggleSidePane")
        return toggleSidePane;
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
    if (actionId == "focusNextRegion")
        return focusNextRegion;
    if (actionId == "focusPrevRegion")
        return focusPrevRegion;
    if (actionId == "focusTimeline")
        return focusTimeline;
    if (actionId == "focusLibrary")
        return focusLibrary;
    if (actionId == "focusLibrarySearch")
        return focusLibrarySearch;
    if (actionId == "openContextMenu")
        return openContextMenu;
    if (actionId == "locateMaster")
        return locateMaster;
    if (actionId == "openAddTrackMenu")
        return openAddTrackMenu;
    if (actionId == "openFeedback")
        return openFeedback;
    if (actionId == "renameSelectedModule")
        return renameSelectedModule;
    if (actionId == "replaceSelectedModule")
        return replaceSelectedModule;
    if (actionId == "transportPlay")
        return transportPlay;
    if (actionId == "transportStop")
        return transportStop;
    // The alias -- see its enum-side comment above.
    if (actionId == "transportTogglePlayStop")
        return togglePlayback;
    if (actionId == "transportToggleLoop")
        return transportToggleLoop;
    if (actionId == "transportRecord")
        return transportRecord;
    if (actionId == "transportToggleMetronome")
        return transportToggleMetronome;
    if (actionId == "transportReturnToStart")
        return transportReturnToStart;
    if (actionId == "transportNudgeBackBeat")
        return transportNudgeBackBeat;
    if (actionId == "transportNudgeForwardBeat")
        return transportNudgeForwardBeat;
    if (actionId == "transportNudgeBackBar")
        return transportNudgeBackBar;
    if (actionId == "transportNudgeForwardBar")
        return transportNudgeForwardBar;
    if (actionId == "transportJumpToLoopStart")
        return transportJumpToLoopStart;
    if (actionId == "transportJumpToLoopEnd")
        return transportJumpToLoopEnd;
    if (actionId == "transportJumpToNextMarker")
        return transportJumpToNextMarker;
    if (actionId == "transportJumpToPreviousMarker")
        return transportJumpToPreviousMarker;
    if (actionId == "selectNextModule")
        return selectNextModule;
    if (actionId == "selectPreviousModule")
        return selectPreviousModule;
    if (actionId == "selectNextTrack")
        return selectNextTrack;
    if (actionId == "selectPreviousTrack")
        return selectPreviousTrack;
    // Every SURFACE action lands here — see kNoCommand.
    return kNoCommand;
}
} // namespace AppCommands
