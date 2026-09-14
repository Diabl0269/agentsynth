#pragma once

// Shared helpers for the MacroContainer test suite (Tests/MacroContainer/MacroContainer*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "../../Source/UI/GraphEditor/GraphEditor.h"
#include "../../Source/UI/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

/** Adds a module, lays out its ModuleComponent at a known position, and gives it a real
 *  persistent uuid up front — macro membership is keyed by uuid, and the plain graph.addNode()
 *  a bare test would use leaves it empty until something like graphToJSON assigns one lazily. */
NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

ModuleComponent* findComponent(GraphEditor& editor, NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}

} // namespace

// The chip-drag tests below drive selectMacro/beginSelectionDrag/dragSelectionBy directly, which
// exercises the API layer BENEATH GraphEditor::mouseDown/mouseDrag/mouseUp. That is exactly the
// layer a broken hit-test cannot fail in, so a second set of tests drives synthesised mouse events
// into GraphEditor itself, the same way GraphEditorViewportTests.cpp (Tests/GraphEditor/) and
// MinimapComponentTests.cpp already do — shared here because both MacroContainerGeometryTests.cpp
// and MacroContainerInteractionTests.cpp use it.
namespace {

juce::MouseEvent makeCanvasMouseEvent(juce::Component& comp, juce::Point<int> position, int clicks = 1) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), clicks,
                            false);
}

} // namespace
