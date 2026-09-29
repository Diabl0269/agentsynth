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

        for (const auto& other : placed) {
            if (!bubble->intersects(other))
                continue;
            const int overlap = std::min(bubble->getRight(), other.getRight()) - std::max(bubble->getX(), other.getX());
            const int shift = std::min(overlap, bubble->getWidth() / 2);
            bubble->translate(bubble->getCentreX() >= other.getCentreX() ? shift : -shift, 0);
        }

        const bool stillOverlaps = std::any_of(
            placed.begin(), placed.end(), [&](const juce::Rectangle<int>& other) { return bubble->intersects(other); });
        if (stillOverlaps || !window.contains(*bubble))
            continue;
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
