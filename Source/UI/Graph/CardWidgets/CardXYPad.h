#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * A module card's XY pad: one square pad that drives two parameters, X to the right and Y upwards. It looks like the
 * Filter's morph map (the dark plot, the 4x4 grid and the ring cursor, painted by MorphPlot.h) and reads like it: a
 * press or drag puts the ring under the pointer, and a readout under the plot shows both values.
 *
 * It edits nothing itself. The owner gives it the two values (0..1 positions, `setValues`) and hears every change
 * (`onMoved`) between `onGestureStart` and `onGestureEnd`, which bracket one drag or one key step, so one gesture is
 * one undo step. A bipolar axis shows its value as -1..1 (`setAxes`), the position stays 0..1. Keyboard: a Tab stop;
 * arrows nudge X and Y by `kStep` (Shift: `kFineStep`).
 */
class CardXYPad final
    : public juce::Component
    , public juce::TooltipClient {
public:
    /** What one axis is called and how its value reads. */
    struct Axis {
        juce::String name;
        bool bipolar = false;
        bool operator==(const Axis&) const = default;
    };

    static constexpr int kSize = 104;         ///< The width of the pad, which is also the height of its plot.
    static constexpr int kReadoutHeight = 16; ///< The readout under the plot.
    static constexpr int kHeight = kSize + kReadoutHeight;
    static constexpr float kStep = 0.02f;
    static constexpr float kFineStep = 0.002f;

    CardXYPad();

    /** Names the axes and says whether each reads bipolar; repaints and renames the control. */
    void setAxes(const Axis& x, const Axis& y);
    /** Moves the ring to `x`, `y` (0..1 each) without reporting a change. */
    void setValues(float x, float y);
    float getX() const noexcept { return x_; }
    float getY() const noexcept { return y_; }
    /** "Cutoff 0.50, Res -0.20": both axes, as the readout, the tooltip and the screen reader say them. */
    juce::String describe() const;

    /** One edit gesture (a drag, a key step) opens and closes around its `onMoved` calls. */
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;
    /** The user moved the ring: the new 0..1 positions. */
    std::function<void(float x, float y)> onMoved;
    /** Asked before an edit: true (having said why) swallows it. Unset never refuses. */
    std::function<bool()> refusesEdit;

    /** The dark square plate (the largest that fits above the readout, centred), the plot inside it where the ring
     *  moves, and the pixels of a 0..1 position (y up). */
    juce::Rectangle<float> plateArea() const;
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> pixelsFor(float x, float y) const;
    /** The key step the pad takes for `key`; true when the key was used. */
    bool nudge(const juce::KeyPress& key);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    juce::String getTooltip() override;

private:
    void moveTo(float x, float y);
    void moveToPixels(juce::Point<float> pixels);
    juce::String valueText(float position, const Axis& axis) const;
    void refreshTitle();

    Axis xAxis_{"X", false};
    Axis yAxis_{"Y", false};
    float x_ = 0.5f;
    float y_ = 0.5f;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardXYPad)
};

} // namespace synth::ui
