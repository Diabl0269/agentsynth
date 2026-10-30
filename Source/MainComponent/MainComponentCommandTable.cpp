// MainComponentCommandTable.cpp -- the CommandSpec table getAllCommands/getCommandInfo/perform
// (MainComponentCommands.cpp) look up into, plus the named perform() bodies and isActive
// predicates the table's rows call into. One ordered table replaces per-command switches -- the row
// order below IS getAllCommands()'s order (menu order).
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/SnapNoteValueActions.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/RenameKey.h"
#include "UI/Timeline/TimelineSnapNames.h"
#include <algorithm>
#include <iterator>

namespace {
// Option+R types a symbol in a text field, so a focused editor keeps the key.
bool renameUnlessTyping(CanvasCardKeyboard& cards) {
    return !synth::ui::isTextEditorFocused() && cards.renameSelectedModule();
}

// GraphEditor's public zoom entry point (zoomAroundCentre) takes a WHEEL DELTA, because that is
// what its one zoom implementation was written against -- it applies zoomLevel *= (1 + step * delta)
// with step == 0.1. The timeline surfaces take a multiplicative factor instead, so the zoom commands
// speak in factors and convert here, in ONE place, rather than each call site carrying a magic
// delta. Keep `kGraphZoomWheelStep` in step with GraphEditor::applyZoomAt if that formula changes:
// the consequence of drift is only that a canvas zoom step stops matching a timeline zoom step, but
// it is invisible until someone measures it.
constexpr double kGraphZoomWheelStep = 0.1;
float graphZoomWheelDeltaFor(double factor) { return (float)((factor - 1.0) / kGraphZoomWheelStep); }

} // namespace

// ---- Named perform() bodies (moved verbatim from the former MainComponent::perform switch) ----

bool MainComponent::performLocateMaster() {
    ensureCanvasShowing();
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

bool MainComponent::performSelectAllModules() {
    // Routed by the same resolveEditSurface() the clipboard verbs use. The command id and
    // actionId keep their historical "...Modules" names (persisted bindings resolve by string),
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
    case EditSurface::AutomationLane: {
        auto* editor = activeLaneEditor();
        if (editor != nullptr && editor->selectAllPoints())
            statusBar.showMessage("Selected " + juce::String(editor->getPointSelection().size()) + " points");
        else
            statusBar.showMessage("Nothing to select - the lane has no points");
        return true;
    }
    case EditSurface::Mixer:
        // No "select all" meaning on the mixer -- no-op, rather than falling through to Graph below.
        statusBar.showMessage("Nothing to select - Select All has no effect on the mixer");
        return true;
    case EditSurface::Graph:
        break;
    }
    graphEditor.selectAllModules();
    statusBar.showMessage("Selected " + juce::String(graphEditor.getSelectionCount()) + " modules");
    return true;
}

// Each of these reports what it did in the status bar rather than failing silently. The "did
// nothing" branches are the residual cases only -- getCommandInfo (via isEditSurfaceCommandActive)
// marks all three inactive when there is nothing to act on, and ApplicationCommandTarget::
// tryToInvoke refuses an inactive command outright, so the menu row greys out and the key never
// gets this far.
bool MainComponent::performCopySelection() {
    // Routed by the SAME resolveEditSurface() getCommandInfo just consulted -- the
    // command manager already refused an inactive PianoRoll invocation (see
    // ApplicationCommandTarget::tryToInvoke), so the PianoRoll case below is belt-and-suspenders
    // for a caller that invokes perform() directly.
    switch (resolveEditSurface()) {
    case EditSurface::TimelineClips:
        if (timelinePanel.hasRangeSelection()) { // the Range tool's span wins over the selection
            statusBar.showMessage(timelinePanel.copyRange() ? "Copied the range" : "Nothing to copy in the range");
            return true;
        }
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
    case EditSurface::AutomationLane: {
        auto* editor = activeLaneEditor();
        if (editor != nullptr && editor->copySelectedPoints())
            statusBar.showMessage("Copied the selected points");
        else
            statusBar.showMessage("Nothing to copy - select one or more points first");
        return true;
    }
    case EditSurface::Mixer:
        return true; // isEditSurfaceCommandActive() reports Mixer inactive -- belt-and-suspenders
    case EditSurface::Graph:
        break;
    }
    if (graphEditor.copySelection())
        statusBar.showMessage("Copied " + juce::String(graphEditor.getClipboardModuleCount()) + " modules");
    else
        statusBar.showMessage("Nothing to copy - select one or more modules first");
    return true;
}

bool MainComponent::performPasteSelection() {
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
        // playhead only animates while playing). Read the transport's live position here -- the
        // same source pasteClipsAtPlayhead reads for the clip surface -- so a paste with the
        // transport parked lands under the playhead the user can actually see, rather than at
        // whatever beat the last playback happened to stop pushing at.
        timelinePanel.getPianoRoll().setPlayheadBeat(audioEngine.getTransport().getPositionSnapshot().ppq);
        if (timelinePanel.getPianoRoll().pasteNotesAtPlayhead())
            statusBar.showMessage("Pasted notes at the playhead");
        else
            statusBar.showMessage("Nothing to paste - copy some notes first");
        return true;
    case EditSurface::AutomationLane: {
        auto* editor = activeLaneEditor();
        if (editor != nullptr && editor->pasteAtPlayhead())
            statusBar.showMessage("Pasted points at the playhead");
        else
            statusBar.showMessage("Nothing to paste - copy some points first");
        return true;
    }
    case EditSurface::Mixer:
        return true; // see performCopySelection's Mixer case
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

bool MainComponent::performDuplicateSelection() {
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
    case EditSurface::AutomationLane:
        if (auto* editor = activeLaneEditor(); editor != nullptr && editor->duplicateSelectedPoints()) {
            statusBar.showMessage("Duplicated the selected points");
            return true;
        }
        // The lane is copied below itself once a parameter is picked for the copy (Esc leaves nothing behind).
        if (!timelinePanel.getAutomationLanes().requestDuplicateLane(activeLane()))
            statusBar.showMessage("Nothing to duplicate - focus an automation lane first");
        return true;
    case EditSurface::Mixer:
        return true; // see performCopySelection's Mixer case
    case EditSurface::Graph:
        break;
    }
    if (graphEditor.duplicateSelection())
        statusBar.showMessage("Duplicated " + juce::String(graphEditor.getSelectionCount()) + " modules");
    else
        statusBar.showMessage("Nothing to duplicate - select one or more modules first");
    return true;
}

bool MainComponent::performCutSelection() {
    switch (resolveEditSurface()) {
    case EditSurface::TimelineClips:
        // The panel's own verb: copy + delete inside ONE recordTimelineChange. Never wrap it --
        // a second transaction around it would make Cmd+Z a two-step undo for one gesture.
        if (timelinePanel.hasRangeSelection()) {
            statusBar.showMessage(timelinePanel.cutRange() ? "Cut the range" : "Nothing to cut in the range");
            return true;
        }
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
    case EditSurface::AutomationLane: {
        auto* editor = activeLaneEditor();
        if (editor != nullptr && editor->cutSelectedPoints())
            statusBar.showMessage("Cut points");
        else
            statusBar.showMessage("Nothing to cut - select one or more points first");
        return true;
    }
    case EditSurface::Mixer:
        return true; // see performCopySelection's Mixer case
    case EditSurface::Graph:
        break;
    }
    // The graph has no cut verb of its own, so it is composed here from the two that exist --
    // copySelection() fills the clipboard WITHOUT touching the graph or the undo stack, and
    // requestDeleteSelection() (a whole macro asks about its track first) then does the whole removal inside its own
    // single recordStructuralChange (the same transaction Delete and the canvas menu already use). So a cut is exactly
    // one graph-undo step, and the copy half survives the undo.
    const int cutCount = graphEditor.getSelectionCount();
    if (graphEditor.copySelection()) {
        graphEditor.getMacroController().requestDeleteSelection();
        statusBar.showMessage("Cut " + juce::String(cutCount) + " modules");
    } else {
        statusBar.showMessage("Nothing to cut - select one or more modules first");
    }
    return true;
}

// ---- Grid division: eight absolute setters plus the two-step cycle ----
// Every one of these goes through TimelinePanelComponent::setSnapValue / cycleSnapValue, which
// is the panel's ONE writer for the shared snap value: the combo, these commands and the grid
// cycle therefore share one persist and one set of repaints (see that method's comment). View
// state only -- nothing on the undo stack.
bool MainComponent::applySnapCommand(juce::CommandID commandID) {
    if (commandID == AppCommands::snapCyclePrev || commandID == AppCommands::snapCycleNext) {
        const int direction = commandID == AppCommands::snapCycleNext ? 1 : -1;
        timelinePanel.cycleSnapValue(direction);
        // Reports where the grid ENDED UP, not which way it was asked to move: the cycle clamps at
        // both ends, so a held key parked on 1/16 says so instead of implying it moved again.
        statusBar.showMessage("Grid: " + synth::ui::snapValueName(timelinePanel.getViewState().snap));
        return true;
    }

    using Snap = synth::ui::TimelineViewState::Snap;
    static const std::pair<juce::CommandID, Snap> kSetCommands[] = {
        {AppCommands::snapSetWhole, Snap::Whole},
        {AppCommands::snapSetHalf, Snap::Half},
        {AppCommands::snapSetQuarter, Snap::Quarter},
        {AppCommands::snapSetEighth, Snap::Eighth},
        {AppCommands::snapSetSixteenth, Snap::Sixteenth},
        {AppCommands::snapSetThirtySecond, Snap::ThirtySecond},
        {AppCommands::snapSetSixtyFourth, Snap::SixtyFourth},
        {AppCommands::snapSetHundredTwentyEighth, Snap::HundredTwentyEighth},
    };
    Snap value = Snap::HundredTwentyEighth;
    const auto& noteValueActions = snapNoteValueActions();
    for (size_t i = 0; i < noteValueActions.size(); ++i)
        if (noteValueActions[i].command == commandID)
            value = synth::ui::TimelineViewState::noteValue(i < 8 ? synth::ui::TimelineViewState::SnapKind::triplet
                                                                  : synth::ui::TimelineViewState::SnapKind::dotted,
                                                            1 << (i % 8));
    for (const auto& [id, snap] : kSetCommands)
        if (id == commandID)
            value = snap;
    timelinePanel.setSnapValue(value);
    statusBar.showMessage("Grid: " + synth::ui::snapValueName(value));
    return true;
}

// ---- Zoom: routed per focused surface, like the clipboard verbs ----
bool MainComponent::applyZoomCommand(juce::CommandID commandID) {
    const bool zoomIn = commandID == AppCommands::zoomInHorizontal || commandID == AppCommands::zoomInVertical;
    const bool vertical = commandID == AppCommands::zoomInVertical || commandID == AppCommands::zoomOutVertical;
    const double factor = zoomIn ? kZoomInFactor : kZoomOutFactor;

    switch (resolveEditSurface()) {
    case EditSurface::PianoRoll:
        if (timelinePanel.getDrumMachine().isOpen()) {
            timelinePanel.getDrumMachine().zoomHorizontal(vertical ? 1.0 : factor);
            statusBar.showMessage("Drum machine: zoom");
            return true;
        }
        if (vertical)
            timelinePanel.getPianoRoll().zoomVertical(factor);
        else
            timelinePanel.getPianoRoll().zoomHorizontal(factor);
        statusBar.showMessage(vertical ? "Piano roll: vertical zoom" : "Piano roll: zoom");
        return true;
    case EditSurface::AutomationLane: // the lane rows scale with the timeline
    case EditSurface::TimelineClips:
        if (vertical)
            timelinePanel.zoomTimelineVertical(factor);
        else
            timelinePanel.zoomTimelineHorizontal(factor);
        statusBar.showMessage(vertical ? "Timeline: track height" : "Timeline: zoom");
        return true;
    case EditSurface::Mixer:
        return true; // isZoomCommandActive() reports Mixer inactive on both axes
    case EditSurface::Graph:
        break;
    }

    // The canvas has ONE zoom level (see isZoomCommandActive), so only the horizontal pair reaches
    // it. GraphEditor's public zoom takes a wheel DELTA rather than a factor -- see
    // graphZoomWheelDeltaFor() for the conversion and why it lives in one place.
    if (vertical)
        return true; // reported inactive on Graph; belt-and-suspenders for a direct perform()
    graphEditor.zoomAroundCentre(graphZoomWheelDeltaFor(factor));
    statusBar.showMessage("Canvas: zoom");
    return true;
}

// ---- Named isActive predicates shared by more than one row (touchesAnyMacro,
// isEditSurfaceCommandActive, isZoomCommandActive) moved to MainComponentCommandPredicates.cpp
// to keep this file under the 1,000-line cap.

// ---- The table itself, split into category-grouped builder functions purely to keep
// commandTable() itself under the function-size cap -- category boundaries are the
// same ones the row comments below already used. Row order across the four still IS
// getAllCommands()'s order (the menu order contract), built once and cached the same way a
// static local in a member function normally is: the vector's contents don't depend on `this`,
// only its lambdas capture nothing but a per-row literal id, so one shared table safely backs
// every MainComponent instance. Groups: general/file, edit/graph toggles, timeline/zoom/panels,
// then focus/help.

std::vector<MainComponent::CommandSpec> MainComponent::buildGeneralCommandRows() {
    return {
        {AppCommands::openSettings,
         "Open Settings",
         "Open the settings window",
         "General",
         "openSettings",
         {},
         [](MainComponent& m) {
             if (m.settingsButton.onClick)
                 m.settingsButton.onClick();
             return true;
         }},
        {AppCommands::savePreset,
         "Save Project",
         "Save the project (graph and timeline)",
         "General",
         "savePreset",
         {},
         [](MainComponent& m) {
             m.performSaveProject(false);
             return true;
         }},
        {AppCommands::saveProjectAs,
         "Save Project As...",
         "Save the project to a new .agsproj bundle",
         "General",
         "saveProjectAs",
         {},
         [](MainComponent& m) {
             m.performSaveProject(true);
             return true;
         }},
        {AppCommands::exportPatchOnly,
         "Export Patch Only (.json)...",
         "Save just the patch, without the timeline, as a plain JSON preset",
         "General",
         "exportPatchOnly",
         {},
         [](MainComponent& m) {
             m.promptExportPatchOnly();
             return true;
         }},
        // Greyed out rather than re-entrant: only one bounce (and one modal progress window) at a
        // time -- see isBounceInProgress_.
        {AppCommands::exportAudio, "Export Audio...",
         "Bounce the arrangement or the current loop range to a WAV, AIFF, FLAC or MP3 file", "General", "exportAudio",
         [](const MainComponent& m) { return m.isExportAvailable(); },
         [](MainComponent& m) {
             m.promptExportAudio();
             return true;
         }},
        // Menu-only, like openPreset/checkForUpdates -- no ShortcutManager binding. Same
        // isBounceInProgress_ gate as exportAudio -- see its comment.
        {AppCommands::exportStems, "Export Stems...", "Render each mixer channel to its own audio file in a folder",
         "General", nullptr, [](const MainComponent& m) { return m.isExportAvailable(); },
         [](MainComponent& m) {
             m.promptExportStems();
             return true;
         }},
        // Menu-only and never gated: a MIDI export reads the document, not the offline render path.
        {AppCommands::exportMidi,
         "Export MIDI...",
         "Save the arrangement or the current loop range as a Standard MIDI File (.mid)",
         "General",
         nullptr,
         {},
         [](MainComponent& m) {
             m.promptExportMidi();
             return true;
         }},
        // Menu-only; greyed out while a collect/zip or a render is running.
        {AppCommands::collectAndArchive, "Collect & Archive...",
         "Copy every sample and wavetable the project uses into it, optionally as one zip file", "General", nullptr,
         [](const MainComponent& m) { return m.isCollectArchiveAvailable(); },
         [](MainComponent& m) {
             m.promptCollectAndArchive();
             return true;
         }},
        // The Patch open is menu-only, like checkForUpdates -- no rebindable action-id, so no
        // default keypress and no Settings row; it asks whether to replace or add onto the patch.
        {AppCommands::openPreset,
         "Open Patch",
         "Open a patch (.json preset; replace or add onto the patch)",
         "General",
         nullptr,
         {},
         [](MainComponent& m) {
             m.openPresetFromFile();
             return true;
         }},
        // The rebindable Cmd+O open is the WHOLE PROJECT (.agsproj bundle).
        {AppCommands::openProject,
         "Open Project",
         "Open a project (.agsproj bundle: patch + timeline)",
         "General",
         "openProject",
         {},
         [](MainComponent& m) {
             m.openProjectFromFile();
             return true;
         }},
        {AppCommands::newPatch,
         "New Patch",
         "Clear the canvas and start a new patch",
         "General",
         "newPatch",
         {},
         [](MainComponent& m) {
             m.guardUnsavedChanges("New Patch", [&m] { m.newPatch(); });
             return true;
         }},
    };
}

// Undo/Redo (Cmd+Z / Cmd+Shift+Z and the Edit menu) act on the controller edit history while
// keyboard focus is inside the MIDI Remote panel (docked or detached -- the same panel object
// either way), and on the project's AppUndoManager everywhere else. The toolbar's Undo/Redo buttons
// do not come through here and always mean the project. A focused panel with nothing left to undo
// does nothing rather than falling through to the project: the user is looking at the panel, and
// silently undoing a canvas edit instead would be a surprise
// (see docs/control/midi-remote.md#undo).
std::vector<MainComponent::CommandSpec> MainComponent::buildEditAndGraphCommandRows() {
    return {
        {AppCommands::undo,
         "Undo",
         "Undo the last action",
         "Edit",
         "undo",
         {},
         [](MainComponent& m) {
             if (m.bottomDock.getMidiRemotePanel().holdsUndoFocus())
                 m.midiLearnController_.undoProfileEdit();
             else if (m.undoManager.canUndo())
                 m.undoManager.undo();
             return true;
         }},
        {AppCommands::redo,
         "Redo",
         "Redo the last undone action",
         "Edit",
         "redo",
         {},
         [](MainComponent& m) {
             if (m.bottomDock.getMidiRemotePanel().holdsUndoFocus())
                 m.midiLearnController_.redoProfileEdit();
             else if (m.undoManager.canRedo())
                 m.undoManager.redo();
             return true;
         }},
        {AppCommands::toggleModMatrix,
         "Toggle Mod Matrix",
         "Toggle the modulation matrix panel",
         "View",
         "toggleModMatrix",
         {},
         [](MainComponent& m) {
             m.ensureCanvasShowing();
             m.toggleModMatrixButton.triggerClick();
             return true;
         }},
        {AppCommands::toggleMinimap,
         "Toggle Minimap",
         "Toggle the graph editor minimap overlay",
         "View",
         "toggleMinimap",
         {},
         [](MainComponent& m) {
             m.ensureCanvasShowing();
             m.toggleMinimapButton.triggerClick();
             return true;
         }},
        {AppCommands::toggleAiPanel,
         "Toggle AI Panel",
         "Toggle the AI chat panel",
         "View",
         "toggleAiPanel",
         {},
         [](MainComponent& m) {
             m.toggleAiPanelButton.triggerClick();
             return true;
         }},
        {AppCommands::autoArrange,
         "Auto Arrange",
         "Auto-arrange modules by signal flow",
         "View",
         "autoArrange",
         {},
         [](MainComponent& m) {
             m.ensureCanvasShowing();
             m.graphEditor.autoArrange();
             return true;
         }},
        // Static label -- Cmd+G now dispatches to whichever verb applies
        // (GraphEditor::groupOrToggleSelectionMacros), so the label can't claim to be only one of
        // them. Mirrors collapseMacro's "static label covers both directions" reasoning below.
        // Active whenever EITHER branch of the dispatch could do something: enough modules to
        // group, or the selection touches at least one macro to toggle.
        {AppCommands::groupSelection, "Group / Toggle Macro",
         "Group the selection into a new Macro, or toggle collapse/expand if it already touches one", "Edit",
         "groupSelection", [](const MainComponent& m) { return m.canGroupSelection(); },
         [](MainComponent& m) {
             m.graphEditor.getMacroController().groupOrToggleSelectionMacros();
             return true;
         }},
        {AppCommands::ungroupSelection, "Ungroup Macro",
         "Dissolve the macro the selection belongs to, keeping its modules", "Edit", "ungroupSelection",
         [](const MainComponent& m) { return m.hasSelection(); },
         [](MainComponent& m) {
             m.graphEditor.getMacroController().ungroupSelection();
             return true;
         }},
        // toggleSelectionMacrosCollapsed() itself refuses (with a status message) when the
        // selection touches no macro at all -- touchesAnyMacro() is the more precise "touches ANY
        // macro" check (not just "there's a selection"), since a plain non-macro selection can
        // never succeed here either.
        {AppCommands::collapseMacro, "Collapse / Expand Macro",
         "Toggle the collapsed state of the macro the selection belongs to", "Edit", "collapseMacro",
         [](const MainComponent& m) { return m.touchesAnyMacro(); },
         [](MainComponent& m) {
             m.graphEditor.getMacroController().toggleSelectionMacrosCollapsed();
             return true;
         }},
        // The same "touches ANY macro" gate as collapseMacro: the verb folds every touched macro (or expands them when
        // all are folded) and, with the "Collapsing also packs macros together" preference, tidies their cards.
        {AppCommands::foldAndPackMacros, "Fold and Pack Macros",
         "Fold every selected macro and tidy their cards into a grid (default Cmd+Ctrl+G)", "Edit", "foldAndPackMacros",
         [](const MainComponent& m) { return m.touchesAnyMacro(); },
         [](MainComponent& m) {
             m.graphEditor.getMacroController().foldAndPackSelectionMacros();
             return true;
         }},
        // Mirrors the canvas context menu item's setEnabled -- same predicate, so the two
        // discoverable surfaces can never disagree about whether there's anything to find.
        {AppCommands::locateMaster, "Go to Output", "Select Master (or Audio Output) and frame the whole output dock",
         "View", "locateMaster", [](const MainComponent& m) { return m.graphEditor.hasLocatableMasterOrOutput(); },
         [](MainComponent& m) { return m.performLocateMaster(); }},
        {AppCommands::renameSelectedModule, "Rename", "Rename the focused name, else the selected module card", "Edit",
         "renameSelectedModule",
         [](const MainComponent& m) { return m.graphEditor.getCardKeyboard().canRenameSelectedModule(); },
         [](MainComponent& m) { return renameUnlessTyping(m.graphEditor.getCardKeyboard()); }},
        {AppCommands::replaceSelectedModule, "Replace Module With...",
         "Swap the selected module for another type or hosted plugin", "Edit", "replaceSelectedModule",
         [](const MainComponent& m) { return m.canReplaceModuleFromShortcut(); },
         [](MainComponent& m) { return m.replaceModuleFromShortcut(); }},
        {AppCommands::toggleLibrary,
         "Toggle Module Library",
         "Show the Modules view of the sidebar, or hide the sidebar when it already shows",
         "View",
         "toggleLibrary",
         {},
         [](MainComponent& m) {
             m.toggleLibraryView(synth::ui::SidebarView::Modules);
             return true;
         }},
        {AppCommands::toggleSoundBrowser,
         "Toggle Sound Browser",
         "Show the Sounds view of the sidebar, or hide the sidebar when it already shows",
         "View",
         "toggleSoundBrowser",
         {},
         [](MainComponent& m) {
             m.toggleLibraryView(synth::ui::SidebarView::Sounds);
             return true;
         }},
        // Still AppCommands::selectAllModules / actionId "selectAllModules" -- the NAME is frozen so
        // a persisted user binding keeps resolving, but the verb now means "select everything in
        // the focused editor" and routes like Cmd+C/V/D. Deliberately left always-active on every
        // surface: unlike the clipboard verbs it needs no pre-existing selection.
        {AppCommands::selectAllModules,
         "Select All",
         "Select everything in the focused editor",
         "Edit",
         "selectAllModules",
         {},
         [](MainComponent& m) { return m.performSelectAllModules(); }},
        {AppCommands::saveSnippet, "Save Selection as Snippet", "Save the selected modules as a reusable snippet",
         "Edit", "saveSnippet", [](const MainComponent& m) { return m.hasSelection(); },
         [](MainComponent& m) {
             m.promptSaveSnippet();
             return true;
         }},
        {AppCommands::copySelection, "Copy", "Copy the selection in the focused editor", "Edit", "copySelection",
         [](const MainComponent& m) { return m.isEditSurfaceCommandActive(AppCommands::copySelection); },
         [](MainComponent& m) { return m.performCopySelection(); }},
        {AppCommands::pasteSelection, "Paste", "Paste into the focused editor", "Edit", "pasteSelection",
         [](const MainComponent& m) { return m.isEditSurfaceCommandActive(AppCommands::pasteSelection); },
         [](MainComponent& m) { return m.performPasteSelection(); }},
        {AppCommands::duplicateSelection, "Duplicate", "Duplicate the selection in the focused editor", "Edit",
         "duplicateSelection",
         [](const MainComponent& m) { return m.isEditSurfaceCommandActive(AppCommands::duplicateSelection); },
         [](MainComponent& m) { return m.performDuplicateSelection(); }},
        {AppCommands::cutSelection, "Cut", "Cut the selection in the focused editor", "Edit", "cutSelection",
         [](const MainComponent& m) { return m.isEditSurfaceCommandActive(AppCommands::cutSelection); },
         [](MainComponent& m) { return m.performCutSelection(); }},
        // Registered unconditionally alongside togglePlayback below even though only the timeline
        // surfaces implement it -- reported inactive rather than dropping the row from Settings.
        {AppCommands::repeatSelection, "Repeat", "Repeat the selection a number of times", "Edit", "repeatSelection",
         [](const MainComponent& m) { return m.isEditSurfaceCommandActive(AppCommands::repeatSelection); },
         [](MainComponent& m) {
             m.promptRepeatSelection();
             return true;
         }},
        // Registered unconditionally (like every command above), reported inactive rather than
        // dropping it from the Settings shortcut list entirely. Space is GLOBAL -- no
        // resolveEditSurface() branch, unlike C/V/D above.
        {AppCommands::togglePlayback,
         "Play / Stop",
         "Play or stop the timeline transport",
         "Transport",
         "togglePlayback",
         {},
         [](MainComponent& m) {
             // Reuses the transport bar's own play/stop choke point (reads the transport's CURRENT
             // playing state at click time) rather than re-deciding play-vs-stop here, so the bar's
             // button visual and a Space-bar toggle can never disagree -- same triggerClick() idiom
             // as toggleModMatrix/toggleMinimap/toggleAiPanel above.
             m.timelinePanel.getTransportBar().getPlayStopButton().triggerClick();
             return true;
         }},
    };
}

std::vector<MainComponent::CommandSpec> MainComponent::buildTimelineAndPanelCommandRows() {
    std::vector<CommandSpec> rows{
        // ---- Grid division: registered in every build, inactive whenever the panel is off screen ----
        {AppCommands::snapSetWhole, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetWhole",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetWhole); }},
        {AppCommands::snapSetHalf, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetHalf",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetHalf); }},
        {AppCommands::snapSetQuarter, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetQuarter",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetQuarter); }},
        {AppCommands::snapSetEighth, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetEighth",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetEighth); }},
        {AppCommands::snapSetSixteenth, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetSixteenth",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetSixteenth); }},
        {AppCommands::snapSetThirtySecond, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetThirtySecond",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetThirtySecond); }},
        {AppCommands::snapSetSixtyFourth, nullptr, "Set the timeline's snap grid", "Timeline", "snapSetSixtyFourth",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetSixtyFourth); }},
        {AppCommands::snapSetHundredTwentyEighth, nullptr, "Set the timeline's snap grid", "Timeline",
         "snapSetHundredTwentyEighth", [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapSetHundredTwentyEighth); }},
        {AppCommands::snapCyclePrev, nullptr, "Set the timeline's snap grid", "Timeline", "snapCyclePrev",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapCyclePrev); }},
        {AppCommands::snapCycleNext, nullptr, "Set the timeline's snap grid", "Timeline", "snapCycleNext",
         [](const MainComponent& m) { return m.isTimelineReachable(); },
         [](MainComponent& m) { return m.applySnapCommand(AppCommands::snapCycleNext); }},
        // ---- Zoom: routed per focused surface, like the clipboard verbs ----
        {AppCommands::zoomInHorizontal, nullptr, "Zoom the focused editor", "View", "zoomInHorizontal",
         [](const MainComponent& m) { return m.isZoomCommandActive(AppCommands::zoomInHorizontal); },
         [](MainComponent& m) { return m.applyZoomCommand(AppCommands::zoomInHorizontal); }},
        {AppCommands::zoomOutHorizontal, nullptr, "Zoom the focused editor", "View", "zoomOutHorizontal",
         [](const MainComponent& m) { return m.isZoomCommandActive(AppCommands::zoomOutHorizontal); },
         [](MainComponent& m) { return m.applyZoomCommand(AppCommands::zoomOutHorizontal); }},
        {AppCommands::zoomInVertical, nullptr, "Zoom the focused editor", "View", "zoomInVertical",
         [](const MainComponent& m) { return m.isZoomCommandActive(AppCommands::zoomInVertical); },
         [](MainComponent& m) { return m.applyZoomCommand(AppCommands::zoomInVertical); }},
        {AppCommands::zoomOutVertical, nullptr, "Zoom the focused editor", "View", "zoomOutVertical",
         [](const MainComponent& m) { return m.isZoomCommandActive(AppCommands::zoomOutVertical); },
         [](MainComponent& m) { return m.applyZoomCommand(AppCommands::zoomOutVertical); }},
        // The ONE bottom-dock open/close toggle -- through the (renamed) toolbar button's
        // triggerClick(), same "reuse the toggle path" idiom the three "show tab" rows below used
        // to use individually.
        {AppCommands::toggleBottomPanel,
         "Toggle Bottom Panel",
         "Show or hide the bottom-docked panel, reopening on its last active tab",
         "View",
         "toggleBottomPanel",
         {},
         [](MainComponent& m) {
             m.toggleBottomPanelFromShortcut();
             return true;
         }},
        // No longer a toggle -- opens the dock if needed and switches to this tab; a second
        // press while already showing is a no-op (never closes the dock -- that's toggleBottomPanel's
        // job now). showBottomDockTab() also brings a DETACHED tab's own window to the front instead.
        {AppCommands::toggleTimelinePanel,
         "Show Timeline Tab",
         "Show the Timeline tab in the bottom-docked panel",
         "View",
         "toggleTimelinePanel",
         {},
         [](MainComponent& m) {
             m.showBottomDockTabFromShortcut(synth::ui::BottomDockComponent::Tab::Timeline);
             return true;
         }},
        {AppCommands::toggleMixerPanel,
         "Show Mixer Tab",
         "Show the Mixer tab in the bottom-docked panel",
         "View",
         "toggleMixerPanel",
         {},
         [](MainComponent& m) {
             m.showBottomDockTabFromShortcut(synth::ui::BottomDockComponent::Tab::Mixer);
             return true;
         }},
        {AppCommands::toggleMidiRemotePanel,
         "Show Controllers Tab",
         "Show the Controllers tab in the bottom-docked panel",
         "View",
         "toggleMidiRemotePanel",
         {},
         [](MainComponent& m) {
             m.showBottomDockTabFromShortcut(synth::ui::BottomDockComponent::Tab::MidiRemote);
             return true;
         }},
        // The fourth view's key: focuses the canvas while it is the main view.
        {AppCommands::showCanvas,
         "Show Canvas",
         "Show the patch canvas and focus it",
         "View",
         "showCanvas",
         {},
         [](MainComponent& m) {
             m.showBottomDockTabFromShortcut(synth::ui::BottomDockComponent::Tab::Canvas);
             return true;
         }},
        // A no-op (that still consumes the key) when the active tab has no side pane.
        {AppCommands::toggleSidePane,
         "Show/Hide Side Pane",
         "Show or hide the side pane of the active bottom-panel tab",
         "View",
         "toggleSidePane",
         {},
         [](MainComponent& m) {
             m.toggleActiveSidePane();
             return true;
         }},
    };
    // The triplet and dotted Set Grid rows sit just before the grid cycle.
    const auto cycle = std::find_if(rows.begin(), rows.end(),
                                    [](const CommandSpec& row) { return row.id == AppCommands::snapCyclePrev; });
    auto snapRows = buildSnapNoteValueCommandRows();
    rows.insert(cycle, std::make_move_iterator(snapRows.begin()), std::make_move_iterator(snapRows.end()));
    return rows;
}

const std::vector<MainComponent::CommandSpec>& MainComponent::commandTable() const {
    static const std::vector<CommandSpec> table = [] {
        std::vector<CommandSpec> t;
        auto append = [&](std::vector<CommandSpec> part) {
            for (auto& c : part)
                t.push_back(std::move(c));
        };
        append(buildGeneralCommandRows());
        append(buildEditAndGraphCommandRows());
        append(buildTimelineAndPanelCommandRows());
        append(buildFocusAndHelpCommandRows());
        append(buildTransportCommandRows());
        append(buildSelectionStepCommandRows());
        append(buildViewSwapCommandRows());
        return t;
    }();
    return table;
}
