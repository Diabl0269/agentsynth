#pragma once

#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

// One folding section of the Settings Audio tab (Device, Inputs, Remembered interfaces, Outputs, Recording, MIDI):
// the body under a header. The tab stacks the bodies and gives each the height it asks for at the tab's width.
class AudioSettingsSection : public juce::Component {
public:
    /** The height the body needs at `width`, folded or not. */
    virtual int preferredHeight(int width) const = 0;
    /** The one-line text after the header's name. */
    virtual juce::String summary() const { return {}; }
    /** Re-reads the machine and the layout store (a device or layout change); never steals focus. */
    virtual void refresh() = 0;

    /** The height `text` needs when wrapped to `width` in `font`. */
    static int wrappedHeight(const juce::String& text, const juce::Font& font, int width) {
        juce::AttributedString attributed;
        attributed.append(text, font);
        juce::TextLayout layout;
        layout.createLayout(attributed, (float)juce::jmax(40, width));
        return (int)std::ceil(layout.getHeight()) + 2;
    }

    static constexpr int kRowHeight = 28;
    static constexpr int kPad = 8;
    static constexpr int kLabelWidth = 150;
};
