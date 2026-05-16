#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ArrangementView : public juce::Component {
public:
    ArrangementView();
    void paint(juce::Graphics& g) override;
    void resized() override;
};
