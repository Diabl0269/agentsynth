// MorphMapRestoreGlide.cpp -- an undo or redo that moves dots or the cursor: they are drawn gliding from where they
// were, the stored positions final at once (KeyedTween).
#include "MorphMap.h"
#include "MorphMapPalette.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

juce::Point<float> MorphMap::getDrawnDot(int index) const {
    if (index < 0 || index >= (int)dots_.size())
        return {};
    return moveTween_.drawnAt(index, {dots_[(size_t)index].x, dots_[(size_t)index].y});
}

juce::Colour MorphMap::resolvedDotColour(const Dot& dot, int index) const {
    return morphShapeColour(synth::theme::themeOf(*this).colors, morphColourSlot(dot.colourSlot, index), dot.colour);
}

juce::Colour MorphMap::getDrawnDotColour(int index) const {
    if (index < 0 || index >= (int)dots_.size())
        return {};
    const auto resolved = resolvedDotColour(dots_[(size_t)index], index);
    return colourTween_.drawnAt(index, TweenColour::from(resolved)).toColour();
}

juce::Point<float> MorphMap::getDrawnCursor() const { return moveTween_.drawnAt(kCursorKey, cursor_); }

/** Same count and a dot at a new place: the glide. Any other change (a count change, a first fill) drops the
    running glides and lands at once. */
void MorphMap::startMoveGlide(const std::vector<Dot>& before, bool glideMoved) {
    if (!glideMoved || before.size() != dots_.size() || before.empty()) {
        for (int i = 0; i < (int)std::max(before.size(), dots_.size()); ++i)
            moveTween_.erase(i);
        for (int i = 0; i < (int)std::max(before.size(), dots_.size()); ++i)
            colourTween_.erase(i);
        return;
    }
    moveTween_.forceAnimateForTest(animatesOffScreenForTest());
    colourTween_.forceAnimateForTest(animatesOffScreenForTest());
    for (int i = 0; i < (int)dots_.size(); ++i) {
        const auto was = TweenColour::from(resolvedDotColour(before[(size_t)i], i));
        const auto now = TweenColour::from(resolvedDotColour(dots_[(size_t)i], i));
        if (was != now)
            colourTween_.retarget(i, colourTween_.drawnAt(i, was), now);
    }
    for (int i = 0; i < (int)dots_.size(); ++i) {
        const auto& was = before[(size_t)i];
        const auto& now = dots_[(size_t)i];
        if (was.x == now.x && was.y == now.y)
            continue;
        // The old drawn place continues a glide that was already running.
        moveTween_.retarget(i, moveTween_.drawnAt(i, {was.x, was.y}), {now.x, now.y}, true);
    }
}

} // namespace synth::ui
