#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

// EdgeScrollTimer: the one timer behind every beat-axis drag that follows its view (the Sections strip, the ruler, the
// lane editor, the drum machine and its lanes). The owner tells it where the pointer is on every drag event; it runs at
// kEdgeScrollHz only while the drag follows and the pointer sits in an edge zone (EdgeAutoScroll.h), and each tick the
// owner scrolls its view by scrollView() and re-derives its preview from the still pointer. A header reached by many
// files holds it through a forward declaration and a unique_ptr, so this header stays narrow.
namespace synth::ui {

struct TimelineViewState;
struct EdgeIntent;

class EdgeScrollTimer : public juce::Timer {
public:
    EdgeScrollTimer();
    ~EdgeScrollTimer() override;

    std::function<void()> onTick;
    void timerCallback() override {
        if (onTick)
            onTick();
    }

    /** Remembers the pointer and the area whose left and right edges scroll (and top and bottom with `vertical`), and
     *  runs the ticks while `following` and the pointer sits in one of those edge zones. A press inside a zone
     *  scrolls only once the pointer moves toward that edge (EdgeIntent). The press is the point given to press(), or
     *  without one the first pointer of the drag (the first call with `following` after stop() or a call without it).
     */
    void track(bool following, juce::Point<int> pointer, juce::Rectangle<int> area, bool vertical = false);
    /** The owner's mouse-down point (in track's coordinates): the next drag measures its edge intent from here, so a
     *  first drag event that already lands deep in a zone still counts as a move toward that edge. */
    void press(juce::Point<int> p) noexcept { pendingPress_ = p; }
    /** Stops the scroll and ends the drag: the next track() starts a new one. Call it on release, Esc and cancel. */
    void stop();
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
    std::unique_ptr<EdgeIntent> intent_; // EdgeAutoScroll.h stays out of this header
    std::optional<juce::Point<int>> pendingPress_;
    bool inDrag_ = false;
    bool armedX_ = true;
    bool armedY_ = true;
};

} // namespace synth::ui
