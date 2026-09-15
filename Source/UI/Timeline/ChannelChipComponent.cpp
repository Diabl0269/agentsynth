#include "ChannelChipComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kMeterWidth = 26;
constexpr int kMeterGap = 4;
constexpr float kCornerRadius = 2.0f;

// Themed colours with literal fallbacks -- the headless test path installs no AppLookAndFeel, the
// same pattern TimelineTrackHeaderComponent's own coloursFor() uses.
struct ChipColours {
    juce::Colour surface{juce::Colour(0xff1B1F26)};
    juce::Colour border{juce::Colour(0xff2A2F38)};
    juce::Colour text{juce::Colour(0xffEAEEF3)};
    juce::Colour meter{juce::Colour(0xff00D1FF)};
};

ChipColours coloursFor(const juce::Component& component) {
    ChipColours result;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        result.surface = c.surface;
        result.border = c.border;
        result.text = c.textPrimary;
        result.meter = c.accent;
    }
    return result;
}

} // namespace

ChannelChipComponent::ChannelChipComponent()
    : juce::Button("timelineChannelChip") {
    setComponentID("trackChannelChip");
    // Same focus opt-out as the header's other toggles: clicking the chip must not silently move
    // real keyboard focus off the track row it belongs to.
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void ChannelChipComponent::setChannelName(const juce::String& name) {
    if (channelName_ == name)
        return;
    channelName_ = name;
    repaint();
}

bool ChannelChipComponent::setMeterLevel(float peak) {
    const float clamped = juce::jlimit(0.0f, 1.0f, peak);
    // The gate. A tick whose level did not move a visible amount repaints nothing at all -- except
    // when it lands exactly on silence, which must always be drawn (a decaying tail that stops
    // short of the threshold would otherwise leave the bar stuck showing a signal that is gone).
    const bool crossedToSilence = clamped <= 0.0f && meterLevel_ > 0.0f;
    if (!crossedToSilence && std::abs(clamped - meterLevel_) < kMeterRepaintThreshold)
        return false;
    meterLevel_ = clamped;
    repaint();
    return true;
}

void ChannelChipComponent::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto colours = coloursFor(*this);
    auto bounds = getLocalBounds().toFloat();

    g.setColour(highlighted ? colours.surface.brighter(0.15f) : colours.surface);
    g.fillRoundedRectangle(bounds, kCornerRadius);
    g.setColour(colours.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kCornerRadius, 1.0f);

    auto content = getLocalBounds().reduced(4, 1);
    auto meterArea = content.removeFromRight(kMeterWidth);
    content.removeFromRight(kMeterGap);

    g.setColour(colours.text);
    g.setFont(juce::Font(juce::FontOptions((float)std::min(11, std::max(8, content.getHeight() - 2)))));
    g.drawText(channelName_, content, juce::Justification::centredLeft, true);

    // The meter: a track plus the filled portion. Drawn from the gated meterLevel_, never from a
    // live atomic read -- see the header's timer note.
    const auto meterBounds = meterArea.toFloat().reduced(0.0f, (float)meterArea.getHeight() * 0.3f);
    g.setColour(colours.border);
    g.fillRoundedRectangle(meterBounds, 1.0f);
    if (meterLevel_ > 0.0f) {
        auto filled = meterBounds;
        filled.setWidth(meterBounds.getWidth() * meterLevel_);
        g.setColour(colours.meter);
        g.fillRoundedRectangle(filled, 1.0f);
    }
}

} // namespace synth::ui
