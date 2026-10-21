#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/**
 * A row of toggle chips, one per lane of a stepped editor (the sequencer's step grid, later the drum machine's).
 * Several chips are on at once. The row is ONE Tab stop: Left/Right move the focused chip, Space toggles it; every
 * chip is also a toggle button of its own to a screen reader and the mouse, named "<Lane> lane" with the tooltip
 * "Show the <Lane> lane". docs/layout/lane-chips.md.
 */
class LaneChipRow
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kHeight = 22;

    LaneChipRow();

    /** Replaces the chips; `labels` are the lane names ("Pitch"). Every chip starts hidden-state off. */
    void setLanes(const juce::StringArray& labels);
    int getNumChips() const noexcept { return chips_.size(); }
    juce::ToggleButton* getChip(int index) const { return chips_[index]; }

    /** Sets a chip's state without notifying. */
    void setShown(int index, bool shown);
    bool isShown(int index) const;
    /** The chip the arrow keys have moved to (shown with the focus ring while the row has focus). */
    int getFocusedChip() const noexcept { return focused_; }

    /** Fired when the user toggles a chip (click, Space or an accessibility press). */
    std::function<void(int index, bool shown)> onToggle;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void toggled(int index);

    juce::OwnedArray<juce::ToggleButton> chips_;
    int focused_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneChipRow)
};

} // namespace synth::ui
