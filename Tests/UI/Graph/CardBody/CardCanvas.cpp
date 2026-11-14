// CardCanvas.cpp -- the test canvas's node adding, kept out of the header so the layout-override header reaches only
// the tests that use it.
#include "CardCanvas.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"

namespace cardbody_test {

NodeID CardCanvas::add(std::unique_ptr<juce::AudioProcessor> processor, int x, int y,
                       std::optional<synth::CardLayout> layout) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    if (layout.has_value())
        synth::setCardLayoutOverride(engine.getGraph(), nullptr, node->nodeID, layout);
    return node->nodeID;
}

} // namespace cardbody_test
