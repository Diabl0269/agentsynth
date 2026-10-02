#include "ShortcutHintLayout.h"

#include <algorithm>

namespace synth::ui::hint {

std::optional<juce::Rectangle<int>> placeBubble(const BubbleRequest& request, juce::Rectangle<int> window) {
    const auto limit = request.container.isEmpty() ? window : request.container.getIntersection(window);
    const int w = request.size.x;
    const int h = request.size.y;

    juce::Rectangle<int> bubble(request.anchor.getCentreX() - w / 2, request.anchor.getBottom() - kBubbleOverlap, w, h);
    if (bubble.getBottom() > limit.getBottom())
        bubble.setY(request.anchor.getY() + kBubbleOverlap - h); // flip above
    if (bubble.getY() < limit.getY() || bubble.getBottom() > limit.getBottom())
        return std::nullopt;

    if (bubble.getX() < window.getX())
        bubble.setX(window.getX());
    if (bubble.getRight() > window.getRight())
        bubble.setX(window.getRight() - w);
    if (bubble.getX() < window.getX())
        return std::nullopt;
    return bubble;
}

std::vector<std::optional<juce::Rectangle<int>>> placeBubbles(const std::vector<BubbleRequest>& requests,
                                                              juce::Rectangle<int> window) {
    std::vector<std::optional<juce::Rectangle<int>>> result(requests.size());
    std::vector<juce::Rectangle<int>> placed;
    for (size_t i = 0; i < requests.size(); ++i) {
        auto bubble = placeBubble(requests[i], window);
        if (!bubble)
            continue;
        const auto first = *bubble;

        for (const auto& other : placed) {
            if (!bubble->intersects(other))
                continue;
            const int overlap = std::min(bubble->getRight(), other.getRight()) - std::max(bubble->getX(), other.getX());
            const int shift = std::min(overlap, bubble->getWidth() / 2);
            bubble->translate(bubble->getCentreX() >= other.getCentreX() ? shift : -shift, 0);
        }

        const auto overlapsPlaced = [&](const juce::Rectangle<int>& r) {
            return std::any_of(placed.begin(), placed.end(),
                               [&](const juce::Rectangle<int>& other) { return r.intersects(other); });
        };
        if (overlapsPlaced(*bubble) || !window.contains(*bubble)) {
            // Narrow neighbours (a row of small icon buttons) with wide key text ("Shift+2" off the Mac):
            // stagger this bubble into a second row, away from its button, rather than dropping it.
            const auto limit = requests[i].container.isEmpty() ? window : requests[i].container.getIntersection(window);
            const bool below = first.getY() >= requests[i].anchor.getCentreY();
            const auto staggered =
                first.translated(0, below ? first.getHeight() + kStaggerGap : -(first.getHeight() + kStaggerGap));
            if (overlapsPlaced(staggered) || !limit.contains(staggered))
                continue;
            bubble = staggered;
        }
        placed.push_back(*bubble);
        result[i] = bubble;
    }
    return result;
}

std::optional<juce::Rectangle<int>> placeInsideTab(juce::Rectangle<int> tab, int nameRight, juce::Point<int> size) {
    juce::Rectangle<int> bubble(nameRight + kTabNameGap, tab.getCentreY() - size.y / 2, size.x, size.y);
    if (!tab.contains(bubble))
        return std::nullopt;
    return bubble;
}

BubbleKind kindOfPlacedBubble(juce::Rectangle<int> bubble, juce::Rectangle<int> anchor) {
    return bubble.getCentreY() < anchor.getCentreY() ? BubbleKind::AboveAnchor : BubbleKind::BelowAnchor;
}

juce::Point<float> bubbleOrigin(BubbleKind kind, juce::Rectangle<float> target, juce::Rectangle<float> anchor) {
    if (kind == BubbleKind::HiddenRow)
        return target.getCentre().translated(0.0f, kPillRisePx);
    return anchor.getCentre();
}

juce::Rectangle<float> animatedBubbleBounds(juce::Rectangle<float> target, juce::Point<float> origin, float t) {
    t = juce::jlimit(0.0f, 1.0f, t);
    const float scale = kBubbleStartScale + (1.0f - kBubbleStartScale) * t;
    const auto centre = origin + (target.getCentre() - origin) * t;
    return juce::Rectangle<float>(target.getWidth() * scale, target.getHeight() * scale).withCentre(centre);
}

std::vector<juce::Rectangle<int>> layoutHiddenRow(const std::vector<int>& pillWidths, int pillHeight,
                                                  juce::Rectangle<int> window, int statusBarTop) {
    std::vector<juce::Rectangle<int>> row;
    if (pillWidths.empty())
        return row;

    int total = kPillGap * (static_cast<int>(pillWidths.size()) - 1);
    for (int w : pillWidths)
        total += w;

    int x = window.getCentreX() - total / 2;
    const int y = statusBarTop - kRowBottomMargin - pillHeight;
    for (int w : pillWidths) {
        row.emplace_back(x, y, w, pillHeight);
        x += w + kPillGap;
    }
    return row;
}

} // namespace synth::ui::hint
