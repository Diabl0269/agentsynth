// GraphEditorModuleTitles.cpp
//
// Custom module titles (the "displayName" node property): get/set/resolve-for-display, and
// committing any inline title editor left open when the user clicks elsewhere. Moved here
// (FRO77 PR1) from GraphEditorSmartConnections.cpp, where they had been misplaced — titles are
// not a smart-connections concern. GraphEditor is declared in GraphEditor.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"

#include "AI/AIStateMapper/AIStateMapper.h" // kMaxModuleDisplayNameChars
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

// The user's custom title for a node, or an empty string when it has none.
//
// Message-thread ONLY and display-only, which is why — unlike "uuid" — it is deliberately NOT
// mirrored into the processor: nothing on the audio thread reads a card title, so there is no
// lock-free read to make sound, and adding a mirror would only create a second copy to keep in
// sync. Do not "fix" this by mirroring it.
//
// It is also deliberately NOT the processor's own name: ModuleBase::getName() is the
// auto-numbered "Chorus 2" that AudioEngine::updateModuleNames() recomputes wholesale on every
// graph change (it strips trailing digits to renumber), so a custom title written there would be
// clobbered by the next node added. The numbered name stays the fallback.
juce::String GraphEditor::getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
        return node->properties["displayName"].toString();
    return {};
}

// Sets (or, given blank/whitespace, clears) a node's custom title. Undoable.
void GraphEditor::setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name) {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return;

    // Blank or whitespace-only reverts to the auto-numbered default rather than showing an empty
    // header. Capped at the same length the untrusted patch path caps at, so a title typed here and
    // a title loaded from a file can never disagree about what is storable.
    const auto trimmed = name.trim().substring(0, synth::kMaxModuleDisplayNameChars);
    if (trimmed == getModuleDisplayName(nodeId))
        return; // no-op rename: do not burn an undo step on it

    auto apply = [this, nodeId, trimmed] {
        if (auto* n = audioEngine.getGraph().getNodeForId(nodeId)) {
            if (trimmed.isEmpty())
                n->properties.remove("displayName");
            else
                n->properties.set("displayName", trimmed);
        }
        for (auto* comp : content.getModules())
            if (comp != nullptr && comp->getNodeId() == nodeId)
                comp->repaint();
    };

    if (undoManager)
        undoManager->recordStructuralChange(graph, apply);
    else
        apply();
}

// Title a card should paint: the custom one when set, else the auto-numbered module name.
// Also GraphCanvasHost::getModuleTitle() — MacroGroupController::macroMemberNames() (FRO77
// PR2) needs it and title resolution otherwise requires a live GraphEditor.
juce::String GraphEditor::getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                         juce::AudioProcessor* processor) const {
    const auto custom = getModuleDisplayName(nodeId);
    if (custom.isNotEmpty())
        return custom;
    return processor != nullptr ? processor->getName() : juce::String();
}

// Commits and closes any open inline title editor, on any card.
//
// Called from every canvas press path (GraphEditor::mouseDown for empty canvas and cables,
// ModuleComponent::mouseDown for any card) because the editor's own onFocusLost is NOT enough:
// almost nothing on this canvas wants keyboard focus, so clicking a module body or the
// background never takes focus away from the editor and the callback never fires. Clicking
// away has to commit from the presser's side instead. Escape still cancels.
void GraphEditor::commitAnyOpenTitleRename() {
    // Copy the card list first: committing mutates the graph (and pushes an undo snapshot), and
    // nothing may be iterating content.getModules() across that.
    std::vector<ModuleComponent*> renaming;
    for (auto* comp : content.getModules())
        if (comp != nullptr && comp->isRenamingTitle())
            renaming.push_back(comp);

    for (auto* comp : renaming) {
        juce::Component::SafePointer<ModuleComponent> safe(comp);
        if (safe != nullptr)
            safe->finishTitleRename(true);
    }
}
