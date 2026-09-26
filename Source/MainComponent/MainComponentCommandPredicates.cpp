// MainComponentCommandPredicates.cpp -- the isActive predicates MainComponentCommandTable.cpp's
// table rows share (FRO227 split, purely to keep that file under the repo's 1,000-line cap --
// registering the Mixer edit surface pushed it over; no behaviour change). MainComponent is
// declared in MainComponent.h; the table itself and the named perform() bodies stay in
// MainComponentCommandTable.cpp.
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

// ---- Named isActive predicates shared by more than one row ----

bool MainComponent::hasSelection() const { return graphEditor.getSelectionCount() > 0; }

bool MainComponent::canGroupSelection() const { return graphEditor.getSelectionCount() > 1 || touchesAnyMacro(); }

bool MainComponent::touchesAnyMacro() const {
    for (auto nodeId : graphEditor.getSelectedNodes()) {
        if (graphEditor.getMacroController().macroForNode(nodeId) != nullptr)
            return true;
    }
    return false;
}

// The Copy/Paste/Duplicate/Cut/Repeat block: each routes by resolveEditSurface(), but the exact
// per-surface predicate differs by command (see each case) -- moved verbatim out of the former
// per-command switches in MainComponent::getCommandInfo, now selected by id in one place.
// FRO227: Mixer is inactive for all five -- its own keyboard verbs (Left/Right/Up/Down/M/S/R) are
// resolved directly by MixerPanelComponent::keyPressed, never through this table.
bool MainComponent::isEditSurfaceCommandActive(juce::CommandID id) const {
    switch (id) {
    case AppCommands::copySelection:
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            // getClipSelection() has no const overload; hasClipSelection() answers the same
            // !clipSelection_.isEmpty() question and is const, so getCommandInfo (const-context
            // predicate) uses it here in place of the non-const getClipSelection().size() > 0
            // the perform-side action code below still uses for its status-bar count.
            return timelinePanel.hasClipSelection();
        case EditSurface::PianoRoll:
            return timelinePanel.getPianoRoll().hasNoteSelection();
        case EditSurface::Graph:
            return graphEditor.getSelectionCount() > 0;
        case EditSurface::Mixer:
            return false; // no clipboard model on the mixer
        }
        break;
    case AppCommands::pasteSelection:
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            return timelinePanel.canPasteClips();
        case EditSurface::PianoRoll:
            // canPasteNotes() is BOTH halves: a non-empty note clipboard AND an open clip. A roll
            // with nothing open has nowhere to put the block, so the row greys out rather than
            // silently discarding a paste.
            return timelinePanel.getPianoRoll().canPasteNotes();
        case EditSurface::Graph:
            return graphEditor.canPaste();
        case EditSurface::Mixer:
            return false; // no clipboard model on the mixer
        }
        break;
    case AppCommands::duplicateSelection:
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            // Same substitution as copySelection above (const-context: hasClipSelection()).
            return timelinePanel.hasClipSelection();
        case EditSurface::PianoRoll:
            return timelinePanel.getPianoRoll().hasNoteSelection();
        case EditSurface::Graph:
            return graphEditor.getSelectionCount() > 0;
        case EditSurface::Mixer:
            return false; // no clipboard model on the mixer
        }
        break;
    case AppCommands::cutSelection:
        // Same enablement predicate Copy uses on every surface -- a cut is a copy that also
        // deletes, so anything copyable is cuttable and the two rows can never disagree.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            return timelinePanel.canCutClips();
        case EditSurface::PianoRoll:
            return timelinePanel.getPianoRoll().hasNoteSelection();
        case EditSurface::Graph:
            return graphEditor.getSelectionCount() > 0;
        case EditSurface::Mixer:
            return false; // no clipboard model on the mixer
        }
        break;
    case AppCommands::repeatSelection:
        // Inactive on Graph AND Mixer -- neither a spatial canvas nor a column strip has a time
        // axis to tile copies along. See performRepeatSelection.
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            return timelinePanel.hasClipSelection();
        case EditSurface::PianoRoll:
            return timelinePanel.getPianoRoll().hasNoteSelection();
        case EditSurface::Graph:
        case EditSurface::Mixer:
            return false; // no clipboard model on the mixer
        }
        break;
    default:
        break;
    }
    return true;
}

bool MainComponent::isZoomCommandActive(juce::CommandID id) const {
    const bool vertical = id == AppCommands::zoomInVertical || id == AppCommands::zoomOutVertical;
    // The graph canvas zooms UNIFORMLY (GraphEditor::zoomAroundCentre -- one zoomLevel, no
    // separate axes), so the horizontal pair drives it and the vertical pair is inactive there
    // rather than silently doing the same thing twice under a different key.
    switch (resolveEditSurface()) {
    case EditSurface::Graph:
        return !vertical;
    case EditSurface::TimelineClips:
    case EditSurface::PianoRoll:
        return isBottomDockVisible;
    case EditSurface::Mixer:
        return false; // no zoom concept at all -- inactive on both axes, unlike Graph above
    }
    return true;
}
