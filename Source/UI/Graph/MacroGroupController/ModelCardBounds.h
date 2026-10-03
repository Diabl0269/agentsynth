#pragma once

// ModelCardBounds.h
//
// A node's card rectangle read from the model (its stored "x"/"y" and the estimated card size), for the
// placement queries that must still see a card while the canvas has none: between
// GraphEditor::detachAllModuleComponents and the next updateComponents, which is exactly the window an AI
// edit plan builds its tracks in (docs/ai/timeline-ops.md#where-things-land). Header only.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/AttenuverterModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

// `sizeOf(factoryTypeName)` is the card-size estimate (GraphEditor::estimateModuleSize, or a GraphCanvasHost's
// estimateModuleSizeForType). Empty for a node with no stored position yet, or one that never gets a card (an
// Attenuverter lives in the mod matrix, not on the canvas).
template <typename SizeOf>
juce::Rectangle<int> modelCardBounds(const juce::AudioProcessorGraph::Node& node, SizeOf&& sizeOf) {
    auto* processor = node.getProcessor();
    if (processor == nullptr || dynamic_cast<AttenuverterModule*>(processor) != nullptr)
        return {};
    const int x = node.properties.getWithDefault("x", -1);
    const int y = node.properties.getWithDefault("y", -1);
    if (x == -1 || y == -1)
        return {};
    const juce::Point<int> size = sizeOf(AIStateMapper::getFactoryTypeName(processor));
    return {x, y, size.x, size.y};
}

} // namespace synth
