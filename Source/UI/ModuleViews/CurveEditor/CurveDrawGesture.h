#pragma once

// CurveDrawGesture.h -- the pen on a shape curve: press, drag and release draw a stretch of the curve the way the
// timeline's lane Draw tool does. Sine, Triangle, Saw and Square fill the dragged span with whole cycles (one per grid
// cell while the grid is on), Line ramps from press to release, and Free follows the mouse and is thinned on release.
// Only the points inside the dragged span change; the rest of the curve stays.
//
// The drag's preview and the release's commit both read `result()`, which runs drawOnCurve over the curve as it stood
// at the press, so what the drag shows is what the drop writes. A release writes ONE gesture bracket (one undo step);
// Escape cancels and writes nothing. Owned by value by its CurveEditorComponent, which hands its mouse presses here
// while a pen is chosen. Message thread only.
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

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

    /** Each returns true when it took the event. A press is taken whenever a pen is chosen. */
    bool press(const juce::MouseEvent& e);
    bool drag(const juce::MouseEvent& e);
    bool release(const juce::MouseEvent& e);
    /** Drops the drag in flight, changing nothing. */
    void cancel();
    bool isActive() const noexcept { return active_; }

    /** What the drag so far would make of the curve: the single source of the preview and of the commit. */
    ShapeDrawResult result() const;
    /** The live chip ("2 cycles"), the refusal while one stands, else empty. */
    juce::String chipText() const;

    void paint(juce::Graphics& g) const;

private:
    ShapeDrawStroke stroke(const juce::Point<double>& a, const juce::Point<double>& b) const;

    CurveEditorComponent& editor_;
    std::optional<DrawShape> pen_;
    Band band_;
    bool active_ = false;
    struct State; ///< The curve at the press and the Free samples (needs LfoCustomWave, so lives in the .cpp).
    std::unique_ptr<State> s_;
    juce::Point<double> a_, b_;
};

} // namespace synth::ui
