// ShapeStamp.cpp -- the keyboard way to stamp a library shape: the same hand-over a drop makes, to the selected card.
#include "UI/Library/SoundBrowser/ShapeStamp.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth::ui {

juce::String stampShapeOnSelectedCard(GraphEditor& editor, const synth::shapes::LibraryShape& shape) {
    const auto selected = editor.getSelectedNodes();
    if (selected.size() != 1)
        return {};
    for (auto* card : editor.getModuleComponents()) {
        if (card->getNodeId() != selected.front())
            continue;
        auto* target = card->shapeDropTarget();
        if (target == nullptr || !target->acceptsShapeDrop(shape.kind))
            return {};
        target->shapeDropped(shape);
        return shape.kind == synth::shapes::ShapeKind::Filter ? "Filter" : "LFO";
    }
    return {};
}

} // namespace synth::ui
