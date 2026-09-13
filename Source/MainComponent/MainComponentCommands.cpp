// MainComponentCommands.cpp — MainComponent's juce::ApplicationCommandTarget implementation:
// paint() plus the getAllCommands/getCommandInfo/perform trio (kept together — see the class's
// own comment on why they must not be split). MainComponent is declared in MainComponent.h; the
// rest of its implementation lives in the sibling MainComponent*.cpp units next to this one.
#include "MainComponent.h"

namespace {

// ---- Command <-> ShortcutManager action id, for the two blocks handled as case-fallthrough runs ----
//
// The ten grid commands and the four zoom commands share one getCommandInfo case each (their
// enablement rule is per BLOCK, not per command), so the block needs to recover which action id it
// is reporting for — that is what supplies the row's label and its default keypress. Written as a
// lookup rather than fourteen near-identical cases so a renamed id is one edit, and so a command
// that grows an id without a description shows up as the id itself in Settings rather than compiling
// to a blank row.
juce::String snapActionIdForCommand(juce::CommandID commandID) {
    switch (commandID) {
    case AppCommands::snapSetWhole:
        return "snapSetWhole";
    case AppCommands::snapSetHalf:
        return "snapSetHalf";
    case AppCommands::snapSetQuarter:
        return "snapSetQuarter";
    case AppCommands::snapSetEighth:
        return "snapSetEighth";
    case AppCommands::snapSetSixteenth:
        return "snapSetSixteenth";
    case AppCommands::snapSetThirtySecond:
        return "snapSetThirtySecond";
    case AppCommands::snapSetSixtyFourth:
        return "snapSetSixtyFourth";
    case AppCommands::snapSetHundredTwentyEighth:
        return "snapSetHundredTwentyEighth";
    case AppCommands::snapCyclePrev:
        return "snapCyclePrev";
    case AppCommands::snapCycleNext:
        return "snapCycleNext";
    default:
        return {};
    }
}
// The note-value name for a grid division — the SAME strings TimelinePanelComponent's snap combo
// shows ("Off", "Bar", "1", "1/2", …), so the status-bar report after a grid shortcut and the
// selector the user can see never disagree about what the grid is called.
juce::String snapDivisionLabel(synth::ui::TimelineViewState::Snap snap) {
    using Snap = synth::ui::TimelineViewState::Snap;
    switch (snap) {
    case Snap::Off:
        return "Off";
    case Snap::Bar:
        return "Bar";
    case Snap::Whole:
        return "1";
    case Snap::Half:
        return "1/2";
    case Snap::Quarter:
        return "1/4";
    case Snap::Eighth:
        return "1/8";
    case Snap::Sixteenth:
        return "1/16";
    case Snap::ThirtySecond:
        return "1/32";
    case Snap::SixtyFourth:
        return "1/64";
    case Snap::HundredTwentyEighth:
        return "1/128";
    }
    return "Off";
}
// GraphEditor's public zoom entry point (zoomAroundCentre) takes a WHEEL DELTA, because that is
// what its one zoom implementation was written against — it applies zoomLevel *= (1 + step * delta)
// with step == 0.1. The timeline surfaces take a multiplicative factor instead, so the zoom commands
// speak in factors and convert here, in ONE place, rather than each call site carrying a magic
// delta. Keep `kGraphZoomWheelStep` in step with GraphEditor::applyZoomAt if that formula changes:
// the consequence of drift is only that a canvas zoom step stops matching a timeline zoom step, but
// it is invisible until someone measures it.
constexpr double kGraphZoomWheelStep = 0.1;
float graphZoomWheelDeltaFor(double factor) { return (float)((factor - 1.0) / kGraphZoomWheelStep); }
juce::String zoomActionIdForCommand(juce::CommandID commandID) {
    switch (commandID) {
    case AppCommands::zoomInHorizontal:
        return "zoomInHorizontal";
    case AppCommands::zoomOutHorizontal:
        return "zoomOutHorizontal";
    case AppCommands::zoomInVertical:
        return "zoomInVertical";
    case AppCommands::zoomOutVertical:
        return "zoomOutVertical";
    default:
        return {};
    }
}

} // namespace

//==============================================================================
void MainComponent::paint(juce::Graphics& g) {
    // (Our component is opaque, so we must completely fill the background with a
    // solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    commands.addArray({AppCommands::openSettings, AppCommands::savePreset, AppCommands::saveProjectAs,
                       AppCommands::exportPatchOnly, AppCommands::exportAudio, AppCommands::exportStems,
                       AppCommands::openPreset, AppCommands::openProject, AppCommands::newPatch, AppCommands::undo,
                       AppCommands::redo, AppCommands::toggleModMatrix, AppCommands::toggleMinimap,
                       AppCommands::toggleAiPanel, AppCommands::autoArrange, AppCommands::groupSelection,
                       AppCommands::ungroupSelection, AppCommands::collapseMacro, AppCommands::locateMaster,
                       AppCommands::toggleLibrary, AppCommands::selectAllModules, AppCommands::saveSnippet,
                       AppCommands::copySelection, AppCommands::pasteSelection, AppCommands::duplicateSelection,
                       AppCommands::cutSelection,
                       // Registered unconditionally alongside togglePlayback below even though only
                       // the timeline surfaces implement it — reported inactive rather than dropping
                       // the row from Settings.
                       AppCommands::repeatSelection,
                       // Registered unconditionally (like every command above), reported inactive
                       // rather than dropping it from the Settings shortcut list entirely.
                       AppCommands::togglePlayback,
                       // The grid block and both zoom pairs follow the same rule: registered in
                       // every build configuration, reported inactive where there is nothing to act
                       // on (see getCommandInfo). The ten grid commands act on the timeline's
                       // shared snap value, so they are inactive whenever the panel is not on
                       // screen; the four zoom commands route per focused surface.
                       AppCommands::snapSetWhole, AppCommands::snapSetHalf, AppCommands::snapSetQuarter,
                       AppCommands::snapSetEighth, AppCommands::snapSetSixteenth, AppCommands::snapSetThirtySecond,
                       AppCommands::snapSetSixtyFourth, AppCommands::snapSetHundredTwentyEighth,
                       AppCommands::snapCyclePrev, AppCommands::snapCycleNext, AppCommands::zoomInHorizontal,
                       AppCommands::zoomOutHorizontal, AppCommands::zoomInVertical, AppCommands::zoomOutVertical});
    commands.add(AppCommands::toggleTimelinePanel);
    // T159: registered unconditionally, like every command above — getCommandInfo reports the two
    // Tab-cycle actions inactive while the welcome screen is up front rather than dropping them.
    commands.addArray({AppCommands::focusNextRegion, AppCommands::focusPrevRegion, AppCommands::focusTimeline,
                       AppCommands::focusLibrary, AppCommands::focusLibrarySearch});
    // T114/P8-10: unconditional (unlike checkForUpdates below) — neither command needs OS
    // integration, only ownedAudioEngine != nullptr, which getCommandInfo enforces via setActive()
    // and which is fixed for this MainComponent instance's whole lifetime.
    commands.add(AppCommands::showWelcomeScreen);
    commands.add(AppCommands::whatsNew);
#if JUCE_MAC || JUCE_WINDOWS
    commands.add(AppCommands::checkForUpdates);
#endif
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) {
    switch (commandID) {
    case AppCommands::openSettings: {
        result.setInfo("Open Settings", "Open the settings window", "General", 0);
        auto kp = shortcutManager.getBinding("openSettings");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::savePreset: {
        result.setInfo("Save Preset", "Save the current preset", "General", 0);
        auto kp = shortcutManager.getBinding("savePreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::saveProjectAs: {
        result.setInfo("Save Project As...", "Save the project to a new location", "General", 0);
        auto kp = shortcutManager.getBinding("saveProjectAs");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::exportPatchOnly: {
        result.setInfo("Export Patch Only (.json)...",
                       "Save just the patch, without the timeline, as a plain JSON preset", "General", 0);
        auto kp = shortcutManager.getBinding("exportPatchOnly");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::exportAudio: {
        result.setInfo("Export Audio...", "Bounce the arrangement or the current loop range to a WAV or AIFF file",
                       "General", 0);
        auto kp = shortcutManager.getBinding("exportAudio");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        // Greyed out rather than re-entrant: only one bounce (and one modal progress window) at a
        // time - see isBounceInProgress_.
        result.setActive(!isBounceInProgress_);
        break;
    }
    case AppCommands::exportStems: {
        result.setInfo("Export Stems...", "Render each mixer channel to its own audio file in a folder", "General", 0);
        // Menu-only, like openPreset/checkForUpdates - no ShortcutManager binding, so no
        // addDefaultKeypress call. Same isBounceInProgress_ gate as exportAudio - see its comment.
        result.setActive(!isBounceInProgress_);
        break;
    }
    case AppCommands::openProject: {
        // P8-31: the rebindable Cmd+O open is the WHOLE PROJECT (.agsproj bundle).
        result.setInfo("Open Project", "Open a project (.agsproj bundle: patch + timeline)", "General", 0);
        auto kp = shortcutManager.getBinding("openProject");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
        // P8-31: the Patch open is menu-only, like checkForUpdates - no rebindable action-id, so no
        // default keypress and no Settings row; it asks whether to replace or add onto the patch.
    case AppCommands::openPreset: {
        result.setInfo("Open Patch", "Open a patch (.json preset; replace or add onto the patch)", "General", 0);
        break;
    }
    case AppCommands::newPatch: {
        result.setInfo("New Patch", "Clear the canvas and start a new patch", "General", 0);
        auto kp = shortcutManager.getBinding("newPatch");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::undo: {
        result.setInfo("Undo", "Undo the last action", "Edit", 0);
        auto kp = shortcutManager.getBinding("undo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::redo: {
        result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
        auto kp = shortcutManager.getBinding("redo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleModMatrix: {
        result.setInfo("Toggle Mod Matrix", "Toggle the modulation matrix panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleModMatrix");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleMinimap: {
        result.setInfo("Toggle Minimap", "Toggle the graph editor minimap overlay", "View", 0);
        auto kp = shortcutManager.getBinding("toggleMinimap");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleAiPanel: {
        result.setInfo("Toggle AI Panel", "Toggle the AI chat panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleAiPanel");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::autoArrange: {
        result.setInfo("Auto Arrange", "Auto-arrange modules by signal flow", "View", 0);
        auto kp = shortcutManager.getBinding("autoArrange");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::locateMaster: {
        result.setInfo("Locate Master", "Select Master (or Audio Output) and pan it into view", "View", 0);
        auto kp = shortcutManager.getBinding("locateMaster");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        // Mirrors the canvas context menu item's setEnabled — same predicate, so the two
        // discoverable surfaces can never disagree about whether there's anything to find.
        result.setActive(graphEditor.hasLocatableMasterOrOutput());
        break;
    }
    case AppCommands::toggleLibrary: {
        result.setInfo("Toggle Module Library", "Toggle the module library sidebar", "View", 0);
        auto kp = shortcutManager.getBinding("toggleLibrary");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::selectAllModules: {
        // Still AppCommands::selectAllModules / actionId "selectAllModules" — the NAME is frozen so
        // a persisted user binding keeps resolving, but the verb now means "select everything in
        // the focused editor" and routes like Cmd+C/V/D below. Deliberately left always-active on
        // every surface: unlike the clipboard verbs it needs no pre-existing selection, and each
        // surface's own selectAll* returns false harmlessly when there is nothing to select.
        result.setInfo("Select All", "Select everything in the focused editor", "Edit", 0);
        auto kp = shortcutManager.getBinding("selectAllModules");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::saveSnippet: {
        result.setInfo("Save Selection as Snippet", "Save the selected modules as a reusable snippet", "Edit", 0);
        result.setActive(graphEditor.getSelectionCount() > 0);
        auto kp = shortcutManager.getBinding("saveSnippet");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::groupSelection: {
        // Static label — Cmd+G now dispatches to whichever verb applies (P8-14,
        // GraphEditor::groupOrToggleSelectionMacros), so the label can't claim to be only one of
        // them. Mirrors collapseMacro's "static label covers both directions" reasoning above.
        result.setInfo("Group / Toggle Macro",
                       "Group the selection into a new Macro, or toggle collapse/expand if it already touches one",
                       "Edit", 0);
        // Active whenever EITHER branch of the dispatch could do something: enough modules to
        // group, or the selection touches at least one macro to toggle (mirrors collapseMacro's
        // gate below).
        bool touchesAnyMacro = false;
        for (auto nodeId : graphEditor.getSelectedNodes()) {
            if (graphEditor.macroForNode(nodeId) != nullptr) {
                touchesAnyMacro = true;
                break;
            }
        }
        result.setActive(graphEditor.getSelectionCount() > 1 || touchesAnyMacro);
        auto kp = shortcutManager.getBinding("groupSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::ungroupSelection: {
        result.setInfo("Ungroup Macro", "Dissolve the macro the selection belongs to, keeping its modules", "Edit", 0);
        result.setActive(graphEditor.getSelectionCount() > 0);
        auto kp = shortcutManager.getBinding("ungroupSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::collapseMacro: {
        result.setInfo("Collapse / Expand Macro", "Toggle the collapsed state of the macro the selection belongs to",
                       "Edit", 0);
        // toggleSelectionMacrosCollapsed() itself refuses (with a status message) when the
        // selection touches no macro at all — this gate is the more precise "touches ANY macro"
        // check (not just "there's a selection"), since a plain non-macro selection can never
        // succeed here either.
        bool touchesAnyMacro = false;
        for (auto nodeId : graphEditor.getSelectedNodes()) {
            if (graphEditor.macroForNode(nodeId) != nullptr) {
                touchesAnyMacro = true;
                break;
            }
        }
        result.setActive(touchesAnyMacro);
        auto kp = shortcutManager.getBinding("collapseMacro");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::copySelection: {
        result.setInfo("Copy", "Copy the selection in the focused editor", "Edit", 0);
        // Routed by resolveEditSurface() — Graph's own behaviour (below) is unchanged; each
        // timeline surface gates on its own selection.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.getClipSelection().size() > 0);
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.getSelectionCount() > 0);
            break;
        }
        auto kp = shortcutManager.getBinding("copySelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::pasteSelection: {
        result.setInfo("Paste", "Paste into the focused editor", "Edit", 0);
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.canPasteClips());
            break;
        case EditSurface::PianoRoll:
            // canPasteNotes() is BOTH halves: a non-empty note clipboard AND an open clip. A roll
            // with nothing open has nowhere to put the block, so the row greys out rather than
            // silently discarding a paste.
            result.setActive(timelinePanel.getPianoRoll().canPasteNotes());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.canPaste());
            break;
        }
        auto kp = shortcutManager.getBinding("pasteSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::duplicateSelection: {
        result.setInfo("Duplicate", "Duplicate the selection in the focused editor", "Edit", 0);
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.getClipSelection().size() > 0);
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.getSelectionCount() > 0);
            break;
        }
        auto kp = shortcutManager.getBinding("duplicateSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::cutSelection: {
        result.setInfo("Cut", "Cut the selection in the focused editor", "Edit", 0);
        // Same enablement predicate Copy uses on every surface — a cut is a copy that also
        // deletes, so anything copyable is cuttable and the two rows can never disagree.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.canCutClips());
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(graphEditor.getSelectionCount() > 0);
            break;
        }
        auto kp = shortcutManager.getBinding("cutSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::repeatSelection: {
        result.setInfo("Repeat", "Repeat the selection a number of times", "Edit", 0);
        // The ONLY edit verb that is inactive on the Graph surface: "repeat N times, each copy one
        // block further along" is a time-axis idea, and a spatial canvas has no such axis — the
        // graph's answer to "another one of these" is Duplicate. See performRepeatSelection.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            result.setActive(timelinePanel.hasClipSelection());
            break;
        case EditSurface::PianoRoll:
            result.setActive(timelinePanel.getPianoRoll().hasNoteSelection());
            break;
        case EditSurface::Graph:
            result.setActive(false);
            break;
        }
        auto kp = shortcutManager.getBinding("repeatSelection");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::togglePlayback: {
        result.setInfo("Toggle Playback", "Play or stop the timeline transport", "Transport", 0);
        // Space is GLOBAL — no resolveEditSurface() branch, unlike C/V/D above.
        auto kp = shortcutManager.getBinding("togglePlayback");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    // ---- Grid division: eight absolute setters plus the two-step cycle ----
    // One block, one enablement rule: the grid is a property of the timeline's view state, so these
    // are active exactly when the panel is on screen. Not routed by resolveEditSurface() — the snap
    // value is SHARED by the clip lanes and the piano roll (one grid, whichever is in the lane
    // rect), so "which timeline surface has focus" is not a question these need to ask.
    case AppCommands::snapSetWhole:
    case AppCommands::snapSetHalf:
    case AppCommands::snapSetQuarter:
    case AppCommands::snapSetEighth:
    case AppCommands::snapSetSixteenth:
    case AppCommands::snapSetThirtySecond:
    case AppCommands::snapSetSixtyFourth:
    case AppCommands::snapSetHundredTwentyEighth:
    case AppCommands::snapCyclePrev:
    case AppCommands::snapCycleNext: {
        const auto actionId = snapActionIdForCommand(commandID);
        result.setInfo(ShortcutManager::getActionDescription(actionId), "Set the timeline's snap grid", "Timeline", 0);
        result.setActive(isTimelineVisible);
        auto kp = shortcutManager.getBinding(actionId);
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    // ---- Zoom: routed per focused surface, like the clipboard verbs ----
    case AppCommands::zoomInHorizontal:
    case AppCommands::zoomOutHorizontal:
    case AppCommands::zoomInVertical:
    case AppCommands::zoomOutVertical: {
        const auto actionId = zoomActionIdForCommand(commandID);
        const bool vertical = commandID == AppCommands::zoomInVertical || commandID == AppCommands::zoomOutVertical;
        result.setInfo(ShortcutManager::getActionDescription(actionId), "Zoom the focused editor", "View", 0);
        // The graph canvas zooms UNIFORMLY (GraphEditor::zoomAroundCentre — one zoomLevel, no
        // separate axes), so the horizontal pair drives it and the vertical pair is inactive there
        // rather than silently doing the same thing twice under a different key.
        switch (resolveEditSurface()) {
        case EditSurface::Graph:
            result.setActive(!vertical);
            break;
        case EditSurface::TimelineClips:
        case EditSurface::PianoRoll:
            result.setActive(isTimelineVisible);
            break;
        }
        auto kp = shortcutManager.getBinding(actionId);
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleTimelinePanel: {
        result.setInfo("Toggle Timeline Panel", "Toggle the bottom-docked timeline panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleTimelinePanel");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::focusNextRegion: {
        result.setInfo("Focus Next Region",
                       "Move keyboard focus to the next open panel (Library, Canvas, Timeline, AI Panel, Mod "
                       "Matrix)",
                       "General", 0);
        // T159: suppressed while the launch overlay is up front — every region it would cycle to is
        // sitting behind it, so there is nowhere useful for Tab to land.
        result.setActive(welcomeScreen_ == nullptr || !welcomeScreen_->isVisible());
        auto kp = shortcutManager.getBinding("focusNextRegion");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::focusPrevRegion: {
        result.setInfo("Focus Previous Region", "Move keyboard focus to the previous open panel", "General", 0);
        result.setActive(welcomeScreen_ == nullptr || !welcomeScreen_->isVisible());
        auto kp = shortcutManager.getBinding("focusPrevRegion");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::focusTimeline: {
        result.setInfo("Focus Timeline", "Open (if needed) and focus the Timeline panel", "General", 0);
        auto kp = shortcutManager.getBinding("focusTimeline");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::focusLibrary: {
        result.setInfo("Focus Library", "Open (if needed) and focus the Module Library", "General", 0);
        auto kp = shortcutManager.getBinding("focusLibrary");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::focusLibrarySearch: {
        result.setInfo("Focus Library Search", "Open (if needed) and focus the Module Library's search field",
                       "General", 0);
        auto kp = shortcutManager.getBinding("focusLibrarySearch");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::showWelcomeScreen: {
        result.setInfo("Show Welcome Screen", "Reopen the welcome screen", "Help", 0);
        // Registered unconditionally (see getAllCommands), but only ever meaningful on the app
        // path — ownedAudioEngine, and therefore welcomeScreen_, is fixed for this instance's whole
        // lifetime, so this check alone is enough; getAllCommands/perform need no matching gate.
        result.setActive(ownedAudioEngine != nullptr);
        break;
    }
    case AppCommands::whatsNew: {
        result.setInfo("What's New...", "See what's changed recently", "Help", 0);
        break;
    }
#if JUCE_MAC || JUCE_WINDOWS
    case AppCommands::checkForUpdates: {
        result.setInfo("Check for Updates...", "Check for a newer version of the app", "Help", 0);
        result.setActive(updateManager.isAvailable());
        break;
    }
#endif
    default:
        break;
    }
}

bool MainComponent::perform(const InvocationInfo& info) {
    switch (info.commandID) {
    case AppCommands::openSettings:
        if (settingsButton.onClick)
            settingsButton.onClick();
        return true;
    case AppCommands::savePreset:
        performSaveProject(false);
        return true;
    case AppCommands::saveProjectAs:
        performSaveProject(true);
        return true;
    case AppCommands::exportPatchOnly:
        promptExportPatchOnly();
        return true;
    case AppCommands::exportAudio:
        promptExportAudio();
        return true;
    case AppCommands::exportStems:
        promptExportStems();
        return true;
    case AppCommands::openPreset:
        openPresetFromFile();
        return true;
    case AppCommands::openProject:
        openProjectFromFile();
        return true;
    case AppCommands::newPatch:
        guardUnsavedChanges("New Patch", [this] { newPatch(); });
        return true;
    case AppCommands::undo:
        if (undoManager.canUndo())
            undoManager.undo();
        return true;
    case AppCommands::redo:
        if (undoManager.canRedo())
            undoManager.redo();
        return true;
    case AppCommands::toggleModMatrix:
        toggleModMatrixButton.triggerClick();
        return true;
    case AppCommands::toggleMinimap:
        toggleMinimapButton.triggerClick();
        return true;
    case AppCommands::toggleAiPanel:
        toggleAiPanelButton.triggerClick();
        return true;
    case AppCommands::autoArrange:
        graphEditor.autoArrange();
        return true;
    case AppCommands::locateMaster: {
        switch (graphEditor.locateMasterOrOutput()) {
        case GraphEditor::LocateMasterResult::Master:
            statusBar.showMessage("Located Master");
            break;
        case GraphEditor::LocateMasterResult::AudioOutput:
            statusBar.showMessage("Located Audio Output (no Master yet)");
            break;
        case GraphEditor::LocateMasterResult::NoTarget:
            statusBar.showMessage("No Master or Audio Output in this patch");
            break;
        }
        return true;
    }
    case AppCommands::groupSelection:
        graphEditor.groupOrToggleSelectionMacros();
        return true;
    case AppCommands::ungroupSelection:
        graphEditor.ungroupSelection();
        return true;
    case AppCommands::collapseMacro:
        graphEditor.toggleSelectionMacrosCollapsed();
        return true;
    case AppCommands::toggleLibrary:
        setLibraryVisible(!isLibraryVisible);
        return true;
    case AppCommands::selectAllModules: {
        // Routed by the same resolveEditSurface() the clipboard verbs use. The command id and
        // actionId keep their historical "…Modules" names (persisted bindings resolve by string),
        // but the behaviour is per-surface: Cmd+Shift+A means "everything in whatever I'm editing".
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.selectAllClips())
                statusBar.showMessage("Selected " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to select - the arrangement has no clips");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().selectAllNotes())
                statusBar.showMessage("Selected every note in the clip");
            else
                statusBar.showMessage("Nothing to select - the clip has no notes");
            return true;
        case EditSurface::Graph:
            break;
        }
        graphEditor.selectAllModules();
        statusBar.showMessage("Selected " + juce::String(graphEditor.getSelectionCount()) + " modules");
        return true;
    }
    case AppCommands::saveSnippet:
        promptSaveSnippet();
        return true;
    // Each of these reports what it did in the status bar rather than failing silently. The "did
    // nothing" branches are the residual cases only — getCommandInfo marks all three inactive when
    // there is nothing to act on, and ApplicationCommandTarget::tryToInvoke refuses an inactive
    // command outright, so the menu row greys out and the key never gets this far.
    case AppCommands::copySelection: {
        // Routed by the SAME resolveEditSurface() getCommandInfo just consulted — the
        // command manager already refused an inactive PianoRoll invocation (see
        // ApplicationCommandTarget::tryToInvoke), so the PianoRoll case below is belt-and-suspenders
        // for a caller that invokes perform() directly.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.copySelectedClips())
                statusBar.showMessage("Copied " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to copy - select one or more clips first");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().copySelectedNotes())
                statusBar.showMessage("Copied the selected notes");
            else
                statusBar.showMessage("Nothing to copy - select one or more notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        if (graphEditor.copySelection())
            statusBar.showMessage("Copied " + juce::String(graphEditor.getClipboardModuleCount()) + " modules");
        else
            statusBar.showMessage("Nothing to copy - select one or more modules first");
        return true;
    }
    case AppCommands::pasteSelection: {
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.pasteClipsAtPlayhead())
                statusBar.showMessage("Pasted " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to paste - copy some clips first");
            return true;
        case EditSurface::PianoRoll:
            // PRIMING, not a side effect: the roll anchors a paste on the last beat something
            // PUSHED into it via setPlayheadBeat, and a stopped transport never pushes one (the
            // playhead only animates while playing). Read the transport's live position here — the
            // same source pasteClipsAtPlayhead reads for the clip surface — so a paste with the
            // transport parked lands under the playhead the user can actually see, rather than at
            // whatever beat the last playback happened to stop pushing at.
            timelinePanel.getPianoRoll().setPlayheadBeat(audioEngine.getTransport().getPositionSnapshot().ppq);
            if (timelinePanel.getPianoRoll().pasteNotesAtPlayhead())
                statusBar.showMessage("Pasted notes at the playhead");
            else
                statusBar.showMessage("Nothing to paste - copy some notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        // Counted AFTER the fact: both leave the new copies selected, so the selection is the
        // authoritative count of what actually landed (ineligible nodes never make it in).
        if (graphEditor.pasteClipboard())
            statusBar.showMessage("Pasted " + juce::String(graphEditor.getSelectionCount()) + " modules");
        else
            statusBar.showMessage("Nothing to paste - copy a selection first");
        return true;
    }
    case AppCommands::duplicateSelection: {
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            if (timelinePanel.duplicateSelectedClips())
                statusBar.showMessage("Duplicated " + juce::String(timelinePanel.getClipSelection().size()) + " clips");
            else
                statusBar.showMessage("Nothing to duplicate - select one or more clips first");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().duplicateSelectedNotes())
                statusBar.showMessage("Duplicated the selected notes");
            else
                statusBar.showMessage("Nothing to duplicate - select one or more notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        if (graphEditor.duplicateSelection())
            statusBar.showMessage("Duplicated " + juce::String(graphEditor.getSelectionCount()) + " modules");
        else
            statusBar.showMessage("Nothing to duplicate - select one or more modules first");
        return true;
    }
    case AppCommands::cutSelection: {
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            // The panel's own verb: copy + delete inside ONE recordTimelineChange. Never wrap it —
            // a second transaction around it would make Cmd+Z a two-step undo for one gesture.
            if (timelinePanel.cutSelectedClips())
                statusBar.showMessage("Cut clips");
            else
                statusBar.showMessage("Nothing to cut - select one or more clips first");
            return true;
        case EditSurface::PianoRoll:
            if (timelinePanel.getPianoRoll().cutSelectedNotes())
                statusBar.showMessage("Cut notes");
            else
                statusBar.showMessage("Nothing to cut - select one or more notes first");
            return true;
        case EditSurface::Graph:
            break;
        }
        // The graph has no cut verb of its own, so it is composed here from the two that exist —
        // copySelection() fills the clipboard WITHOUT touching the graph or the undo stack, and
        // deleteSelection() then does the whole removal inside its own single
        // recordStructuralChange (the same transaction Delete and the canvas menu already use). So
        // a cut is exactly one graph-undo step, and the copy half survives the undo.
        const int cutCount = graphEditor.getSelectionCount();
        if (graphEditor.copySelection()) {
            graphEditor.deleteSelection();
            statusBar.showMessage("Cut " + juce::String(cutCount) + " modules");
        } else {
            statusBar.showMessage("Nothing to cut - select one or more modules first");
        }
        return true;
    }
    case AppCommands::repeatSelection:
        promptRepeatSelection();
        return true;
    case AppCommands::togglePlayback:
        // Reuses the transport bar's own play/stop choke point (reads the transport's CURRENT
        // playing state at click time) rather than re-deciding play-vs-stop here, so the bar's
        // button visual and a Space-bar toggle can never disagree — same triggerClick() idiom as
        // toggleModMatrix/toggleMinimap/toggleAiPanel above.
        timelinePanel.getTransportBar().getPlayStopButton().triggerClick();
        return true;
    // ---- Grid division ----
    // Every one of these goes through TimelinePanelComponent::setSnapValue / cycleSnapValue, which
    // is the panel's ONE writer for the shared snap value: the combo, these commands and the grid
    // cycle therefore share one persist and one set of repaints (see that method's comment). View
    // state only — nothing on the undo stack.
    case AppCommands::snapSetWhole:
    case AppCommands::snapSetHalf:
    case AppCommands::snapSetQuarter:
    case AppCommands::snapSetEighth:
    case AppCommands::snapSetSixteenth:
    case AppCommands::snapSetThirtySecond:
    case AppCommands::snapSetSixtyFourth:
    case AppCommands::snapSetHundredTwentyEighth: {
        using Snap = synth::ui::TimelineViewState::Snap;
        const Snap value = info.commandID == AppCommands::snapSetWhole          ? Snap::Whole
                           : info.commandID == AppCommands::snapSetHalf         ? Snap::Half
                           : info.commandID == AppCommands::snapSetQuarter      ? Snap::Quarter
                           : info.commandID == AppCommands::snapSetEighth       ? Snap::Eighth
                           : info.commandID == AppCommands::snapSetSixteenth    ? Snap::Sixteenth
                           : info.commandID == AppCommands::snapSetThirtySecond ? Snap::ThirtySecond
                           : info.commandID == AppCommands::snapSetSixtyFourth  ? Snap::SixtyFourth
                                                                                : Snap::HundredTwentyEighth;
        timelinePanel.setSnapValue(value);
        statusBar.showMessage("Grid: " + snapDivisionLabel(value));
        return true;
    }
    case AppCommands::snapCyclePrev:
    case AppCommands::snapCycleNext: {
        const int direction = info.commandID == AppCommands::snapCycleNext ? 1 : -1;
        timelinePanel.cycleSnapValue(direction);
        // Reports where the grid ENDED UP, not which way it was asked to move: the cycle clamps at
        // both ends, so a held key parked on 1/16 says so instead of implying it moved again.
        statusBar.showMessage("Grid: " + snapDivisionLabel(timelinePanel.getViewState().snap));
        return true;
    }
    // ---- Zoom ----
    case AppCommands::zoomInHorizontal:
    case AppCommands::zoomOutHorizontal:
    case AppCommands::zoomInVertical:
    case AppCommands::zoomOutVertical: {
        const bool zoomIn =
            info.commandID == AppCommands::zoomInHorizontal || info.commandID == AppCommands::zoomInVertical;
        const bool vertical =
            info.commandID == AppCommands::zoomInVertical || info.commandID == AppCommands::zoomOutVertical;
        const double factor = zoomIn ? kZoomInFactor : kZoomOutFactor;

        switch (resolveEditSurface()) {
        case EditSurface::PianoRoll:
            if (vertical)
                timelinePanel.getPianoRoll().zoomVertical(factor);
            else
                timelinePanel.getPianoRoll().zoomHorizontal(factor);
            statusBar.showMessage(vertical ? "Piano roll: vertical zoom" : "Piano roll: zoom");
            return true;
        case EditSurface::TimelineClips:
            if (vertical)
                timelinePanel.zoomTimelineVertical(factor);
            else
                timelinePanel.zoomTimelineHorizontal(factor);
            statusBar.showMessage(vertical ? "Timeline: track height" : "Timeline: zoom");
            return true;
        case EditSurface::Graph:
            break;
        }

        // The canvas has ONE zoom level (see getCommandInfo), so only the horizontal pair reaches
        // it. GraphEditor's public zoom takes a wheel DELTA rather than a factor — see
        // graphZoomWheelDeltaFor() for the conversion and why it lives in one place.
        if (vertical)
            return true; // reported inactive on Graph; belt-and-suspenders for a direct perform()
        graphEditor.zoomAroundCentre(graphZoomWheelDeltaFor(factor));
        statusBar.showMessage("Canvas: zoom");
        return true;
    }
    case AppCommands::toggleTimelinePanel:
        toggleTimelineButton.triggerClick();
        return true;
    case AppCommands::focusNextRegion:
        focusRegions_.cycleFocus(true);
        return true;
    case AppCommands::focusPrevRegion:
        focusRegions_.cycleFocus(false);
        return true;
    case AppCommands::focusTimeline:
        focusRegions_.focusRegionById("timeline");
        return true;
    case AppCommands::focusLibrary:
        focusRegions_.focusRegionById("library");
        return true;
    case AppCommands::focusLibrarySearch:
        // Deliberately NOT focusRegions_.focusRegionById("library") — that grabs the region ROOT
        // (moduleLibrary itself, per FocusRegion.h's contract), and this shortcut's whole point is to
        // land on the search field specifically. Same "open if closed" behaviour as focusLibrary.
        if (!isLibraryVisible)
            setLibraryVisible(true);
        moduleLibrary.focusSearchField();
        return true;
    case AppCommands::showWelcomeScreen:
        showWelcomeScreen();
        return true;
    case AppCommands::whatsNew:
        showWhatsNewDialog();
        return true;
#if JUCE_MAC || JUCE_WINDOWS
    case AppCommands::checkForUpdates:
        updateManager.checkForUpdates();
        return true;
#endif
    default:
        return false;
    }
}
