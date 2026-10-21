#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/**
 * The drum machine's Loudness lane: one bar per hit of the selected row, as tall as the hit is loud, drawn under the
 * grid's columns. Pressing and dragging across it sets each hit it crosses to the pointer's height (a straight ramp
 * between two pointer samples), previewed here and reported once on release. The lane is one Tab stop: Left and Right
 * move between the row's hits, Up and Down change the loudness by 5 (Shift 25). docs/timeline/drum-machine.md.
 */
class DrumLoudnessLane
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kKeyStep = 5;
    static constexpr int kKeyStepShift = 25;
    static constexpr int kBarInsetPx = 4;

    struct Hit {
        int step = 0;
        int velocity = 0;
        bool operator==(const Hit& other) const noexcept { return step == other.step && velocity == other.velocity; }
    };

    /** Where the lane's columns are: the component's x range of a step, and the step under an x (-1 for none). */
    struct Mapping {
        std::function<juce::Range<int>(int step)> stepRange;
        std::function<int(int x)> stepAt;
        int gutterWidth = 0;
    };

    DrumLoudnessLane();

    void setMapping(Mapping mapping) { mapping_ = std::move(mapping); }
    /** The hits of the selected row, sorted by step. `rowName` and `colour` name and tint them. */
    void setHits(std::vector<Hit> hits, const juce::String& rowName, juce::Colour colour);
    const std::vector<Hit>& getHits() const noexcept { return hits_; }
    /** The hit the keyboard is on (an index into getHits()), or -1 when there are none. */
    int getFocusedHit() const noexcept { return focusedHit_; }
    void setFocusedHit(int index);
    /** The loudness shown for the hit at `index` (the drag preview while one is live). */
    int velocityOf(int index) const;
    /** The bar of the hit at `index`, in this component's coordinates (empty when out of range). */
    juce::Rectangle<int> barBounds(int index) const;
    juce::String getAccessibilityValueText() const;

    /** Fired once per gesture (a whole drag, or one key press) with the hits whose loudness changed. */
    std::function<void(const std::vector<Hit>& changed)> onCommit;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Rectangle<int> barsArea() const;
    int velocityAtY(int y) const;
    void applyPoint(int x, int velocity);
    void endGesture(bool commit);

    Mapping mapping_;
    std::vector<Hit> hits_;
    std::vector<int> preview_;  ///< the loudness of each hit while a drag is live (same order as hits_)
    std::vector<char> crossed_; ///< per hit: the pointer has crossed its centre during this drag
    juce::String rowName_;
    juce::Colour colour_{juce::Colours::grey};
    int focusedHit_ = -1;
    bool dragging_ = false;
    int lastX_ = 0;
    int lastVelocity_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumLoudnessLane)
};

} // namespace synth::ui
