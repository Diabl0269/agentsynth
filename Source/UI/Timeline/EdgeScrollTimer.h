#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

// EdgeScrollTimer: the follow behind every beat-axis drag that scrolls its view at an edge (the Sections strip, the
// ruler, the clip lanes, the lane editor, the piano roll and the drum machine and its lanes). The owner tells it where
// the pointer is on every drag event; while the drag follows and the pointer sits in an edge zone (EdgeAutoScroll.h)
// it runs one frame per display refresh on the shared EdgeScrollPump, and each frame the owner scrolls its view by
// scrollView() (or the steps) and re-derives its preview from the still pointer. The speed eases in and out, so a
// pointer leaving the zone glides to a stop over a few frames. A header reached by many files holds it through a
// forward declaration and a unique_ptr, so this header stays narrow.
namespace synth::ui {

struct TimelineViewState;
struct EdgeIntent;
class EdgeScrollPump;

class EdgeScrollTimer {
public:
    /** Frames come from `host`'s display; `host` must outlive this. The vertical axis (with track's `vertical`) runs up
     *  to `verticalMaxPerSec` of the owner's units a second, moved in whole `verticalQuantum`s (0 keeps fractions); the
     *  one-argument form runs it at the timeline's pace (kEdgeAutoScrollMaxPxPerSec) in whole px. */
    explicit EdgeScrollTimer(juce::Component& host);
    EdgeScrollTimer(juce::Component& host, double verticalMaxPerSec, double verticalQuantum);
    ~EdgeScrollTimer();

    /** One frame that has something to move (or is easing to a stop); not called once the follow has settled. */
    std::function<void()> onTick;

    /** Remembers the pointer and the area whose left and right edges scroll (and top and bottom with `vertical`), and
     *  runs the frames while `following` and the pointer sits in one of those edge zones. A press inside a zone
     *  scrolls only once the pointer moves toward that edge (EdgeIntent). The press is the point given to press(), or
     *  without one the first pointer of the drag (the first call with `following` after stop() or a call without it).
     */
    void track(bool following, juce::Point<int> pointer, juce::Rectangle<int> area, bool vertical = false);
    /** The owner's mouse-down point (in track's coordinates): the next drag measures its edge intent from here, so a
     *  first drag event that already lands deep in a zone still counts as a move toward that edge. */
    void press(juce::Point<int> p) noexcept { pendingPress_ = p; }
    /** Stops the scroll at once and ends the drag: the next track() starts a new one. Call it on release, Esc and
     *  cancel. */
    void stop();
    bool isRunning() const noexcept;
    juce::Point<int> pointer() const noexcept { return pointer_; }

    /** This frame's sideways move in whole px (negative: toward the left), eased; 0 between frames that move. */
    double horizontalStepPx() const noexcept { return stepX_; }
    /** This frame's up and down move in the owner's vertical units (only with `vertical`); negative near the top. */
    double verticalStep() const noexcept { return stepY_; }

    /** Scrolls `view` sideways by horizontalStepPx(). Returns the pixels the view moved: 0 when it is pinned at beat 0
     *  (the view never stops to the right), which also drops the eased speed so a later move starts from rest. */
    double scrollView(TimelineViewState& view) noexcept;
    /** The owner's vertical scroll hit its end: the vertical speed drops to rest. */
    void haltVertical() noexcept;

    /** Test seam: one frame of the default test length, as the display would run it. */
    void frameForTest();

private:
    void frame();
    double horizontalTarget() const noexcept;
    double verticalTarget() const noexcept;

    std::unique_ptr<EdgeScrollPump> pump_;
    std::unique_ptr<EdgeIntent> intent_; // EdgeAutoScroll.h stays out of this header
    double verticalMaxPerSec_;
    double verticalQuantum_;
    juce::Point<int> pointer_;
    juce::Rectangle<int> area_;
    bool vertical_ = false;
    bool following_ = false;
    std::optional<juce::Point<int>> pendingPress_;
    bool inDrag_ = false;
    bool armedX_ = true;
    bool armedY_ = true;
    double stepX_ = 0.0;
    double stepY_ = 0.0;
};

} // namespace synth::ui
