#pragma once

// A canvas fixture for tests that need real cards but not the card-body layout code. Header-only; not registered in
// Tests/CMakeLists.txt.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace cardbody_test {

using NodeID = juce::AudioProcessorGraph::NodeID;

/** A canvas with real graph nodes and the cards updateComponents() builds for them. */
struct CardCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    CardCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(2400, 1800);
    }

    NodeID add(std::unique_ptr<juce::AudioProcessor> processor, int x, int y,
               std::optional<synth::CardLayout> layout = std::nullopt) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        if (layout.has_value())
            synth::setCardLayoutOverride(engine.getGraph(), nullptr, node->nodeID, layout);
        return node->nodeID;
    }

    ModuleComponent* card(NodeID id) {
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                return comp;
        return nullptr;
    }

    juce::AudioProcessor* processor(NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return node != nullptr ? node->getProcessor() : nullptr;
    }
};

} // namespace cardbody_test
