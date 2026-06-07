#pragma once

#include "Theme/GravisynthLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleLibraryComponent
    : public juce::Component
    , public juce::DragAndDropContainer {
public:
    struct Entry {
        juce::String text;
        bool isHeader;
    };

    ModuleLibraryComponent() {
        entries = {
            {"Sources", true},    {"Oscillator", false},    {"LFO", false},
            {"Sequencing", true}, {"Sequencer", false},     {"MidiKeyboard", false},
            {"Poly MIDI", false}, {"External MIDI", false}, {"Envelopes & Control", true},
            {"ADSR", false},      {"VCA", false},           {"Filters", true},
            {"Filter", false},    {"Modulation FX", true},  {"Chorus", false},
            {"Phaser", false},    {"Flanger", false},       {"Distortion", false},
            {"Time FX", true},    {"Delay", false},         {"Reverb", false},
            {"Dynamics", true},   {"Compressor", false},    {"Limiter", false},
            {"Utility", true},    {"Voice Mixer", false},
        };
    }

    void paint(juce::Graphics& g) override {
        // Resolve theme tokens from the active LnF; fall back to plain colors when our LnF
        // isn't installed (e.g. headless tests).
        juce::Colour bgColour = juce::Colours::darkgrey.darker();
        juce::Colour headerColour = juce::Colours::grey;
        juce::Colour itemColour = juce::Colours::white;

        if (auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            bgColour = c.bg0;
            headerColour = c.textMuted;
            itemColour = c.textPrimary;
        }

        g.fillAll(bgColour);

        int y = 10;
        for (const auto& entry : entries) {
            if (entry.isHeader) {
                if (y > 10)
                    y += 5; // extra spacing before headers (except first)
                g.setColour(headerColour);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                g.drawText(entry.text.toUpperCase(), 10, y, getWidth() - 20, 20, juce::Justification::centredLeft);
                y += 25;
            } else {
                g.setColour(itemColour);
                g.setFont(juce::Font(juce::FontOptions(16.0f)));
                g.drawText(entry.text, 20, y, getWidth() - 40, 28, juce::Justification::centredLeft);
                y += 32;
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int index = getEntryIndexAt(e.y);
        if (index >= 0 && index < (int)entries.size() && !entries[index].isHeader) {
            juce::Image dragImage(juce::Image::ARGB, 150, 30, true);
            juce::Graphics dg(dragImage);
            dg.setColour(juce::Colours::white);
            dg.setFont(juce::Font(16.0f));
            dg.drawText(entries[index].text, dragImage.getBounds(), juce::Justification::centred, false);

            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->startDragging(entries[index].text, this, dragImage);
        }
    }

private:
    int getEntryIndexAt(int mouseY) const {
        int y = 10;
        for (int i = 0; i < (int)entries.size(); ++i) {
            if (entries[i].isHeader) {
                if (y > 10)
                    y += 5;
                int h = 25;
                if (mouseY >= y && mouseY < y + h)
                    return i;
                y += h;
            } else {
                int h = 32;
                if (mouseY >= y && mouseY < y + h)
                    return i;
                y += h;
            }
        }
        return -1;
    }

    std::vector<Entry> entries;
};
