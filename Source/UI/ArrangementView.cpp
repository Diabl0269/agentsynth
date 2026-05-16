#include "ArrangementView.h"

ArrangementView::ArrangementView() {}

void ArrangementView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.drawText("Arrangement View", getLocalBounds(), juce::Justification::centred);
}

void ArrangementView::resized() {}
