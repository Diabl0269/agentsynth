#pragma once

// CurveDrawGesture.h -- the pen on a shape curve: press, drag and release draw a stretch of the curve the way the
// timeline's lane Draw tool does. Sine, Triangle, Saw and Square fill the dragged span with whole cycles (one per grid
// cell while the grid is on; on a Notes, Harmonics or Octaves grid one peak on each line instead, so the peaks sit on
// the chosen notes or harmonics) between the press's and the drag's levels (the default band's height, centred on the
// press, while the drag stays level), Line ramps from press to release, and Free follows the mouse and is thinned on
// release. Only the points inside the dragged span change; the rest of the curve stays.
//
// How many cycles (grid Off; a grid's cell sets them otherwise): a plain drag adds cycles sideways at the REMEMBERED
// cycle width and sets the height up and down. Holding Option freezes the box and turns up/down into the count (up =
// more, down = fewer, in the same width); `[` and `]` step it by key, in a drag or between strokes. The width the count
// leaves is remembered for the next stroke (and across windows, through onCycleWidthChanged).
//
// The drag's preview and the release's commit both read `result()`, which runs drawOnCurve over the curve as it stood
// at the press, so what the drag shows is what the drop writes. A release writes ONE gesture bracket (one undo step);
// Escape cancels and writes nothing. Owned by value by its CurveEditorComponent, which hands its mouse presses here
// while a pen is chosen. Message thread only.
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <utility>

namespace synth::ui {

enum class DrawShape; // DrawShape.h; an enum class is complete as declared, and this header must stay cheap to reach
class CurveEditorComponent;
struct ShapeDrawStroke;
struct ShapeDrawResult; // ShapeDrawStroke.h; kept out of here so this header stays cheap to reach

class CurveDrawGesture {
public:
    /** The default band and cycle count a periodic shape draws with (ShapeDrawRange's numbers). */
    struct Band {
        float lo = 0.5f;
        float hi = 0.9f;
        int cycles = 3;
    };

    explicit CurveDrawGesture(CurveEditorComponent& editor);
    ~CurveDrawGesture();

    /** The chosen pen; none means the editor edits points as usual. Choosing one drops a drag in flight. */
    void setPen(std::optional<DrawShape> pen);
    std::optional<DrawShape> getPen() const noexcept { return pen_; }
    void setBand(Band band) noexcept { band_ = band; }
    const Band& getBand() const noexcept { return band_; }

    /** The remembered width of one cycle in the curve's x (0..1); unset, the default band's count across the full
     *  width. Set by the count gesture and the step keys, restored by the window from its saved preference. */
    void setCycleWidth(std::optional<double> width) noexcept { cycleWidth_ = width; }
    std::optional<double> getCycleWidth() const noexcept { return cycleWidth_; }
    /** Tells the host the remembered width changed, so it can save it. */
    std::function<void(double width)> onCycleWidthChanged;
    /** A short sentence for the host's status line (the count after a key step, why a key did nothing). */
    std::function<void(const juce::String& note)> onNote;
    /** `[` (delta -1) and `]` (+1): one more or fewer whole cycle in the span being dragged, else across the full
     * width. True when the key was taken (it is taken, saying why, even when a grid sets the count instead). */
    bool stepCount(int delta);

    /** Each returns true when it took the event. A press is taken whenever a pen is chosen. */
    bool press(const juce::MouseEvent& e);
    bool drag(const juce::MouseEvent& e);
    bool release(const juce::MouseEvent& e);
    /** Drops the drag in flight, changing nothing. */
    void cancel();
    bool isActive() const noexcept { return active_; }

    /** What the drag so far would make of the curve: the single source of the preview and of the commit. */
    ShapeDrawResult result() const;
    /** The live chip ("2 cycles", "8 peaks, one per note"), the refusal while one stands, else empty. */
    juce::String chipText() const;

    void paint(juce::Graphics& g) const;

    /** Pixels of Option+vertical drag per cycle added or removed, and the most cycles one span can hold. */
    static constexpr float kCountStepPx = 12.0f;
    static constexpr int kMaxCycles = 256;
    /** Below this many pixels between the press's and the drag's levels a periodic shape keeps the default height. */
    static constexpr float kMinBoxHeightPx = 3.0f;

private:
    std::pair<float, float> boxLevels(const juce::Point<double>& a, const juce::Point<double>& b) const;
    ShapeDrawStroke stroke(const juce::Point<double>& a, const juce::Point<double>& b) const;
    double effectiveCycleWidth() const;
    bool countApplies() const;
    void setCycles(double span, int cycles);
    void dragCount(const juce::MouseEvent& e);

    CurveEditorComponent& editor_;
    std::optional<DrawShape> pen_;
    Band band_;
    std::optional<double> cycleWidth_;
    bool active_ = false;
    bool countHeld_ = false; ///< Option is down: the box is frozen and up/down counts.
    bool gridHint_ = false;  ///< Option went down while a grid sets the count: the chip says so.
    float countAnchorY_ = 0.0f;
    int countBase_ = 1;
    struct State; ///< The curve at the press and the Free samples (needs LfoCustomWave, so lives in the .cpp).
    std::unique_ptr<State> s_;
    juce::Point<double> a_, b_;
};

} // namespace synth::ui
