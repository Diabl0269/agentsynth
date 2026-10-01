#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

// MixerSourceLine.h (docs/mixer/panel.md#what-the-mixer-shows): the small line under a strip column's
// header that names what plays into the channel, read as "From <sources>" with "From" muted. Hovering
// it lists every source; the same words are its accessible description. A label so the screen-reader
// tree gets its text as the name; it takes no focus (it is information, not a control), and a press on
// it reaches the column through the mouse listener the column registers.

namespace synth::ui {

class MixerSourceLine : public juce::Label {
public:
    static constexpr const char* kPrefix = "From ";

    MixerSourceLine() {
        setComponentID("mixerSourceLine");
        setInterceptsMouseClicks(true, false);
        setFont(juce::Font(juce::FontOptions(10.0f)));
        setJustificationType(juce::Justification::centredLeft);
    }

    /** `sources`: the feeding names, comma-joined; empty leaves the line blank and hidden. */
    void setSources(const juce::String& sources) {
        const auto text = sources.isEmpty() ? juce::String() : kPrefix + sources;
        const auto words = sources.isEmpty() ? juce::String() : "Plays into this channel: " + sources;
        setText(text, juce::dontSendNotification);
        setTooltip(words);
        setDescription(words);
        setVisible(sources.isNotEmpty());
    }

    void paint(juce::Graphics& g) override {
        const auto text = getText();
        if (text.isEmpty())
            return;
        juce::Colour muted(0xff8A93A0), name(0xffEAEEF3);
        if (const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            muted = lf->getTheme().colors.textMuted;
            name = lf->getTheme().colors.textPrimary;
        }
        auto area = getLocalBounds();
        const auto font = getFont();
        g.setFont(font);
        const juce::String prefix(kPrefix);
        const int prefixWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, prefix));
        g.setColour(muted);
        g.drawText(prefix, area.removeFromLeft(prefixWidth), juce::Justification::centredLeft, false);
        g.setColour(name);
        g.drawText(text.substring(prefix.length()), area, juce::Justification::centredLeft, true);
    }
};

} // namespace synth::ui
