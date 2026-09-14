// GraphEditorModuleTitles.cpp
//
// Custom module titles (the "displayName" node property): get/set/resolve-for-display, and
// committing any inline title editor left open when the user clicks elsewhere. Moved here
// (FRO77 PR1) from GraphEditorSmartConnections.cpp, where they had been misplaced — titles are
// not a smart-connections concern. GraphEditor is declared in GraphEditor.h.

#include "GraphEditor.h"

#include "AI/AIStateMapper/AIStateMapper.h" // kMaxModuleDisplayNameChars
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

juce::String GraphEditor::getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
        return node->properties["displayName"].toString();
    return {};
}

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

juce::String GraphEditor::getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                         juce::AudioProcessor* processor) const {
    const auto custom = getModuleDisplayName(nodeId);
    if (custom.isNotEmpty())
        return custom;
    return processor != nullptr ? processor->getName() : juce::String();
}

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
