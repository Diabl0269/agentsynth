// Concern: which card an arrow key on the canvas lands on -- kept free of GraphEditor and of
// components so it is unit-testable against plain rectangles.
#pragma once

#include "UI/Graph/ModuleStepOrder.h"
#include <cmath>
#include <optional>
#include <vector>

namespace synth::ui {

enum class CardDirection { Left, Right, Up, Down };

/** The card an arrow press in `direction` moves to from `from`.
 *
 *  Only cards whose centre lies strictly on the arrow's side of `from`'s centre count. Among those
 *  the nearest wins, centre to centre, with the arrow's own axis favoured: the score is the
 *  distance along the arrow plus twice the distance across it, so a card straight ahead beats a
 *  slightly closer one off to the side. Ties go to the lower node id.
 *
 *  `from` invalid or not among `cards` (nothing selected): the first card in ModuleStepOrder's
 *  left-to-right order, whatever the direction. A zero NodeID means no move: no cards at all, or
 *  none in that direction (the selection then stays where it is). */
inline juce::AudioProcessorGraph::NodeID nearestCardInDirection(const std::vector<StepModule>& cards,
                                                                juce::AudioProcessorGraph::NodeID from,
                                                                CardDirection direction) {
    std::optional<juce::Point<float>> origin;
    for (const auto& c : cards)
        if (c.id == from)
            origin = c.bounds.getCentre();
    if (!origin.has_value())
        return adjacentModule(cards, {}, 1);

    juce::AudioProcessorGraph::NodeID best;
    float bestScore = 0.0f;
    for (const auto& c : cards) {
        if (c.id == from)
            continue;
        const auto d = c.bounds.getCentre() - *origin;
        const bool horizontal = direction == CardDirection::Left || direction == CardDirection::Right;
        const float sign = (direction == CardDirection::Left || direction == CardDirection::Up) ? -1.0f : 1.0f;
        const float along = (horizontal ? d.x : d.y) * sign;
        const float across = std::abs(horizontal ? d.y : d.x);
        if (along <= 0.0f)
            continue;
        const float score = along + 2.0f * across;
        if (best.uid == 0 || score < bestScore || (score == bestScore && c.id.uid < best.uid)) {
            best = c.id;
            bestScore = score;
        }
    }
    return best;
}

} // namespace synth::ui
