#include "TimelinePanelComponent.h"
#include "Theme/AppLookAndFeel.h"

namespace synth::ui {

//==============================================================================
TimelinePanelComponent::TimelinePanelComponent() = default;

//==============================================================================
void TimelinePanelComponent::resized() {
    // Themed metrics with literal fallbacks for the headless test path (same pattern as
    // MainComponent::resized()/computePanelBounds()).
    int transportBarHeight = 28;
    int trackHeaderWidth = 160;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        transportBarHeight = m.timelineTransportBarHeight;
        trackHeaderWidth = m.timelineTrackHeaderWidth;
    }

    auto bounds = getLocalBounds();
    transportBarBounds_ = bounds.removeFromTop(transportBarHeight);
    trackHeaderBounds_ = bounds.removeFromLeft(trackHeaderWidth);
    lanesBounds_ = bounds; // remainder
}

//==============================================================================
void TimelinePanelComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    juce::Colour bg, border, textMuted;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.bg0;
        border = c.border;
        textMuted = c.textMuted;
    } else {
        bg = juce::Colours::darkgrey.darker(0.5f);
        border = juce::Colours::grey;
        textMuted = juce::Colours::grey;
    }

    g.fillAll(bg);

    // Thin top border separating the panel from the graph editor above it.
    g.setColour(border);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());

    // Placeholder — real transport/track-header/lane content arrives in TL5-2+.
    g.setColour(textMuted);
    g.setFont(juce::Font(13.0f));
    g.drawText("Timeline", lanesBounds_, juce::Justification::centred, true);
}

} // namespace synth::ui
