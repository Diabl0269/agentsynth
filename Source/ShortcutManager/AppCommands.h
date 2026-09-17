#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AppCommands {
enum CommandIDs {
    openSettings = 0x100,
    savePreset,
    // Save-with-a-chooser, always — the explicit escape hatch from savePreset's "resave silently
    // to the remembered bundle" default. Rebindable (Cmd+Opt+S) — see resetToDefaults().
    saveProjectAs,
    // Legacy patch-only export: writes a plain `.json` via GraphEditor::savePreset directly, never
    // touching currentBundleDir_ or the window title -- a SIDE export, not "the project got
    // saved". Rebindable since P8-20 with a Cmd+Shift+P default (see resetToDefaults()); until
    // then it kept the checkForUpdates treatment (a menu-only item with no chord and no row).
    exportPatchOnly,
    // Offline audio bounce (BounceExporter/BounceRunner) - the whole arrangement or the current
    // loop range, rendered to WAV/AIFF. Rebindable (Cmd+Shift+E default) - see resetToDefaults().
    exportAudio,
    // P9-8: offline stem export (StemExporter/StemRunner) - one file per mixer channel strip, same
    // range/format options as exportAudio. Menu-only, immediately after Export Audio in the File
    // menu - same "no chord, no Settings row" treatment as openPreset/checkForUpdates below (no
    // ShortcutManager actionId/binding).
    exportStems,
    // P8-31 split the former single "Load from file..." browser into two menu entry points:
    // a whole `.agsproj` project and a plain `.json` patch. openProject is the rebindable Cmd+O
    // open (a project); openPreset is a menu-only patch open. Loading a patch asks whether to
    // replace the current one or add the loaded one on top of it.
    openPreset,
    openProject,
    newPatch,
    undo,
    redo,
    toggleModMatrix,
    toggleMinimap,
    toggleAiPanel,
    autoArrange,
    // Wrap/unwrap the selection in a Macro container (P8-12). groupSelection (Cmd+G) is smart
    // (P8-14, GraphEditor::groupOrToggleSelectionMacros): it groups the selection into a new
    // macro when none of it is already grouped, and otherwise toggles the touched macro(s)
    // collapsed/expanded — the same verb as collapseMacro below, minus the explicit binding.
    // ungroupSelection stays its own command: dissolving a macro is never something grouping or
    // toggling should ever do as a side effect.
    groupSelection,
    ungroupSelection,
    // Collapses an expanded macro back to its card, or expands a collapsed one — a TOGGLE, not
    // the collapse/expand pair the comment above rules out for group/ungroup. That objection
    // ("a menu row has no notion of a label that depends on what's selected right now") is
    // answered here by keeping a single static label ("Collapse / Expand Macro",
    // getActionDescription below): the label never has to guess which way the toggle is about to
    // go, so one command covers both directions. Originally shipped collapse-only (P8-12
    // follow-up: expanding left no way back short of Undo) before this toggle behaviour.
    collapseMacro,
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
    toggleMixerPanel,
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
    checkForUpdates,
    // T114/P8-10: reopens the welcome screen overlay. Unlike checkForUpdates, registered
    // unconditionally in every build (see MainComponent::getAllCommands) — it needs no OS
    // integration, only ownedAudioEngine != nullptr (never registered at all on the plugin path).
    // Same "menu-only item with no chord" treatment as checkForUpdates: no ShortcutManager
    // actionId/binding.
    showWelcomeScreen,
    // Build-time, no-network "What's New" dialog (Feature 2 of T114/P8-10) — see the root
    // CMakeLists.txt's WhatsNewData.h generation and MainComponent::showWhatsNewDialog. Same
    // unconditional-registration, no-chord treatment as showWelcomeScreen above.
    whatsNew,
    // T159: the focus-region framework (see Source/UI/Layout/FocusRegion.h and docs/shortcuts.md). Tab and
    // Shift+Tab cycle keyboard focus between whichever of the app's regions are currently OPEN
    // (Library/Canvas/Timeline/AI Panel/Mod Matrix); the two Focus* commands open their target
    // region first if it is closed, then focus it. All four are General, like every other
    // command-dispatched action routed through MainComponent::keyPressed's existing loop.
    focusNextRegion,
    focusPrevRegion,
    focusTimeline,
    focusLibrary,
    // T160: opens the Library (if closed) and grabs focus on its search field specifically, rather
    // than the region root focusLibrary lands on — see ModuleLibraryComponent::focusSearchField and
    // docs/shortcuts.md's Focus regions section for why those are two different destinations. Same
    // General/command-dispatched treatment as the other three Focus* actions above.
    focusLibrarySearch,
    // FRO45: selects Master (falling back to Audio Output when there is no Master yet) and pans it
    // into view — the canvas-only stopgap for founder feedback that auto-arrange or a drag can
    // leave either node anywhere (GraphEditor::locateMasterOrOutput). Appended here per the
    // snapSet comment above ("nothing persists a raw juce::CommandID"); filed under Graph in the
    // action table below, alongside autoArrange, since it means nothing off the canvas.
    locateMaster,
    // FRO125: transport verbs promoted to command-dispatched actions -- the prerequisite
    // docs/midi_remote.md §4.9 asks for, since a MIDI Remote action target invokes a
    // juce::CommandID. Filed under General (docs/shortcuts.md). Deliberately unbound by default
    // (see resetToDefaults()) -- these exist to be command/MIDI-Remote targets, not new default
    // keyboard shortcuts; togglePlayback's own Space binding is untouched. There is no
    // transportTogglePlayStop enumerator: that action id is a pure alias resolved by
    // getCommandForAction() below straight to togglePlayback, so the persisted "togglePlayback"
    // keybinding and its command id never move.
    transportPlay,
    transportStop,
    // Command-dispatches the SAME setLoop(...,!looping) verb "timelineToggleLoop" already performs
    // as a surface-resolved action -- the surface key keeps working unchanged (see getCommandForAction
    // below, which still answers kNoCommand for "timelineToggleLoop" itself).
    transportToggleLoop,
    // Routes through TimelineTransportBar's own record button, so it reaches the exact same
    // MainComponent armed-track gate onRecordToggled does -- see MainComponentCommandTable.cpp.
    transportRecord,
    // Routes through the transport bar's own metronome button, so its persisted
    // "timelineMetronomeEnabled" state stays authoritative.
    transportToggleMetronome,
    transportReturnToStart
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
    if (actionId == "toggleMixerPanel")
        return toggleMixerPanel;
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
    if (actionId == "locateMaster")
        return locateMaster;
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
    // Every SURFACE action lands here — see kNoCommand.
    return kNoCommand;
}
} // namespace AppCommands
