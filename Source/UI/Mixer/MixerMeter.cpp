// Concern: FRO11 (P9-5) -- MixerMeter's paint only (the ballistics live inline in the header).
#include "MixerMeter.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

void MixerMeter::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto track = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    const auto fill = laf != nullptr ? laf->getTheme().colors.meterFill : juce::Colour(0xff00D1FF);

    auto bounds = getLocalBounds().toFloat();
    g.setColour(track);
    g.fillRoundedRectangle(bounds, 2.0f);

    if (displayedLevel_ <= 0.0f)
        return;

    auto filled = bounds;
    filled = filled.removeFromBottom(bounds.getHeight() * displayedLevel_);
    juce::ColourGradient gradient(fill.brighter(0.3f), filled.getX(), filled.getY(), fill, filled.getX(),
                                  filled.getBottom(), false);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(filled, 2.0f);
}

} // namespace synth::ui
