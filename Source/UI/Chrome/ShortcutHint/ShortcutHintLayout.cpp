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

namespace {

// Slides `bubble` sideways off every placed bubble it overlaps, each slide capped at half its width.
juce::Rectangle<int> slidOffPlaced(juce::Rectangle<int> bubble, const std::vector<juce::Rectangle<int>>& placed) {
    for (const auto& other : placed) {
        if (!bubble.intersects(other))
            continue;
        const int overlap = std::min(bubble.getRight(), other.getRight()) - std::max(bubble.getX(), other.getX());
        const int shift = std::min(overlap, bubble.getWidth() / 2);
        bubble.translate(bubble.getCentreX() >= other.getCentreX() ? shift : -shift, 0);
    }
    return bubble;
}

} // namespace

// Row 0 is the bubble's own place; when it still collides after the slide, rows further from the button
// are tried (a row of narrow icon buttons with wide "Shift+2" key text off the Mac), each with the same
// slide, before the bubble is left out.
std::vector<std::optional<juce::Rectangle<int>>> placeBubbles(const std::vector<BubbleRequest>& requests,
                                                              juce::Rectangle<int> window) {
    std::vector<std::optional<juce::Rectangle<int>>> result(requests.size());
    std::vector<juce::Rectangle<int>> placed;
    const auto overlapsPlaced = [&](const juce::Rectangle<int>& r) {
        return std::any_of(placed.begin(), placed.end(),
                           [&](const juce::Rectangle<int>& other) { return r.intersects(other); });
    };
    for (size_t i = 0; i < requests.size(); ++i) {
        const auto first = placeBubble(requests[i], window);
        if (!first)
            continue;
        const auto limit = requests[i].container.isEmpty() ? window : requests[i].container.getIntersection(window);
        const int step =
            (first->getHeight() + kStaggerGap) * (first->getY() >= requests[i].anchor.getCentreY() ? 1 : -1);

        for (int row = 0; row <= kMaxStaggerRows; ++row) {
            const auto bubble = slidOffPlaced(first->translated(0, row * step), placed);
            const auto& bounds = row == 0 ? window : limit;
            if (overlapsPlaced(bubble) || !bounds.contains(bubble))
                continue;
            placed.push_back(bubble);
            result[i] = bubble;
            break;
        }
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
