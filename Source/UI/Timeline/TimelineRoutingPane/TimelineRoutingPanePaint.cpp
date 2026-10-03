// Concern: TimelineRoutingPane::paint -- the header line, the section headings and borders, and the muted lines.
#include "TimelineRoutingPane.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kInset = 8;
constexpr int kSwatchWidth = 8;
constexpr int kBadgePadding = 5;

struct PaneColours {
    juce::Colour text;
    juce::Colour muted;
    juce::Colour border;
    juce::Colour warning;
};

PaneColours coloursOf(const juce::Component& component) {
    const auto& c = synth::theme::themeOf(component).colors;
    return {c.textPrimary, c.textMuted, c.border, c.warning};
}

// Small sentence-case heading, like the Mixer pane's group headings but without the capitals.
void drawHeading(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& text,
                 const PaneColours& colours) {
    g.setColour(colours.muted);
    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    g.drawText(text, area, juce::Justification::bottomLeft, false);
}

void drawHeaderLine(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::Colour& swatch,
                    const juce::String& name, const juce::String& badge, const PaneColours& colours) {
    auto line = area.reduced(kInset, 0);
    g.setColour(swatch);
    g.fillRoundedRectangle(line.removeFromLeft(kSwatchWidth).reduced(0, 9).toFloat(), 2.0f);
    line.removeFromLeft(kInset);

    const juce::Font badgeFont{juce::FontOptions(9.5f, juce::Font::bold)};
    const int badgeWidth = static_cast<int>(std::ceil(badgeFont.getStringWidthFloat(badge))) + 2 * kBadgePadding;
    const auto badgeBounds = line.removeFromRight(badgeWidth).withSizeKeepingCentre(badgeWidth, 16).toFloat();
    g.setColour(colours.border);
    g.drawRoundedRectangle(badgeBounds.reduced(0.5f), 3.0f, 1.0f);
    g.setColour(colours.muted);
    g.setFont(badgeFont);
    g.drawText(badge, badgeBounds, juce::Justification::centred, false);

    line.removeFromRight(kInset);
    g.setColour(colours.text);
    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.drawText(name, line, juce::Justification::centredLeft, true);
}
} // namespace

void TimelineRoutingPane::paint(juce::Graphics& g) {
    const auto colours = coloursOf(*this);

    if (!view_.hasTrack) {
        g.setColour(colours.muted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText(view_.emptyLine, layout_.emptyLine, juce::Justification::topLeft, 3);
        return;
    }

    drawHeaderLine(g, layout_.header, view_.colour, view_.name, view_.kindBadge, colours);
    g.setColour(colours.border);
    g.fillRect(0, layout_.header.getBottom() - 1, getWidth(), 1);

    drawHeading(g, layout_.canvasNodeHeading, "Canvas node", colours);
    if (view_.missingNote.isNotEmpty()) {
        g.setColour(colours.warning);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawFittedText(view_.missingNote, layout_.missingNote, juce::Justification::topLeft, 3);
    }
    if (view_.isMidi)
        drawHeading(g, layout_.midiDestinationsHeading, "MIDI destinations", colours);
    drawHeading(g, layout_.mixerChannelHeading, "Mixer channel", colours);
    if (!layout_.noChannel.isEmpty()) {
        g.setColour(colours.muted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(view_.channelText, layout_.noChannel, juce::Justification::centredLeft, true);
    }

    g.setColour(colours.border);
    for (const int y : layout_.dividers)
        g.fillRect(0, y, getWidth(), 1);
}

} // namespace synth::ui
