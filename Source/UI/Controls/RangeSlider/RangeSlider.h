#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/**
 * A horizontal slider with two handles that choose a range inside fixed limits. Drag either handle (a click on the
 * track moves the nearer one); the handles never cross. Each handle is its own Tab stop with its own screen-reader
 * name and value ("Tempo, lower handle, 90 BPM"). Left and Down step the focused handle down, Right and Up step it
 * up, Shift or Page Up and Page Down take ten steps, Home and End go to the limit it can reach. Keyboard and
 * programmatic changes glide 90 ms; a drag follows the pointer.
 */
class RangeSlider
    : public juce::Component
    , public juce::TooltipClient {
public:
    static constexpr int kPreferredHeight = 26;

    enum class Handle { Lower, Upper };

    RangeSlider();
    ~RangeSlider() override;

    /** The limits and the step every value is rounded to. Values are clamped into the new limits. */
    void setLimits(double minimum, double maximum, double step = 1.0);
    /** What the handles are called to a screen reader and in tooltips, and how a value is spoken. */
    void setLabel(const juce::String& label, const juce::String& unit);
    /** Replaces the default value wording (the number and the unit). */
    std::function<juce::String(double)> textForValue;

    /** Shows `lower`..`upper` (clamped, rounded to the step, swapped if reversed); does not call onChange. */
    void setValues(double lower, double upper);
    double getLower() const noexcept { return lower_; }
    double getUpper() const noexcept { return upper_; }
    double getMinimum() const noexcept { return min_; }
    double getMaximum() const noexcept { return max_; }

    /** Called after the user moves a handle (mouse, keys, a screen reader), never by setValues. */
    std::function<void(double lower, double upper)> onChange;

    // ---- Geometry and state, for tests ----------------------------------------------------------------
    juce::Component& getHandle(Handle which) noexcept;
    juce::Point<int> handleCentre(Handle which) const;
    juce::String spokenValue(Handle which) const;
    bool isMotionRunning() const noexcept { return glideAnim_.isRunning(); }
    void finishMotionForTest();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void lookAndFeelChanged() override;
    juce::String getTooltip() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class HandleStop;

    double snap(double value) const;
    double valueAt(float x) const;
    float xFor(double value) const;
    float trackLeft() const;
    float trackRight() const;
    /** The one place a handle's value changes; `glide` eases it, a drag does not. */
    void setHandle(Handle which, double value, bool notify, bool glide);
    void step(Handle which, int steps);
    bool keyPressedOn(Handle which, const juce::KeyPress& key);
    void layoutHandles();
    void glideTo(float lowerX, float upperX);
    void updateNames();
    juce::String nameOf(Handle which) const;

    double min_ = 0.0, max_ = 100.0, step_ = 1.0;
    double lower_ = 0.0, upper_ = 100.0;
    juce::String label_{"Range"}, unit_;
    float shownLower_ = 0.0f, shownUpper_ = 0.0f;
    float fromLower_ = 0.0f, fromUpper_ = 0.0f;
    std::unique_ptr<HandleStop> lowerStop_;
    std::unique_ptr<HandleStop> upperStop_;

    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver glideAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RangeSlider)
};

} // namespace synth::ui
