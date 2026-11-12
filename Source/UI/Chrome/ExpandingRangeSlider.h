#pragma once

#include "UI/Controls/FineDrag/FineDragSlider.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// A juce::Slider whose text box accepts a value past the drag range instead of silently clamping
// it away: typing a tail longer than the slider currently spans grows the range to fit. Dragging
// still tops out at whatever the last-grown maximum is - only the text box can extend it further.
//
// Keyboard: the slider itself is a Tab stop (arrow keys, Home and End move it), and so is its editable
// value box, which keeps typing a value reachable; call nameValueBox() after setTitle() to name that box.
class ExpandingRangeSlider : public FineDragSlider {
public:
    ExpandingRangeSlider() { setWantsKeyboardFocus(true); }

    // Titles the value box "<slider title> value". Re-applied whenever the look and feel changes,
    // because the slider rebuilds its value box then.
    void nameValueBox() {
        for (auto* child : getChildren())
            if (auto* box = dynamic_cast<juce::Label*>(child))
                box->setTitle(getTitle() + " value");
    }

    void lookAndFeelChanged() override {
        juce::Slider::lookAndFeelChanged();
        nameValueBox();
    }

    double getValueFromText(const juce::String& text) override {
        const double typed = text.retainCharacters("0123456789.-").getDoubleValue();
        if (typed > getMaximum())
            setRange(getMinimum(), typed, getInterval());
        return typed;
    }
};

} // namespace synth::ui
