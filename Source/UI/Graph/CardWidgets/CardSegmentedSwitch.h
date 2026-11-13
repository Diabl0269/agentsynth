#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * A choice shown as a row of joined segments, one per value (the ADSR MS|BPM switch's look). One Tab
 * stop: Left/Right (or Up/Down) move the selection, Home/End jump to the ends. To a screen reader it
 * is a group named after the parameter holding one radio button per value. A right click is left to
 * the card's mouse listener. docs/layout/module-card.md#faders-switches-and-steppers.
 */
class CardSegmentedSwitch
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    /** `name` is the parameter's display name; `values` one segment each, left to right. */
    CardSegmentedSwitch(const juce::String& name, const juce::StringArray& values);

    /** Fired with the new index when the user picks a segment (click, key or accessibility press). */
    std::function<void(int index)> onChange;

    int getSelectedIndex() const noexcept { return selected_; }
    /** Out-of-range indices are ignored. */
    void setSelectedIndex(int index, juce::NotificationType notification);
    int getNumSegments() const noexcept { return segments_.size(); }
    juce::Button* getSegment(int index) const { return segments_[index]; }

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    int segmentAt(juce::Point<int> local) const;

    juce::OwnedArray<juce::TextButton> segments_;
    int selected_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardSegmentedSwitch)
};

} // namespace synth::ui
