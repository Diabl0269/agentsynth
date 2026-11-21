#pragma once

// Questions other parts of the app ask a card body, answered without the whole CardBody.h: that header reaches over a
// hundred .cpp files, and a caller that only asks one question should not be rebuilt by every card body change.
// Defined in CardBody.cpp.

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

namespace synth {

class CardBody;

/** Where a card body's last layout put a keyboard stop in its lane grid (lane, column, place in the column) and the
 *  grid's top-left in card pixels. docs/layout/module-card-layout.md#lane-presentation. */
struct CardLaneStop {
    int lane = -1;
    int column = 0;
    int row = 0;
    int gridX = 0;
    int gridY = 0;
};

/** `component` (a body widget, or a child of one) in `body`'s lane grid; nothing outside the grid. */
std::optional<CardLaneStop> cardBodyLaneStopOf(const CardBody& body, const juce::Component* component);

/** True when the layout `body` was built from has changed since (CardBody::isStaleFor). */
bool cardBodyIsStaleFor(const CardBody& body, const juce::AudioProcessorGraph::Node& node);

} // namespace synth
