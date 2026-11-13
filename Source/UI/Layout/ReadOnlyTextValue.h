#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// A read-only accessibility value whose text is pulled from `getText` each time a screen reader asks.
// `getText` runs on the message thread and must stay valid for the handler's lifetime.
class ReadOnlyTextValue : public juce::AccessibilityTextValueInterface {
public:
    explicit ReadOnlyTextValue(std::function<juce::String()> getText)
        : getText_(std::move(getText)) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return getText_(); }
    void setValueAsString(const juce::String&) override {}

private:
    std::function<juce::String()> getText_;
};

} // namespace synth::ui
