#pragma once

// One of the two range handles on the left edge of the curve box: a vertical track with a draggable marker, 0 at the
// bottom and 100 percent at the top. Up and Down step it by one percent (Shift: ten) for the keyboard.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ModCurveRangeHandle final
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    /** `name` is "Curve minimum" or "Curve maximum"; the screen-reader name appends the value. */
    explicit ModCurveRangeHandle(const juce::String& name);

    float value() const noexcept { return value_; }
    /** Moves the marker without telling anyone. */
    void setValue(float value);

    /** A drag (begin, change..., end) or one key step (begin, change, end). */
    std::function<void()> onBegin;
    std::function<void(float value)> onChange;
    std::function<void()> onEnd;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    float valueForY(float y) const;
    float markerY() const;
    void changeTo(float value);

    juce::String name_;
    float value_ = 0.0f;
    bool dragging_ = false;
};

} // namespace synth::ui
