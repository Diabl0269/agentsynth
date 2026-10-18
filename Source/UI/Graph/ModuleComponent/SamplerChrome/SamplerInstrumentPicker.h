#pragma once

// The Sampler card's instrument name with a caret: a Tab stop that opens the picker list on click, Return or Space.

#include "UI/Layout/ChevronTurn.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class SamplerInstrumentPicker : public juce::Button {
public:
    SamplerInstrumentPicker();

    /** `name` empty = nothing loaded; `origin` ("built in") is shown dim after the name. */
    void setInstrument(const juce::String& name, const juce::String& origin);
    juce::String getInstrumentName() const { return name_; }

    /** True while the list is open: the caret points down. */
    void setListOpen(bool open) { caret_.setOpen(open); }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    /** Return and Space open the list. */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    juce::String name_, origin_;
    ChevronTurn caret_{*this, false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerInstrumentPicker)
};

} // namespace synth::ui
