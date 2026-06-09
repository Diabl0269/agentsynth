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

        auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel());
        if (lf != nullptr) {
            const auto& c = lf->getTheme().colors;
            bgColour = c.bg0;
            headerColour = c.textMuted;
            itemColour = c.textPrimary;
        }

        g.fillAll(bgColour);

        int y = 10;
        int headerIconOffset = 0; // index into header sequence for icon lookup
        for (const auto& entry : entries) {
            if (entry.isHeader) {
                if (y > 10)
                    y += 5; // extra spacing before headers (except first)
                g.setColour(headerColour);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));

                // Draw a 16x16 category icon at x=10 (null-guarded — no-op when LnF absent).
                gsynth::theme::Icon catIcon = categoryIconForHeader(entry.text);
                const juce::Drawable* icon = (lf != nullptr) ? lf->peekIcon(catIcon) : nullptr;

                if (icon != nullptr) {
                    icon->drawWithin(g, juce::Rectangle<float>(10.0f, (float)y + 2.0f, 16.0f, 16.0f),
                                     juce::RectanglePlacement::centred, 1.0f);
                    // Icon present: shift header text right to x=30.
                    g.drawText(entry.text.toUpperCase(), 30, y, getWidth() - 40, 20, juce::Justification::centredLeft);
                } else {
                    // No icon (headless or assets absent): text at original x=10.
                    g.drawText(entry.text.toUpperCase(), 10, y, getWidth() - 20, 20, juce::Justification::centredLeft);
                }

                y += 25;
                ++headerIconOffset;
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
    // Map a category header string to its Icon enum value.
    static gsynth::theme::Icon categoryIconForHeader(const juce::String& header) {
        if (header.equalsIgnoreCase("Sources"))
            return gsynth::theme::Icon::CatSources;
        if (header.equalsIgnoreCase("Sequencing"))
            return gsynth::theme::Icon::CatSequencing;
        if (header.startsWithIgnoreCase("Envelopes"))
            return gsynth::theme::Icon::CatEnvelopes;
        if (header.equalsIgnoreCase("Filters"))
            return gsynth::theme::Icon::CatFilters;
        if (header.startsWithIgnoreCase("Modulation"))
            return gsynth::theme::Icon::CatModulationFX;
        if (header.equalsIgnoreCase("Time FX"))
            return gsynth::theme::Icon::CatTimeFX;
        if (header.equalsIgnoreCase("Dynamics"))
            return gsynth::theme::Icon::CatDynamics;
        // "Utility" and any unrecognised headers fall back to CatUtility.
        return gsynth::theme::Icon::CatUtility;
    }

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
