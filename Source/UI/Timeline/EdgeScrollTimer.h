#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// EdgeScrollTimer: the one timer behind every beat-axis drag that follows its view (the Sections strip, the ruler, the
// lane editor, the drum machine and its lanes). The owner tells it where the pointer is on every drag event; it runs at
// kEdgeScrollHz only while the drag follows and the pointer sits in an edge zone (EdgeAutoScroll.h), and each tick the
// owner scrolls its view by scrollView() and re-derives its preview from the still pointer. A header reached by many
// files holds it through a forward declaration and a unique_ptr, so this header stays narrow.
namespace synth::ui {

struct TimelineViewState;

class EdgeScrollTimer : public juce::Timer {
public:
    std::function<void()> onTick;
    void timerCallback() override {
        if (onTick)
            onTick();
    }

    /** Remembers the pointer and the area whose left and right edges scroll (and top and bottom with `vertical`), and
     *  runs the timer only while `following` and the pointer sits in one of those edge zones. */
    void track(bool following, juce::Point<int> pointer, juce::Rectangle<int> area, bool vertical = false);
    juce::Point<int> pointer() const noexcept { return pointer_; }

    /** The tracked pointer's sideways speed in px per tick, faster nearer the edge; 0 in the middle. */
    double horizontalPxPerTick() const noexcept;
    /** The same up and down (only with `vertical`), scaled to `maxPerTick`; negative near the top. */
    double verticalPerTick(double maxPerTick) const noexcept;

    /** One tick's sideways scroll of `view` at horizontalPxPerTick(). Returns the pixels the view moved: 0 when it is
     *  pinned at beat 0 (the view never stops to the right). */
    double scrollView(TimelineViewState& view) const noexcept;

private:
    juce::Point<int> pointer_;
    juce::Rectangle<int> area_;
    bool vertical_ = false;
};

} // namespace synth::ui
