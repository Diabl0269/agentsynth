// MainComponentCommandPredicates.cpp -- the isActive predicates MainComponentCommandTable.cpp's
// table rows share (split, purely to keep that file under the repo's 1,000-line cap --
// registering the Mixer edit surface pushed it over; no behaviour change). MainComponent is
// declared in MainComponent.h; the table itself and the named perform() bodies stay in
// MainComponentCommandTable.cpp.
#include "MainComponent.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

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

// The lane editor the point commands act on: the one holding focus, else the lane last shown or focused.
synth::ui::AutomationLaneEditor* MainComponent::activeLaneEditor() const {
    if (auto* focused = timelinePanel.getAutomationLanes().focusedEditor())
        return focused;
    return timelinePanel.getAutomationLanes().editorFor(timelinePanel.getSelectedAutomationLane());
}

// The lane the lane commands act on: the one whose editor or header holds focus, else the lane last shown or focused.
synth::LaneId MainComponent::activeLane() const {
    const auto focused = timelinePanel.getAutomationLanes().focusedLane();
    return focused.isValid() ? focused : timelinePanel.getSelectedAutomationLane();
}

// The module Replace Module acts on when the timeline owns focus: a focused modulator row's source (the LFO), else
// the focused lane's target module. Empty when neither (the canvas selection then decides).
juce::String MainComponent::replaceModuleTargetUuid() const {
    const auto& lanes = timelinePanel.getAutomationLanes();
    const auto* focused = getFocusedComponentForMenu();
    if (!bottomDock.isTimelineShowing() || focused == nullptr)
        return {};
    if (const auto source = lanes.modulatorSourceOwning(focused); source.isNotEmpty())
        return source;
    if (const auto* lane = timelineDoc.getLane(lanes.laneOwning(focused)))
        return lane->nodeUuid;
    return {};
}

bool MainComponent::canReplaceModuleFromShortcut() const {
    const auto uuid = replaceModuleTargetUuid();
    if (uuid.isEmpty())
        return graphEditor.getCardKeyboard().canReplaceSelectedModule();
    const auto* node = findNodeByUuid(uuid);
    if (node == nullptr)
        return false;
    for (auto* card : graphEditor.getModuleComponents())
        if (card != nullptr && card->getNodeId() == node->nodeID)
            return card->canReplace(); // a member of a collapsed group is revealed on perform
    return false;
}

bool MainComponent::replaceModuleFromShortcut() {
    const auto uuid = replaceModuleTargetUuid();
    if (uuid.isEmpty())
        return graphEditor.getCardKeyboard().replaceSelectedModule();
    if (!canReplaceModuleFromShortcut())
        return false;
    // A module inside a collapsed macro (every instrument track's modules, by default) has no visible card: unfold
    // each collapsed ancestor, outermost first, through the same undoable setMacroCollapsed the card's expand button
    // uses, so the picker has a card to sit on.
    auto& macros = graphEditor.getMacros();
    std::vector<juce::String> collapsedAncestors;
    for (const auto* macro = macros.findByMember(uuid); macro != nullptr;
         macro = macro->parentId.isEmpty() ? nullptr : macros.find(macro->parentId))
        if (macro->collapsed)
            collapsedAncestors.insert(collapsedAncestors.begin(), macro->id);
    for (const auto& id : collapsedAncestors)
        graphEditor.getMacroController().setMacroCollapsed(id, false);
    showNodeOnCanvas(uuid); // switches to the canvas and selects the card, so the picker opens where it can be seen
    // The unfold holds the module's card back until its glide lands, and a hidden card cannot take the picker: wait
    // for the fold to settle (at once when nothing animates).
    auto open = [safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe == nullptr)
            return false;
        safe->graphEditor.grabKeyboardFocus();
        return safe->graphEditor.getCardKeyboard().replaceSelectedModule();
    };
    if (!graphEditor.getCardGlide().fold().isLive())
        return open();
    graphEditor.getCardGlide().fold().whenSettled([open] { open(); });
    return true;
}

// True while the active lane editor has a point selected.
bool MainComponent::hasPointSelection() const {
    const auto* editor = activeLaneEditor();
    return editor != nullptr && !editor->getPointSelection().isEmpty();
}

// The Copy/Paste/Duplicate/Cut/Repeat block: each routes by resolveEditSurface(), but the exact
// per-surface predicate differs by command (see each case) -- moved verbatim out of the former
// per-command switches in MainComponent::getCommandInfo, now selected by id in one place.
// Mixer is inactive for all five -- its own keyboard verbs (Left/Right/Up/Down/M/S/R) are
// resolved directly by MixerPanelComponent::keyPressed, never through this table.
bool MainComponent::isEditSurfaceCommandActive(juce::CommandID id) const {
    switch (id) {
    case AppCommands::copySelection:
        switch (resolveEditSurface()) {
        case EditSurface::TimelineClips:
            // getClipSelection() has no const overload; hasClipSelection() answers the same
            // !clipSelection_.isEmpty() question and is const, so getCommandInfo (const-context
            // predicate) uses it here in place of the non-const getClipSelection().size() > 0
            // the perform-side action code below still uses for its status-bar count. A live range
            // (the Range tool) is copyable too.
            return timelinePanel.hasClipSelection() || timelinePanel.hasRangeSelection();
        case EditSurface::PianoRoll:
            return timelinePanel.getPianoRoll().hasNoteSelection();
        case EditSurface::AutomationLane:
            return hasPointSelection();
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
        case EditSurface::AutomationLane:
            return activeLaneEditor() != nullptr && activeLaneEditor()->canPastePoints();
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
        case EditSurface::AutomationLane:
            return activeLane().isValid(); // duplicates the lane
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
            return timelinePanel.canCutClips() || timelinePanel.hasRangeSelection();
        case EditSurface::AutomationLane:
            return hasPointSelection();
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
        case EditSurface::AutomationLane:
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
    case EditSurface::AutomationLane:
        return isTimelineReachable();
    case EditSurface::Mixer:
        return false; // no zoom concept at all -- inactive on both axes, unlike Graph above
    }
    return true;
}

// Export is unavailable while an Export Audio/Stems render owns the engine.
bool MainComponent::isExportAvailable() const { return !isBounceInProgress_; }

bool MainComponent::isWelcomeScreenHidden() const { return welcomeFade_ == nullptr || !welcomeFade_->isShown(); }
